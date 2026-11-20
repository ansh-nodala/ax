#include "runtime/core/asset_cooker.hpp"

#include "runtime/core/sha256.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <span>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ax::asset {
namespace {

constexpr std::array<std::uint8_t, 8> bundle_magic{'A', 'X', 'B', 'N', 'D', 'L', '1', 0};
constexpr std::size_t bundle_header_size = 56;
constexpr std::size_t maximum_source_size = 512u * 1024u * 1024u;

struct Vec3 {
  float x;
  float y;
  float z;
};
struct Vec2 { float x; float y; };
struct ObjRef { int position; int uv; int normal; };
struct VertexKey {
  int position;
  int uv;
  int normal;
  friend bool operator==(const VertexKey&, const VertexKey&) = default;
};
struct VertexHash {
  std::size_t operator()(const VertexKey& key) const noexcept {
    std::size_t hash = static_cast<std::size_t>(static_cast<std::uint32_t>(key.position));
    hash ^= static_cast<std::size_t>(static_cast<std::uint32_t>(key.uv)) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
    hash ^= static_cast<std::size_t>(static_cast<std::uint32_t>(key.normal)) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
    return hash;
  }
};
struct Meshlet {
  std::vector<std::uint32_t> vertices;
  std::vector<std::uint8_t> local_indices;
};

void append_u16(std::vector<std::uint8_t>& output, std::uint16_t value) {
  output.push_back(static_cast<std::uint8_t>(value));
  output.push_back(static_cast<std::uint8_t>(value >> 8));
}

void append_u32(std::vector<std::uint8_t>& output, std::uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) output.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}

void append_u64(std::vector<std::uint8_t>& output, std::uint64_t value) {
  for (unsigned i = 0; i < 8; ++i) output.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}

void append_f32(std::vector<std::uint8_t>& output, float value) {
  append_u32(output, std::bit_cast<std::uint32_t>(value));
}

std::uint16_t read_u16(const std::uint8_t* bytes) {
  return static_cast<std::uint16_t>(bytes[0]) | static_cast<std::uint16_t>(bytes[1] << 8);
}

std::uint32_t read_u32_le(const std::uint8_t* bytes) {
  return static_cast<std::uint32_t>(bytes[0]) |
         (static_cast<std::uint32_t>(bytes[1]) << 8) |
         (static_cast<std::uint32_t>(bytes[2]) << 16) |
         (static_cast<std::uint32_t>(bytes[3]) << 24);
}

std::uint32_t read_u32_be(const std::uint8_t* bytes) {
  return (static_cast<std::uint32_t>(bytes[0]) << 24) |
         (static_cast<std::uint32_t>(bytes[1]) << 16) |
         (static_cast<std::uint32_t>(bytes[2]) << 8) |
         static_cast<std::uint32_t>(bytes[3]);
}

std::uint64_t read_u64_le(const std::uint8_t* bytes) {
  std::uint64_t value = 0;
  for (unsigned i = 0; i < 8; ++i) value |= static_cast<std::uint64_t>(bytes[i]) << (8 * i);
  return value;
}

bool read_file(const std::string& path, std::vector<std::uint8_t>& bytes, std::string& error) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) { error = "cannot open input file"; return false; }
  const std::streamoff size = input.tellg();
  if (size < 0 || static_cast<std::uint64_t>(size) > maximum_source_size) {
    error = "input exceeds the 512 MiB cooker limit";
    return false;
  }
  bytes.resize(static_cast<std::size_t>(size));
  input.seekg(0);
  if (!bytes.empty() && !input.read(reinterpret_cast<char*>(bytes.data()), size)) {
    error = "failed while reading input file";
    return false;
  }
  return true;
}

bool write_all(int fd, const std::uint8_t* bytes, std::size_t size) {
  while (size != 0) {
    const ssize_t written = ::write(fd, bytes, size);
    if (written < 0 && errno == EINTR) continue;
    if (written <= 0) return false;
    bytes += written;
    size -= static_cast<std::size_t>(written);
  }
  return true;
}

bool atomic_write(const std::string& path, const std::vector<std::uint8_t>& bytes) {
  std::string temporary = path + ".tmp.XXXXXX";
  std::vector<char> template_name(temporary.begin(), temporary.end());
  template_name.push_back('\0');
  const int fd = ::mkstemp(template_name.data());
  if (fd < 0) return false;
  temporary = template_name.data();
  bool ok = ::fcntl(fd, F_SETFD, FD_CLOEXEC) == 0 && ::fchmod(fd, 0644) == 0 &&
            write_all(fd, bytes.data(), bytes.size()) && ::fsync(fd) == 0;
  if (::close(fd) != 0) ok = false;
  if (ok) ok = ::rename(temporary.c_str(), path.c_str()) == 0;
  if (ok) {
    const std::filesystem::path parent_path = std::filesystem::path(path).parent_path();
    const std::string parent = parent_path.empty() ? "." : parent_path.string();
    const int dir_fd = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir_fd < 0 || ::fsync(dir_fd) != 0) ok = false;
    if (dir_fd >= 0) ::close(dir_fd);
  }
  if (!ok) ::unlink(temporary.c_str());
  return ok;
}

Vec3 subtract(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 cross(Vec3 a, Vec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Vec3 normalized(Vec3 value) {
  const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
  if (length <= 1.0e-20f) return {};
  return {value.x / length, value.y / length, value.z / length};
}

bool parse_int(std::string_view text, int& value) {
  if (text.empty()) return false;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && value != 0;
}

bool resolve_index(std::string_view text, std::size_t count, int& resolved) {
  int raw = 0;
  if (!parse_int(text, raw) || count > static_cast<std::size_t>(std::numeric_limits<int>::max())) return false;
  const long long index = raw > 0 ? static_cast<long long>(raw) - 1
                                  : static_cast<long long>(count) + raw;
  if (index < 0 || index >= static_cast<long long>(count)) return false;
  resolved = static_cast<int>(index);
  return true;
}

bool parse_obj_ref(std::string_view token, std::size_t position_count, std::size_t uv_count,
                   std::size_t normal_count, ObjRef& ref) {
  const std::size_t first_slash = token.find('/');
  const std::string_view position = token.substr(0, first_slash);
  if (!resolve_index(position, position_count, ref.position)) return false;
  ref.uv = -1;
  ref.normal = -1;
  if (first_slash == std::string_view::npos) return true;
  const std::size_t second_slash = token.find('/', first_slash + 1);
  const std::string_view uv = token.substr(first_slash + 1,
      (second_slash == std::string_view::npos ? token.size() : second_slash) - first_slash - 1);
  if (!uv.empty() && !resolve_index(uv, uv_count, ref.uv)) return false;
  if (second_slash != std::string_view::npos) {
    const std::string_view normal = token.substr(second_slash + 1);
    if (!normal.empty() && !resolve_index(normal, normal_count, ref.normal)) return false;
  }
  return true;
}

bool parse_float_token(const std::string& token, float& value) {
  const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
  return parsed.ec == std::errc{} && parsed.ptr == token.data() + token.size() && std::isfinite(value);
}

bool cook_obj(const std::vector<std::uint8_t>& source, std::vector<std::uint8_t>& payload,
              CookStats& stats, std::string& error) {
  if (source.empty()) { error = "OBJ input is empty"; return false; }
  std::string text(reinterpret_cast<const char*>(source.data()), source.size());
  std::istringstream lines(text);
  std::vector<Vec3> positions, normals;
  std::vector<Vec2> uvs;
  std::vector<Vec3> output_positions, output_normals;
  std::vector<Vec2> output_uvs;
  std::vector<std::uint32_t> indices;
  std::unordered_map<VertexKey, std::uint32_t, VertexHash> vertex_map;
  std::string line;
  std::size_t line_number = 0;
  std::size_t triangle_number = 0;
  while (std::getline(lines, line)) {
    ++line_number;
    std::istringstream fields(line);
    std::string kind;
    fields >> kind;
    if (kind.empty() || kind[0] == '#') continue;
    if (kind == "v" || kind == "vn") {
      std::string sx, sy, sz;
      float x = 0.0f, y = 0.0f, z = 0.0f;
      if (!(fields >> sx >> sy) || !parse_float_token(sx, x) || !parse_float_token(sy, y)) {
        error = "invalid vector at OBJ line " + std::to_string(line_number); return false;
      }
      if (kind == "v") {
        if (!(fields >> sz) || !parse_float_token(sz, z)) {
          error = "invalid position at OBJ line " + std::to_string(line_number); return false;
        }
        positions.push_back({x, y, z});
      } else {
        if (!(fields >> sz) || !parse_float_token(sz, z)) {
          error = "invalid normal at OBJ line " + std::to_string(line_number); return false;
        }
        normals.push_back(normalized({x, y, z}));
      }
    } else if (kind == "vt") {
      std::string sx, sy;
      float x = 0.0f, y = 0.0f;
      if (!(fields >> sx >> sy) || !parse_float_token(sx, x) || !parse_float_token(sy, y)) {
        error = "invalid UV at OBJ line " + std::to_string(line_number); return false;
      }
      uvs.push_back({x, y});
    } else if (kind == "f") {
      std::vector<ObjRef> face;
      std::string item;
      while (fields >> item) {
        if (!item.empty() && item[0] == '#') break;
        ObjRef ref{};
        if (!parse_obj_ref(item, positions.size(), uvs.size(), normals.size(), ref)) {
          error = "invalid face index at OBJ line " + std::to_string(line_number); return false;
        }
        face.push_back(ref);
      }
      if (face.size() < 3) {
        error = "face has fewer than three vertices at OBJ line " + std::to_string(line_number); return false;
      }
      for (std::size_t corner = 1; corner + 1 < face.size(); ++corner) {
        const ObjRef triangle[3]{face[0], face[corner], face[corner + 1]};
        const Vec3 face_normal = normalized(cross(subtract(positions[triangle[1].position], positions[triangle[0].position]),
                                                   subtract(positions[triangle[2].position], positions[triangle[0].position])));
        for (const ObjRef& ref : triangle) {
          const int normal_key = ref.normal >= 0 ? ref.normal : -2 - static_cast<int>(triangle_number);
          const VertexKey key{ref.position, ref.uv, normal_key};
          auto found = vertex_map.find(key);
          if (found == vertex_map.end()) {
            if (output_positions.size() >= std::numeric_limits<std::uint32_t>::max()) {
              error = "OBJ exceeds 32-bit vertex indexing"; return false;
            }
            const std::uint32_t vertex = static_cast<std::uint32_t>(output_positions.size());
            vertex_map.emplace(key, vertex);
            output_positions.push_back(positions[ref.position]);
            output_normals.push_back(ref.normal >= 0 ? normals[ref.normal] : face_normal);
            output_uvs.push_back(ref.uv >= 0 ? uvs[ref.uv] : Vec2{});
            indices.push_back(vertex);
          } else {
            indices.push_back(found->second);
          }
        }
        ++triangle_number;
      }
    }
    // Object, group, material, and smoothing labels do not change cooked geometry.
  }
  if (indices.empty()) { error = "OBJ contains no faces"; return false; }

  std::vector<Meshlet> meshlets;
  Meshlet current;
  std::unordered_map<std::uint32_t, std::uint8_t> local_map;
  for (std::size_t tri = 0; tri < indices.size(); tri += 3) {
    std::size_t added = 0;
    for (std::size_t lane = 0; lane < 3; ++lane) {
      if (!local_map.contains(indices[tri + lane]) &&
          std::find(indices.begin() + static_cast<std::ptrdiff_t>(tri),
                    indices.begin() + static_cast<std::ptrdiff_t>(tri + lane), indices[tri + lane]) ==
              indices.begin() + static_cast<std::ptrdiff_t>(tri + lane)) ++added;
    }
    if (!current.local_indices.empty() &&
        (current.local_indices.size() / 3 >= 126 || current.vertices.size() + added > 64)) {
      meshlets.push_back(std::move(current));
      current = {};
      local_map.clear();
    }
    for (std::size_t lane = 0; lane < 3; ++lane) {
      const std::uint32_t global = indices[tri + lane];
      auto found = local_map.find(global);
      if (found == local_map.end()) {
        const auto local = static_cast<std::uint8_t>(current.vertices.size());
        current.vertices.push_back(global);
        local_map.emplace(global, local);
        current.local_indices.push_back(local);
      } else {
        current.local_indices.push_back(found->second);
      }
    }
  }
  if (!current.local_indices.empty()) meshlets.push_back(std::move(current));

  append_u32(payload, 1); // mesh payload version
  append_u32(payload, static_cast<std::uint32_t>(output_positions.size()));
  append_u32(payload, static_cast<std::uint32_t>(indices.size()));
  append_u32(payload, static_cast<std::uint32_t>(meshlets.size()));
  for (std::size_t i = 0; i < output_positions.size(); ++i) {
    append_f32(payload, output_positions[i].x); append_f32(payload, output_positions[i].y);
    append_f32(payload, output_positions[i].z); append_f32(payload, output_normals[i].x);
    append_f32(payload, output_normals[i].y); append_f32(payload, output_normals[i].z);
    append_f32(payload, output_uvs[i].x); append_f32(payload, output_uvs[i].y);
  }
  for (std::uint32_t index : indices) append_u32(payload, index);
  for (const Meshlet& meshlet : meshlets) {
    append_u32(payload, static_cast<std::uint32_t>(meshlet.vertices.size()));
    append_u32(payload, static_cast<std::uint32_t>(meshlet.local_indices.size() / 3));
    for (std::uint32_t index : meshlet.vertices) append_u32(payload, index);
    payload.insert(payload.end(), meshlet.local_indices.begin(), meshlet.local_indices.end());
  }
  stats.kind = Kind::Mesh;
  stats.vertices = output_positions.size();
  stats.triangles = indices.size() / 3;
  stats.meshlets = meshlets.size();
  return true;
}

bool cook_wav(const std::vector<std::uint8_t>& source, std::vector<std::uint8_t>& payload,
              CookStats& stats, std::string& error) {
  if (source.size() < 12 || std::string_view(reinterpret_cast<const char*>(source.data()), 4) != "RIFF" ||
      std::string_view(reinterpret_cast<const char*>(source.data() + 8), 4) != "WAVE") {
    error = "input is not a RIFF/WAVE file"; return false;
  }
  std::uint16_t format = 0, channels = 0, bits = 0, block_align = 0;
  std::uint32_t sample_rate = 0;
  const std::uint8_t* sample_data = nullptr;
  std::size_t sample_size = 0;
  for (std::size_t offset = 12; offset + 8 <= source.size();) {
    const std::string_view chunk(reinterpret_cast<const char*>(source.data() + offset), 4);
    const std::uint32_t length = read_u32_le(source.data() + offset + 4);
    offset += 8;
    if (length > source.size() - offset) { error = "WAV chunk exceeds file bounds"; return false; }
    if (chunk == "fmt ") {
      if (length < 16) { error = "WAV fmt chunk is too short"; return false; }
      format = read_u16(source.data() + offset);
      channels = read_u16(source.data() + offset + 2);
      sample_rate = read_u32_le(source.data() + offset + 4);
      block_align = read_u16(source.data() + offset + 12);
      bits = read_u16(source.data() + offset + 14);
    } else if (chunk == "data") {
      sample_data = source.data() + offset;
      sample_size = length;
    }
    offset += length + (length & 1u);
  }
  const bool supported_bits = bits == 8 || bits == 16 || bits == 24 || bits == 32;
  const std::uint32_t expected_align = static_cast<std::uint32_t>(channels) * (bits / 8);
  if ((format != 1 && format != 3) || channels == 0 || sample_rate == 0 || !supported_bits ||
      (format == 3 && bits != 32) || expected_align != block_align || sample_data == nullptr ||
      sample_size == 0 || sample_size % block_align != 0) {
    error = "WAV must contain supported PCM or 32-bit IEEE float sample data"; return false;
  }
  append_u32(payload, 1); // audio payload version
  append_u16(payload, format); append_u16(payload, channels);
  append_u32(payload, sample_rate); append_u16(payload, bits); append_u16(payload, block_align);
  append_u64(payload, sample_size);
  payload.insert(payload.end(), sample_data, sample_data + sample_size);
  stats.kind = Kind::Audio;
  return true;
}

std::uint32_t crc32(std::span<const std::uint8_t> bytes) {
  std::uint32_t crc = 0xffffffffu;
  for (std::uint8_t byte : bytes) {
    crc ^= byte;
    for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

bool cook_png(const std::vector<std::uint8_t>& source, std::vector<std::uint8_t>& payload,
              CookStats& stats, std::string& error) {
  constexpr std::array<std::uint8_t, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
  if (source.size() < 8 || !std::equal(signature.begin(), signature.end(), source.begin())) {
    error = "input is not a PNG file"; return false;
  }
  bool saw_header = false, saw_data = false, saw_end = false;
  std::uint32_t width = 0, height = 0;
  for (std::size_t offset = 8; offset + 12 <= source.size();) {
    const std::uint32_t length = read_u32_be(source.data() + offset);
    if (length > source.size() - offset - 12) { error = "PNG chunk exceeds file bounds"; return false; }
    const auto* type = source.data() + offset + 4;
    const auto* data = source.data() + offset + 8;
    const auto expected_crc = read_u32_be(data + length);
    const auto chunk_crc = crc32(std::span<const std::uint8_t>(type, static_cast<std::size_t>(length) + 4));
    if (chunk_crc != expected_crc) { error = "PNG chunk CRC mismatch"; return false; }
    const std::string_view name(reinterpret_cast<const char*>(type), 4);
    if (!saw_header) {
      if (name != "IHDR" || length != 13) { error = "PNG must start with a 13-byte IHDR"; return false; }
      width = read_u32_be(data); height = read_u32_be(data + 4);
      const std::uint8_t depth = data[8], color = data[9];
      const bool valid_depth = (color == 0 && (depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16)) ||
          (color == 2 && (depth == 8 || depth == 16)) || (color == 3 && (depth == 1 || depth == 2 || depth == 4 || depth == 8)) ||
          ((color == 4 || color == 6) && (depth == 8 || depth == 16));
      if (width == 0 || height == 0 || !valid_depth || data[10] != 0 || data[11] != 0 || data[12] > 1) {
        error = "PNG IHDR fields are invalid or unsupported"; return false;
      }
      saw_header = true;
    } else if (name == "IHDR") {
      error = "PNG contains more than one IHDR"; return false;
    } else if (name == "IDAT") {
      if (saw_end) { error = "PNG IDAT appears after IEND"; return false; }
      saw_data = true;
    } else if (name == "IEND") {
      if (length != 0 || !saw_data) { error = "PNG IEND is malformed or precedes image data"; return false; }
      saw_end = true;
      offset += 12;
      if (offset != source.size()) { error = "PNG has trailing bytes after IEND"; return false; }
      break;
    }
    offset += static_cast<std::size_t>(length) + 12;
  }
  if (!saw_header || !saw_data || !saw_end) { error = "PNG is missing IHDR, IDAT, or IEND"; return false; }
  append_u32(payload, 1); // image payload version
  append_u32(payload, width); append_u32(payload, height);
  append_u64(payload, source.size());
  payload.insert(payload.end(), source.begin(), source.end());
  stats.kind = Kind::PngImage;
  return true;
}

bool make_bundle(Kind kind, const std::vector<std::uint8_t>& payload,
                 std::vector<std::uint8_t>& bundle) {
  bundle.clear();
  if (payload.size() > std::numeric_limits<std::uint64_t>::max()) return false;
  bundle.insert(bundle.end(), bundle_magic.begin(), bundle_magic.end());
  append_u32(bundle, 1);
  append_u32(bundle, static_cast<std::uint32_t>(kind));
  append_u64(bundle, static_cast<std::uint64_t>(payload.size()));
  runtime::Sha256 sha;
  sha.update(std::as_bytes(std::span(bundle)));
  sha.update(std::as_bytes(std::span(payload)));
  const auto digest = sha.finish();
  bundle.insert(bundle.end(), digest.begin(), digest.end());
  bundle.insert(bundle.end(), payload.begin(), payload.end());
  return true;
}

} // namespace

bool cook_asset_file(const std::string& input_path, const std::string& output_path,
                     CookStats& stats, std::string& error) {
  error.clear();
  stats = {};
  if (input_path.empty() || output_path.empty()) {
    error = "input and output paths must be nonempty and different";
    return false;
  }
  std::error_code path_error;
  const auto absolute_input = std::filesystem::absolute(input_path, path_error).lexically_normal();
  if (path_error) { error = "cannot resolve input path"; return false; }
  const auto absolute_output = std::filesystem::absolute(output_path, path_error).lexically_normal();
  if (path_error) { error = "cannot resolve output path"; return false; }
  const bool same_existing_file = std::filesystem::equivalent(input_path, output_path, path_error);
  if (absolute_input == absolute_output || (!path_error && same_existing_file)) {
    error = "input and output paths must be different";
    return false;
  }
  std::vector<std::uint8_t> source, payload, bundle;
  if (!read_file(input_path, source, error)) return false;
  stats.source_bytes = source.size();
  std::string extension = std::filesystem::path(input_path).extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  bool parsed = false;
  if (extension == ".obj") parsed = cook_obj(source, payload, stats, error);
  else if (extension == ".wav") parsed = cook_wav(source, payload, stats, error);
  else if (extension == ".png") parsed = cook_png(source, payload, stats, error);
  else error = "supported input formats are OBJ, WAV, and PNG";
  if (!parsed) return false;
  if (!make_bundle(stats.kind, payload, bundle)) { error = "asset bundle size overflow"; return false; }
  if (!atomic_write(output_path, bundle)) { error = "failed to atomically write output bundle"; return false; }
  stats.output_bytes = bundle.size();
  return true;
}

bool verify_asset_bundle(const std::string& path, std::string& error) {
  error.clear();
  std::vector<std::uint8_t> bundle;
  if (!read_file(path, bundle, error)) return false;
  if (bundle.size() < bundle_header_size || !std::equal(bundle_magic.begin(), bundle_magic.end(), bundle.begin())) {
    error = "invalid AX bundle header"; return false;
  }
  const std::uint32_t version = read_u32_le(bundle.data() + 8);
  const std::uint32_t kind = read_u32_le(bundle.data() + 12);
  const std::uint64_t payload_size = read_u64_le(bundle.data() + 16);
  if (version != 1 || (kind < static_cast<std::uint32_t>(Kind::Mesh) || kind > static_cast<std::uint32_t>(Kind::PngImage)) ||
      payload_size != bundle.size() - bundle_header_size) {
    error = "unsupported AX bundle version, kind, or payload size"; return false;
  }
  runtime::Sha256 sha;
  sha.update(std::as_bytes(std::span(bundle.data(), std::size_t{24})));
  sha.update(std::as_bytes(std::span(bundle.data() + bundle_header_size, bundle.size() - bundle_header_size)));
  const auto digest = sha.finish();
  if (!std::equal(digest.begin(), digest.end(), bundle.begin() + 24)) {
    error = "AX bundle SHA-256 mismatch"; return false;
  }
  return true;
}

} // namespace ax::asset
