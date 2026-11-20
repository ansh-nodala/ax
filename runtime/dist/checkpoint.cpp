#include "runtime/dist/dist.hpp"

#include "runtime/core/sha256.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace ax {
namespace {

constexpr std::array<std::uint8_t, 8> magic{'A', 'X', 'C', 'K', 'P', 'T', '1', 0};
constexpr std::size_t digest_size = 32;
constexpr std::size_t value_block = 1024;

bool write_all(int fd, const void* source, std::size_t size) {
  const auto* bytes = static_cast<const std::byte*>(source);
  while (size > 0) {
    const ssize_t written = ::write(fd, bytes, size);
    if (written < 0 && errno == EINTR) continue;
    if (written <= 0) return false;
    bytes += written;
    size -= static_cast<std::size_t>(written);
  }
  return true;
}

bool read_all(int fd, void* destination, std::size_t size) {
  auto* bytes = static_cast<std::byte*>(destination);
  while (size > 0) {
    const ssize_t count = ::read(fd, bytes, size);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) return false;
    bytes += count;
    size -= static_cast<std::size_t>(count);
  }
  return true;
}

std::array<std::uint8_t, 8> encode_u64(std::uint64_t value) {
  std::array<std::uint8_t, 8> bytes{};
  for (unsigned i = 0; i < 8; ++i) bytes[i] = static_cast<std::uint8_t>(value >> (i * 8));
  return bytes;
}

std::uint64_t decode_u64(const std::array<std::uint8_t, 8>& bytes) {
  std::uint64_t value = 0;
  for (unsigned i = 0; i < 8; ++i) value |= static_cast<std::uint64_t>(bytes[i]) << (i * 8);
  return value;
}

void encode_values(std::span<const double> values, std::span<std::uint8_t> bytes) {
  for (std::size_t i = 0; i < values.size(); ++i) {
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(values[i]);
    for (unsigned byte = 0; byte < 8; ++byte) {
      bytes[i * 8 + byte] = static_cast<std::uint8_t>(bits >> (byte * 8));
    }
  }
}

void decode_values(std::span<const std::uint8_t> bytes, std::span<double> values) {
  for (std::size_t i = 0; i < values.size(); ++i) {
    std::uint64_t bits = 0;
    for (unsigned byte = 0; byte < 8; ++byte) {
      bits |= static_cast<std::uint64_t>(bytes[i * 8 + byte]) << (byte * 8);
    }
    values[i] = std::bit_cast<double>(bits);
  }
}

bool write_checkpoint(const std::string& path, const std::vector<double>& params) {
  if (path.empty() || params.size() > std::numeric_limits<std::uint64_t>::max() / sizeof(double)) return false;
  const std::string temporary = path + ".tmp." + std::to_string(static_cast<long long>(::getpid()));
  const int fd = ::open(temporary.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
  if (fd < 0) return false;

  bool ok = true;
  runtime::Sha256 sha;
  const auto count = encode_u64(static_cast<std::uint64_t>(params.size()));
  ok = write_all(fd, magic.data(), magic.size()) && write_all(fd, count.data(), count.size());
  sha.update(std::as_bytes(std::span(magic)));
  sha.update(std::as_bytes(std::span(count)));

  std::array<std::uint8_t, value_block * sizeof(double)> bytes{};
  for (std::size_t offset = 0; ok && offset < params.size(); offset += value_block) {
    const std::size_t amount = std::min(value_block, params.size() - offset);
    auto block = std::span<std::uint8_t>(bytes.data(), amount * sizeof(double));
    encode_values(std::span<const double>(params.data() + offset, amount), block);
    sha.update(std::as_bytes(block));
    ok = write_all(fd, block.data(), block.size());
  }
  const auto digest = sha.finish();
  if (ok) ok = write_all(fd, digest.data(), digest.size());
  if (ok) ok = ::fsync(fd) == 0;
  const int close_result = ::close(fd);
  if (close_result != 0) ok = false;
  if (ok) ok = ::rename(temporary.c_str(), path.c_str()) == 0;
  if (ok) {
    const std::filesystem::path parent = std::filesystem::path(path).parent_path();
    const std::string directory = parent.empty() ? "." : parent.string();
    const int dir_fd = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir_fd >= 0) {
      if (::fsync(dir_fd) != 0) ok = false;
      ::close(dir_fd);
    }
  }
  if (!ok) ::unlink(temporary.c_str());
  return ok;
}

bool read_checkpoint(const std::string& path, std::vector<double>* params,
                     std::array<std::uint8_t, 32>* output_digest) {
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) return false;
  struct stat info {};
  if (::fstat(fd, &info) != 0 || info.st_size < 0 ||
      static_cast<std::uint64_t>(info.st_size) < magic.size() + 8 + digest_size) {
    ::close(fd);
    return false;
  }
  std::array<std::uint8_t, 8> found_magic{};
  std::array<std::uint8_t, 8> count_bytes{};
  bool ok = read_all(fd, found_magic.data(), found_magic.size()) &&
            read_all(fd, count_bytes.data(), count_bytes.size()) && found_magic == magic;
  const std::uint64_t count = decode_u64(count_bytes);
  if (count > std::numeric_limits<std::size_t>::max() / sizeof(double)) ok = false;
  const std::uint64_t value_bytes = count * sizeof(double);
  const std::uint64_t expected_size = magic.size() + 8 + value_bytes + digest_size;
  if (expected_size != static_cast<std::uint64_t>(info.st_size)) ok = false;
  runtime::Sha256 sha;
  sha.update(std::as_bytes(std::span(found_magic)));
  sha.update(std::as_bytes(std::span(count_bytes)));
  std::vector<double> loaded;
  if (ok && params != nullptr) loaded.resize(static_cast<std::size_t>(count));
  std::array<std::uint8_t, value_block * sizeof(double)> bytes{};
  std::size_t remaining = static_cast<std::size_t>(value_bytes);
  std::size_t offset = 0;
  while (ok && remaining > 0) {
    const std::size_t amount = std::min(remaining, bytes.size());
    ok = read_all(fd, bytes.data(), amount);
    if (!ok) break;
    const auto block = std::span<const std::uint8_t>(bytes.data(), amount);
    sha.update(std::as_bytes(block));
    if (params != nullptr) {
      decode_values(block, std::span<double>(loaded.data() + offset, amount / sizeof(double)));
    }
    offset += amount / sizeof(double);
    remaining -= amount;
  }
  std::array<std::uint8_t, digest_size> saved_digest{};
  if (ok) ok = read_all(fd, saved_digest.data(), saved_digest.size());
  const int close_result = ::close(fd);
  if (close_result != 0) ok = false;
  if (!ok) return false;
  const auto actual_digest = sha.finish();
  if (actual_digest != saved_digest) return false;
  if (output_digest != nullptr) *output_digest = actual_digest;
  if (params != nullptr) *params = std::move(loaded);
  return true;
}

} // namespace

bool ax_dist_checkpoint_write(const std::string& path, const std::vector<double>& params) {
  return write_checkpoint(path, params);
}

bool ax_dist_checkpoint_read(const std::string& path, std::vector<double>& params) {
  return read_checkpoint(path, &params, nullptr);
}

bool ax_dist_checkpoint_verify(const std::string& path,
                               std::array<std::uint8_t, 32>* digest) {
  return read_checkpoint(path, nullptr, digest);
}

} // namespace ax
