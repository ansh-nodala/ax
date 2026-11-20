#include "runtime/core/sha256.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;
namespace {

struct Entry {
  std::string path;
  std::uint64_t size;
  std::array<std::uint8_t, 32> digest;
  friend bool operator==(const Entry&, const Entry&) = default;
};

std::string to_hex(const std::array<std::uint8_t, 32>& bytes) {
  constexpr char digits[] = "0123456789abcdef";
  std::string text;
  text.reserve(64);
  for (std::uint8_t byte : bytes) {
    text.push_back(digits[byte >> 4]);
    text.push_back(digits[byte & 15]);
  }
  return text;
}

bool from_hex(const std::string& text, std::array<std::uint8_t, 32>& bytes) {
  if (text.size() != 64) return false;
  const auto value = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
  };
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    const int high = value(text[i * 2]);
    const int low = value(text[i * 2 + 1]);
    if (high < 0 || low < 0) return false;
    bytes[i] = static_cast<std::uint8_t>((high << 4) | low);
  }
  return true;
}

bool hash_file(const fs::path& path, std::uint64_t& size,
               std::array<std::uint8_t, 32>& digest) {
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0) return false;
  struct stat before{};
  bool ok = ::fstat(fd, &before) == 0 && S_ISREG(before.st_mode) && before.st_size >= 0;
  ax::runtime::Sha256 sha;
  std::array<std::uint8_t, 64 * 1024> buffer{};
  std::uint64_t total = 0;
  while (ok) {
    const ssize_t count = ::read(fd, buffer.data(), buffer.size());
    if (count < 0 && errno == EINTR) continue;
    if (count < 0) { ok = false; break; }
    if (count == 0) break;
    total += static_cast<std::uint64_t>(count);
    sha.update(std::as_bytes(std::span(buffer.data(), static_cast<std::size_t>(count))));
  }
  struct stat after{};
  if (::fstat(fd, &after) != 0) ok = false;
  if (::close(fd) != 0) ok = false;
  if (!ok || before.st_dev != after.st_dev || before.st_ino != after.st_ino ||
      before.st_size != after.st_size || before.st_mtime != after.st_mtime ||
      total != static_cast<std::uint64_t>(before.st_size)) return false;
  size = total;
  digest = sha.finish();
  return true;
}

bool excluded_directory(const fs::path& path) {
  const std::string name = path.filename().string();
  return name == ".git" || name == "build" || name == "target" ||
         name == "node_modules" || name == "outputs" || name == "work";
}

bool collect_entries(std::vector<Entry>& entries, std::string& error) {
  std::error_code ec;
  for (fs::recursive_directory_iterator it(".", fs::directory_options::skip_permission_denied, ec), end;
       it != end; it.increment(ec)) {
    if (ec) { error = "directory traversal failed: " + ec.message(); return false; }
    const fs::path relative = it->path().lexically_relative(".");
    if (it->is_symlink(ec)) {
      error = "symlinks are rejected while creating a package lock: " + relative.generic_string();
      return false;
    }
    if (it->is_directory(ec)) {
      if (excluded_directory(relative)) it.disable_recursion_pending();
      continue;
    }
    if (ec) { error = "cannot inspect " + relative.generic_string(); return false; }
    if (!it->is_regular_file(ec)) continue;
    const std::string name = relative.generic_string();
    if (name == "axpkg.lock" || name.starts_with("axpkg.lock.tmp.")) continue;
    Entry entry{name, 0, {}};
    if (!hash_file(it->path(), entry.size, entry.digest)) {
      error = "file changed or could not be hashed: " + name;
      return false;
    }
    entries.push_back(std::move(entry));
  }
  if (ec) { error = "directory traversal failed: " + ec.message(); return false; }
  std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.path < b.path; });
  return true;
}

std::array<std::uint8_t, 32> tree_digest(const std::vector<Entry>& entries) {
  ax::runtime::Sha256 sha;
  for (const Entry& entry : entries) {
    sha.update(std::as_bytes(std::span(entry.path.data(), entry.path.size())));
    constexpr std::uint8_t separator = 0;
    sha.update(std::as_bytes(std::span(&separator, 1)));
    std::array<std::uint8_t, 8> size_bytes{};
    for (unsigned i = 0; i < 8; ++i) size_bytes[i] = static_cast<std::uint8_t>(entry.size >> (i * 8));
    sha.update(std::as_bytes(std::span(size_bytes)));
    sha.update(std::as_bytes(std::span(entry.digest)));
  }
  return sha.finish();
}

bool write_lock(const std::vector<Entry>& entries, const std::array<std::uint8_t, 32>& root_hash) {
  std::ostringstream text;
  text << "AXPKG-LOCK 1\n";
  for (const Entry& entry : entries) {
    text << "file " << std::quoted(entry.path) << ' ' << entry.size << ' ' << to_hex(entry.digest) << '\n';
  }
  text << "tree " << to_hex(root_hash) << '\n';
  const std::string contents = text.str();
  std::string pattern = "axpkg.lock.tmp.XXXXXX";
  std::vector<char> temporary(pattern.begin(), pattern.end());
  temporary.push_back('\0');
  const int fd = ::mkstemp(temporary.data());
  if (fd < 0) return false;
  bool ok = ::fchmod(fd, 0644) == 0;
  const char* bytes = contents.data();
  std::size_t remaining = contents.size();
  while (ok && remaining != 0) {
    const ssize_t count = ::write(fd, bytes, remaining);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) { ok = false; break; }
    bytes += count;
    remaining -= static_cast<std::size_t>(count);
  }
  if (ok) ok = ::fsync(fd) == 0;
  if (::close(fd) != 0) ok = false;
  const std::string temp_path = temporary.data();
  if (ok) ok = ::rename(temp_path.c_str(), "axpkg.lock") == 0;
  if (ok) {
    const int dir_fd = ::open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir_fd < 0 || ::fsync(dir_fd) != 0) ok = false;
    if (dir_fd >= 0) ::close(dir_fd);
  }
  if (!ok) ::unlink(temp_path.c_str());
  return ok;
}

bool create_lock() {
  std::vector<Entry> entries;
  std::string error;
  if (!collect_entries(entries, error)) { std::cerr << "axpkg: " << error << '\n'; return false; }
  const auto digest = tree_digest(entries);
  if (!write_lock(entries, digest)) { std::cerr << "axpkg: could not atomically write axpkg.lock\n"; return false; }
  std::cout << "axpkg.lock updated: " << entries.size() << " files, SHA-256 " << to_hex(digest) << '\n';
  return true;
}

bool verify_lock() {
  std::ifstream input("axpkg.lock");
  if (!input) { std::cerr << "axpkg: axpkg.lock not found; run axpkg lock\n"; return false; }
  std::string line;
  if (!std::getline(input, line) || line != "AXPKG-LOCK 1") {
    std::cerr << "axpkg: unsupported or malformed lockfile header\n"; return false;
  }
  std::vector<Entry> locked;
  std::array<std::uint8_t, 32> expected_tree{};
  bool saw_tree = false;
  while (std::getline(input, line)) {
    std::istringstream row(line);
    std::string kind;
    row >> kind;
    if (kind == "file") {
      Entry entry{};
      std::string hash;
      std::string extra;
      if (!(row >> std::quoted(entry.path) >> entry.size >> hash) || (row >> extra) ||
          entry.path.empty() || entry.path.starts_with('/') || entry.path.find("..") != std::string::npos ||
          !from_hex(hash, entry.digest)) {
        std::cerr << "axpkg: malformed file entry in lockfile\n"; return false;
      }
      locked.push_back(std::move(entry));
    } else if (kind == "tree") {
      std::string hash, extra;
      if (saw_tree || !(row >> hash) || (row >> extra) || !from_hex(hash, expected_tree)) {
        std::cerr << "axpkg: malformed tree digest in lockfile\n"; return false;
      }
      saw_tree = true;
      if (input.peek() != std::char_traits<char>::eof()) {
        std::cerr << "axpkg: trailing data after tree digest\n"; return false;
      }
    } else {
      std::cerr << "axpkg: unknown lockfile record\n"; return false;
    }
  }
  if (!saw_tree) { std::cerr << "axpkg: lockfile is missing its tree digest\n"; return false; }
  std::vector<Entry> current;
  std::string error;
  if (!collect_entries(current, error)) { std::cerr << "axpkg: " << error << '\n'; return false; }
  if (current != locked || tree_digest(current) != expected_tree) {
    std::cerr << "axpkg: package tree does not match axpkg.lock\n";
    return false;
  }
  std::cout << "axpkg.lock verified: " << current.size() << " files, SHA-256 " << to_hex(expected_tree) << '\n';
  return true;
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cout << "usage: axpkg <init|list|add|remove|lock|verify> [name]\n";
    return 0;
  }
  const std::string command = argv[1];
  if (command == "lock") return create_lock() ? 0 : 1;
  if (command == "verify") return verify_lock() ? 0 : 1;
  if (command == "init") {
    std::ofstream output("axpkg.toml");
    if (!output) { std::cerr << "axpkg: cannot create axpkg.toml\n"; return 1; }
    output << "[package]\nname = \"ax-app\"\nversion = \"0.1.0\"\nedition = \"2026\"\n[dependencies]\n";
    std::cout << "created axpkg.toml\n";
    return output ? 0 : 1;
  }
  if (command == "list") {
    std::ifstream input("axpkg.toml");
    if (!input) { std::cerr << "no axpkg.toml — run axpkg init\n"; return 1; }
    std::cout << input.rdbuf();
    return input.bad() ? 1 : 0;
  }
  if (command == "add" && argc >= 3) {
    std::ofstream output("axpkg.toml", std::ios::app);
    if (!output) { std::cerr << "axpkg: cannot update axpkg.toml\n"; return 1; }
    output << argv[2] << " = \"1.0.0\"\n";
    std::cout << "added " << argv[2] << '\n';
    return output ? 0 : 1;
  }
  if (command == "remove" && argc >= 3) {
    std::ifstream input("axpkg.toml");
    if (!input) { std::cerr << "no axpkg.toml — run axpkg init\n"; return 1; }
    std::ostringstream contents;
    contents << input.rdbuf();
    std::string source = contents.str();
    const std::string needle = std::string(argv[2]) + " =";
    const std::size_t position = source.find(needle);
    if (position == std::string::npos) { std::cout << "not found: " << argv[2] << '\n'; return 0; }
    const std::size_t end = source.find('\n', position);
    source.erase(position, end == std::string::npos ? std::string::npos : end - position + 1);
    std::ofstream output("axpkg.toml", std::ios::trunc);
    output << source;
    std::cout << "removed " << argv[2] << '\n';
    return output ? 0 : 1;
  }
  std::cerr << "unknown axpkg command\n";
  return 1;
}
