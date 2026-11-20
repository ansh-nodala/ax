#include "runtime/core/model_io.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <string_view>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <variant>

namespace ax::ai {
namespace {

struct JsonValue {
  using Object = std::map<std::string, JsonValue>;
  using Array = std::vector<JsonValue>;
  std::variant<std::nullptr_t, bool, std::uint64_t, std::string, Array, Object> value;
};

class JsonParser {
 public:
  explicit JsonParser(std::string_view input) : input_(input) {}
  JsonValue parse() {
    JsonValue result = parse_value();
    skip_space();
    if (position_ != input_.size()) fail("trailing data");
    return result;
  }

 private:
  std::string_view input_;
  std::size_t position_ = 0;

  [[noreturn]] void fail(const char* message) const {
    throw std::runtime_error(std::string("invalid SafeTensors JSON: ") + message);
  }
  void skip_space() {
    while (position_ < input_.size() &&
           (input_[position_] == ' ' || input_[position_] == '\n' ||
            input_[position_] == '\r' || input_[position_] == '\t')) ++position_;
  }
  bool take(char c) {
    skip_space();
    if (position_ < input_.size() && input_[position_] == c) {
      ++position_;
      return true;
    }
    return false;
  }
  std::string parse_string() {
    skip_space();
    if (position_ >= input_.size() || input_[position_++] != '"') fail("expected string");
    std::string result;
    while (position_ < input_.size()) {
      const char c = input_[position_++];
      if (c == '"') return result;
      if (c != '\\') {
        result.push_back(c);
        continue;
      }
      if (position_ >= input_.size()) fail("incomplete escape");
      const char escape = input_[position_++];
      switch (escape) {
        case '"': result.push_back('"'); break;
        case '\\': result.push_back('\\'); break;
        case '/': result.push_back('/'); break;
        case 'b': result.push_back('\b'); break;
        case 'f': result.push_back('\f'); break;
        case 'n': result.push_back('\n'); break;
        case 'r': result.push_back('\r'); break;
        case 't': result.push_back('\t'); break;
        case 'u': {
          if (position_ + 4 > input_.size()) fail("incomplete unicode escape");
          unsigned codepoint = 0;
          for (int i = 0; i < 4; ++i) {
            const char digit = input_[position_++];
            codepoint <<= 4;
            if (digit >= '0' && digit <= '9') codepoint |= static_cast<unsigned>(digit - '0');
            else if (digit >= 'a' && digit <= 'f') codepoint |= static_cast<unsigned>(digit - 'a' + 10);
            else if (digit >= 'A' && digit <= 'F') codepoint |= static_cast<unsigned>(digit - 'A' + 10);
            else fail("invalid unicode escape");
          }
          if (codepoint <= 0x7f) result.push_back(static_cast<char>(codepoint));
          else if (codepoint <= 0x7ff) {
            result.push_back(static_cast<char>(0xc0u | (codepoint >> 6)));
            result.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
          } else {
            result.push_back(static_cast<char>(0xe0u | (codepoint >> 12)));
            result.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3fu)));
            result.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
          }
          break;
        }
        default: fail("unknown escape");
      }
    }
    fail("unterminated string");
  }
  JsonValue parse_number() {
    skip_space();
    const std::size_t begin = position_;
    if (position_ < input_.size() && input_[position_] == '-') fail("negative integer is not valid in tensor metadata");
    while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') ++position_;
    if (begin == position_) fail("expected unsigned integer");
    std::uint64_t value = 0;
    for (std::size_t i = begin; i < position_; ++i) {
      const unsigned digit = static_cast<unsigned>(input_[i] - '0');
      if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) fail("integer overflow");
      value = value * 10 + digit;
    }
    return JsonValue{value};
  }
  JsonValue parse_value() {
    skip_space();
    if (position_ >= input_.size()) fail("unexpected end");
    if (input_[position_] == '"') return JsonValue{parse_string()};
    if (input_[position_] == '{') {
      ++position_;
      JsonValue::Object object;
      if (take('}')) return JsonValue{std::move(object)};
      do {
        const std::string key = parse_string();
        if (!take(':')) fail("expected colon");
        if (!object.emplace(key, parse_value()).second) fail("duplicate object key");
      } while (take(','));
      if (!take('}')) fail("expected object close");
      return JsonValue{std::move(object)};
    }
    if (input_[position_] == '[') {
      ++position_;
      JsonValue::Array array;
      if (take(']')) return JsonValue{std::move(array)};
      do { array.push_back(parse_value()); } while (take(','));
      if (!take(']')) fail("expected array close");
      return JsonValue{std::move(array)};
    }
    if (input_.substr(position_, 4) == "null") {
      position_ += 4;
      return JsonValue{nullptr};
    }
    if (input_.substr(position_, 4) == "true") {
      position_ += 4;
      return JsonValue{true};
    }
    if (input_.substr(position_, 5) == "false") {
      position_ += 5;
      return JsonValue{false};
    }
    if (input_[position_] == '-' || (input_[position_] >= '0' && input_[position_] <= '9')) {
      return parse_number();
    }
    fail("unsupported JSON value");
  }
};

std::string escape_json(const std::string& text) {
  std::string result;
  for (unsigned char c : text) {
    switch (c) {
      case '"': result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\b': result += "\\b"; break;
      case '\f': result += "\\f"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default:
        if (c < 0x20) {
          static constexpr char hex[] = "0123456789abcdef";
          result += "\\u00";
          result.push_back(hex[c >> 4]);
          result.push_back(hex[c & 0x0f]);
        } else result.push_back(static_cast<char>(c));
    }
  }
  return result;
}

const JsonValue& object_field(const JsonValue::Object& object, const char* name) {
  const auto it = object.find(name);
  if (it == object.end()) throw std::runtime_error(std::string("SafeTensors field is missing: ") + name);
  return it->second;
}

std::string json_string(const JsonValue& value) {
  const auto* result = std::get_if<std::string>(&value.value);
  if (!result) throw std::runtime_error("SafeTensors expected a string");
  return *result;
}

std::vector<std::uint64_t> json_integer_array(const JsonValue& value) {
  const auto* array = std::get_if<JsonValue::Array>(&value.value);
  if (!array) throw std::runtime_error("SafeTensors expected an integer array");
  std::vector<std::uint64_t> result;
  result.reserve(array->size());
  for (const JsonValue& item : *array) {
    const auto* number = std::get_if<std::uint64_t>(&item.value);
    if (!number) throw std::runtime_error("SafeTensors expected an unsigned integer");
    result.push_back(*number);
  }
  return result;
}

std::size_t dtype_width(const std::string& dtype) {
  if (dtype == "F64" || dtype == "I64" || dtype == "U64") return 8;
  if (dtype == "F32" || dtype == "I32" || dtype == "U32") return 4;
  if (dtype == "F16" || dtype == "BF16" || dtype == "I16" || dtype == "U16") return 2;
  if (dtype == "I8" || dtype == "U8" || dtype == "BOOL" ||
      dtype == "F8_E4M3" || dtype == "F8_E5M2") return 1;
  throw std::runtime_error("unsupported SafeTensors dtype: " + dtype);
}

std::uint64_t element_count(const std::vector<std::uint64_t>& shape) {
  std::uint64_t count = 1;
  for (std::uint64_t dimension : shape) {
    if (dimension != 0 && count > std::numeric_limits<std::uint64_t>::max() / dimension) {
      throw std::runtime_error("tensor shape size overflow");
    }
    count *= dimension;
  }
  return count;
}

std::uint64_t tensor_byte_count(const std::vector<std::uint64_t>& shape,
                                const std::string& dtype) {
  const std::uint64_t elements = element_count(shape);
  const std::size_t width = dtype_width(dtype);
  if (elements > std::numeric_limits<std::uint64_t>::max() / width) {
    throw std::runtime_error("tensor byte size overflow");
  }
  return elements * width;
}

void append_u64_le(std::ofstream& out, std::uint64_t value) {
  for (unsigned i = 0; i < 8; ++i) out.put(static_cast<char>((value >> (8 * i)) & 0xffu));
}

std::uint64_t read_u64_le(const std::uint8_t* bytes) {
  std::uint64_t value = 0;
  for (unsigned i = 0; i < 8; ++i) value |= static_cast<std::uint64_t>(bytes[i]) << (8 * i);
  return value;
}

} // namespace

void write_safetensors(const std::string& path,
                       const std::map<std::string, TensorBlob>& tensors) {
  std::string header = "{";
  std::uint64_t data_offset = 0;
  bool first = true;
  for (const auto& [name, tensor] : tensors) {
    if (name == "__metadata__") throw std::invalid_argument("reserved SafeTensors tensor name");
    const std::uint64_t expected = tensor_byte_count(tensor.shape, tensor.dtype);
    if (expected != tensor.bytes.size()) throw std::invalid_argument("tensor byte count does not match dtype and shape");
    if (tensor.bytes.size() > std::numeric_limits<std::uint64_t>::max() - data_offset) {
      throw std::invalid_argument("SafeTensors data section size overflow");
    }
    if (!first) header += ',';
    first = false;
    header += "\"" + escape_json(name) + "\":{";
    header += "\"dtype\":\"" + escape_json(tensor.dtype) + "\",\"shape\":[";
    for (std::size_t i = 0; i < tensor.shape.size(); ++i) {
      if (i) header += ',';
      header += std::to_string(tensor.shape[i]);
    }
    header += "],\"data_offsets\":[" + std::to_string(data_offset) + ',' +
              std::to_string(data_offset + tensor.bytes.size()) + "]}";
    data_offset += tensor.bytes.size();
  }
  header += '}';
  while (header.size() % 8 != 0) header.push_back(' ');
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) throw std::runtime_error("cannot create SafeTensors file: " + path);
  append_u64_le(out, header.size());
  out.write(header.data(), static_cast<std::streamsize>(header.size()));
  for (const auto& [name, tensor] : tensors) {
    (void)name;
    if (!tensor.bytes.empty()) {
      out.write(reinterpret_cast<const char*>(tensor.bytes.data()),
                static_cast<std::streamsize>(tensor.bytes.size()));
    }
  }
  if (!out) throw std::runtime_error("failed while writing SafeTensors file: " + path);
}

MappedSafeTensors::~MappedSafeTensors() { close(); }

MappedSafeTensors::MappedSafeTensors(MappedSafeTensors&& other) noexcept
    : data_(other.data_), size_(other.size_), fd_(other.fd_), tensors_(std::move(other.tensors_)) {
  other.data_ = nullptr;
  other.size_ = 0;
  other.fd_ = -1;
}

MappedSafeTensors& MappedSafeTensors::operator=(MappedSafeTensors&& other) noexcept {
  if (this == &other) return *this;
  close();
  data_ = other.data_;
  size_ = other.size_;
  fd_ = other.fd_;
  tensors_ = std::move(other.tensors_);
  other.data_ = nullptr;
  other.size_ = 0;
  other.fd_ = -1;
  return *this;
}

void MappedSafeTensors::close() noexcept {
  tensors_.clear();
  if (data_) munmap(const_cast<std::uint8_t*>(data_), size_);
  if (fd_ >= 0) ::close(fd_);
  data_ = nullptr;
  size_ = 0;
  fd_ = -1;
}

MappedSafeTensors MappedSafeTensors::open(const std::string& path) {
  MappedSafeTensors file;
  file.fd_ = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (file.fd_ < 0) throw std::runtime_error("cannot open SafeTensors file: " + std::string(std::strerror(errno)));
  struct stat status{};
  if (fstat(file.fd_, &status) != 0 || status.st_size < 8) {
    throw std::runtime_error("SafeTensors file is too small or unreadable");
  }
  const auto file_size = static_cast<std::uintmax_t>(status.st_size);
  if (file_size > std::numeric_limits<std::size_t>::max()) {
    throw std::runtime_error("SafeTensors file is too large for this process");
  }
  file.size_ = static_cast<std::size_t>(file_size);
  void* mapped = mmap(nullptr, file.size_, PROT_READ, MAP_PRIVATE, file.fd_, 0);
  if (mapped == MAP_FAILED) {
    file.data_ = nullptr;
    throw std::runtime_error("cannot mmap SafeTensors file: " + std::string(std::strerror(errno)));
  }
  file.data_ = static_cast<const std::uint8_t*>(mapped);
  const std::uint64_t header_length = read_u64_le(file.data_);
  if (header_length > file.size_ - 8) throw std::runtime_error("SafeTensors header extends past end of file");
  const std::string_view header(reinterpret_cast<const char*>(file.data_ + 8),
                                static_cast<std::size_t>(header_length));
  const JsonValue root = JsonParser(header).parse();
  const auto* object = std::get_if<JsonValue::Object>(&root.value);
  if (!object) throw std::runtime_error("SafeTensors header must be a JSON object");
  const std::size_t data_start = 8 + static_cast<std::size_t>(header_length);
  std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges;
  for (const auto& [name, value] : *object) {
    if (name == "__metadata__") continue;
    const auto* fields = std::get_if<JsonValue::Object>(&value.value);
    if (!fields) throw std::runtime_error("SafeTensors tensor entry must be an object");
    TensorView tensor;
    tensor.dtype = json_string(object_field(*fields, "dtype"));
    tensor.shape = json_integer_array(object_field(*fields, "shape"));
    const auto offsets = json_integer_array(object_field(*fields, "data_offsets"));
    if (offsets.size() != 2 || offsets[1] < offsets[0] || offsets[1] > file.size_ - data_start) {
      throw std::runtime_error("invalid SafeTensors data offsets");
    }
    const std::uint64_t expected = tensor_byte_count(tensor.shape, tensor.dtype);
    if (offsets[1] - offsets[0] != expected) throw std::runtime_error("SafeTensors tensor byte length mismatch");
    ranges.emplace_back(offsets[0], offsets[1]);
    tensor.bytes = std::span<const std::uint8_t>(file.data_ + data_start + offsets[0],
                                                static_cast<std::size_t>(offsets[1] - offsets[0]));
    file.tensors_.emplace(name, std::move(tensor));
  }
  std::sort(ranges.begin(), ranges.end());
  std::uint64_t expected_offset = 0;
  for (const auto& [begin, end] : ranges) {
    if (begin != expected_offset) throw std::runtime_error("SafeTensors data ranges overlap or leave gaps");
    expected_offset = end;
  }
  if (expected_offset != file.size_ - data_start) {
    throw std::runtime_error("SafeTensors data section contains unreferenced bytes");
  }
  return file;
}

const TensorView& MappedSafeTensors::tensor(const std::string& name) const {
  const auto it = tensors_.find(name);
  if (it == tensors_.end()) throw std::out_of_range("SafeTensors tensor not found: " + name);
  return it->second;
}

bool MappedSafeTensors::contains(const std::string& name) const {
  return tensors_.contains(name);
}

} // namespace ax::ai
