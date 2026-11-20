#include "compiler/lexer/token.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <poll.h>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr std::string_view kReset = "\x1b[0m";
constexpr std::string_view kDim = "\x1b[2m";
constexpr std::string_view kBold = "\x1b[1m";
constexpr std::string_view kControlColor = "\x1b[35m";
constexpr std::string_view kDeclarationColor = "\x1b[94m";
constexpr std::string_view kConcurrentColor = "\x1b[95m";
constexpr std::string_view kAcceleratorColor = "\x1b[33m";
constexpr std::string_view kTypeColor = "\x1b[96m";
constexpr std::string_view kFunctionColor = "\x1b[92m";
constexpr std::string_view kDecoratorColor = "\x1b[93m";
constexpr std::string_view kStringColor = "\x1b[32m";
constexpr std::string_view kNumberColor = "\x1b[38;5;208m";
constexpr std::string_view kCommentColor = "\x1b[90m";
constexpr std::string_view kOperatorColor = "\x1b[36m";

enum class KeyKind {
  Character,
  Up,
  Down,
  Left,
  Right,
  Home,
  End,
  Delete,
  PageUp,
  PageDown,
  Enter,
  Tab,
  Backspace,
  Escape,
  Save,
  Quit,
  Complete,
  NextCompletion,
  PreviousCompletion,
};

struct Key {
  KeyKind kind = KeyKind::Character;
  char value = 0;
};

class RawTerminal {
 public:
  RawTerminal() {
    if (!::isatty(STDIN_FILENO) || !::isatty(STDOUT_FILENO)) return;
    if (::tcgetattr(STDIN_FILENO, &original_) != 0) return;
    termios raw = original_;
    raw.c_iflag &= static_cast<tcflag_t>(~(BRKINT | ICRNL | INPCK | ISTRIP | IXON));
    raw.c_oflag &= static_cast<tcflag_t>(~OPOST);
    raw.c_cflag |= CS8;
    raw.c_lflag &= static_cast<tcflag_t>(~(ECHO | ICANON | IEXTEN | ISIG));
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (::tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) return;
    active_ = true;
    std::cout << "\x1b[?1049h\x1b[?25l";
    std::cout.flush();
  }

  RawTerminal(const RawTerminal&) = delete;
  RawTerminal& operator=(const RawTerminal&) = delete;

  ~RawTerminal() {
    if (!active_) return;
    std::cout << "\x1b[0m\x1b[?25h\x1b[?1049l";
    std::cout.flush();
    (void)::tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_);
  }

  bool active() const { return active_; }

 private:
  termios original_{};
  bool active_ = false;
};

int read_byte(int timeout_ms = -1) {
  if (timeout_ms >= 0) {
    pollfd descriptor{STDIN_FILENO, POLLIN, 0};
    const int ready = ::poll(&descriptor, 1, timeout_ms);
    if (ready <= 0) return -1;
  }
  unsigned char byte = 0;
  const ssize_t count = ::read(STDIN_FILENO, &byte, 1);
  return count == 1 ? byte : -1;
}

Key read_key() {
  const int first = read_byte();
  if (first < 0) return {KeyKind::Quit, 0};
  if (first == 17 || first == 24) return {KeyKind::Quit, 0};  // Ctrl-Q / Ctrl-X
  if (first == 19 || first == 15) return {KeyKind::Save, 0};  // Ctrl-S / Ctrl-O
  if (first == 0) return {KeyKind::Complete, 0};    // Ctrl-Space
  if (first == 14) return {KeyKind::NextCompletion, 0};  // Ctrl-N
  if (first == 16) return {KeyKind::PreviousCompletion, 0};  // Ctrl-P
  if (first == '\r' || first == '\n') return {KeyKind::Enter, 0};
  if (first == '\t') return {KeyKind::Tab, 0};
  if (first == 127 || first == 8) return {KeyKind::Backspace, 0};
  if (first != 27) {
    if (first >= 32 && first != 127) return {KeyKind::Character, static_cast<char>(first)};
    return {};
  }

  const int second = read_byte(35);
  if (second != '[' && second != 'O') return {KeyKind::Escape, 0};
  const int third = read_byte(35);
  switch (third) {
    case 'A': return {KeyKind::Up, 0};
    case 'B': return {KeyKind::Down, 0};
    case 'C': return {KeyKind::Right, 0};
    case 'D': return {KeyKind::Left, 0};
    case 'H': return {KeyKind::Home, 0};
    case 'F': return {KeyKind::End, 0};
    case '5': (void)read_byte(35); return {KeyKind::PageUp, 0};
    case '6': (void)read_byte(35); return {KeyKind::PageDown, 0};
    case '1': {
      (void)read_byte(35);
      const int final = read_byte(35);
      return final == 'H' ? Key{KeyKind::Home, 0} : Key{KeyKind::End, 0};
    }
    case '3':
      (void)read_byte(35);
      return {KeyKind::Delete, 0};
    default: return {KeyKind::Escape, 0};
  }
}

std::string trim(std::string_view value) {
  std::size_t begin = 0;
  while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
  std::size_t end = value.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
  return std::string(value.substr(begin, end - begin));
}

bool is_identifier_start(char value) {
  return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || value == '_';
}

bool is_identifier_continue(char value) {
  return is_identifier_start(value) || (value >= '0' && value <= '9');
}

std::size_t find_identifier_start(const std::string& line, std::size_t cursor) {
  std::size_t start = std::min(cursor, line.size());
  while (start > 0 && is_identifier_continue(line[start - 1])) --start;
  return start;
}

struct Completion {
  std::string label;
  std::string detail;
  std::string kind;
};

struct EditorModel {
  std::map<std::string, Completion> functions;
  std::map<std::string, std::string> variables;
  std::set<std::string> user_types;
};

std::string infer_value_type(const std::string& source, const EditorModel& model) {
  const std::string value = trim(source);
  if (value.empty()) return {};
  if (value.front() == '"' || value.front() == '\'') return "str";
  if (value == "true" || value == "false") return "bool";
  if (value.front() == '[' || value.rfind("tensor(", 0) == 0) return "Tensor[f32, inferred]";
  if (std::isdigit(static_cast<unsigned char>(value.front())) ||
      (value.front() == '-' && value.size() > 1 && std::isdigit(static_cast<unsigned char>(value[1])))) {
    return "f32";
  }

  std::size_t name_end = 0;
  while (name_end < value.size() && is_identifier_continue(value[name_end])) ++name_end;
  if (name_end == 0) return {};
  const std::string name = value.substr(0, name_end);
  const std::size_t open = value.find('(');
  if (open != std::string::npos && open >= name_end) {
    const auto function = model.functions.find(name);
    if (function != model.functions.end()) {
      const std::size_t arrow = function->second.detail.rfind(" -> ");
      if (arrow != std::string::npos) return function->second.detail.substr(arrow + 4);
    }
    if (name == "matmul" || name == "gemm" || name == "tensor" || name == "softmax" ||
        name == "relu" || name == "layernorm" || name == "rmsnorm" || name == "rope" ||
        name == "swiglu" || name == "geglu") return "Tensor[f32, inferred]";
    if (name == "len" || name == "range") return "i32";
    if (name == "print" || name == "println" || name == "assert") return "void";
    return {};
  }
  const auto variable = model.variables.find(name);
  if (variable != model.variables.end()) return variable->second;
  return {};
}

const std::vector<Completion>& builtin_completions() {
  static const std::vector<Completion> values = {
      {"fn", "declare a function", "keyword"},
      {"let", "immutable local binding", "keyword"},
      {"var", "mutable local binding", "keyword"},
      {"const", "compile-time constant", "keyword"},
      {"return", "return a value", "keyword"},
      {"if", "conditional branch", "keyword"},
      {"else", "alternate branch", "keyword"},
      {"elif", "additional conditional branch", "keyword"},
      {"for", "iterate over a range or collection", "keyword"},
      {"while", "loop while a condition is true", "keyword"},
      {"struct", "declare a structure", "keyword"},
      {"enum", "declare an enum", "keyword"},
      {"trait", "declare a trait", "keyword"},
      {"macro", "declare a macro", "keyword"},
      {"load", "load a module", "keyword"},
      {"soa", "structure of arrays layout", "keyword"},
      {"coro", "coroutine", "concurrency"},
      {"spawn", "start a coroutine", "concurrency"},
      {"await", "wait for a coroutine", "concurrency"},
      {"yield", "yield to the scheduler", "concurrency"},
      {"select", "select a ready channel operation", "concurrency"},
      {"chan", "create or use a channel", "concurrency"},
      {"distribute", "distributed execution", "accelerator"},
      {"mesh", "device mesh", "accelerator"},
      {"shard", "partition a tensor", "accelerator"},
      {"pipeline", "pipeline parallel work", "accelerator"},
      {"replicate", "replicate a tensor", "accelerator"},
      {"allreduce", "combine values across workers", "accelerator"},
      {"i8", "signed 8-bit integer", "type"},
      {"i16", "signed 16-bit integer", "type"},
      {"i32", "signed 32-bit integer", "type"},
      {"i64", "signed 64-bit integer", "type"},
      {"u8", "unsigned 8-bit integer", "type"},
      {"u16", "unsigned 16-bit integer", "type"},
      {"u32", "unsigned 32-bit integer", "type"},
      {"u64", "unsigned 64-bit integer", "type"},
      {"f16", "16-bit floating point", "type"},
      {"bf16", "bfloat16 floating point", "type"},
      {"f32", "32-bit floating point", "type"},
      {"f64", "64-bit floating point", "type"},
      {"bool", "boolean", "type"},
      {"str", "UTF-8 string", "type"},
      {"void", "no return value", "type"},
      {"Tensor", "Tensor[element_type, dimensions...]", "type"},
      {"List", "List[element_type]", "type"},
      {"Option", "Option[value_type]", "type"},
      {"Result", "Result[value_type, error_type]", "type"},
      {"print", "print(value: Any) -> void", "function"},
      {"println", "println(value: Any) -> void", "function"},
      {"input", "input(prompt: str) -> str", "function"},
      {"assert", "assert(condition: bool) -> void", "function"},
      {"panic", "panic(message: str) -> Never", "function"},
      {"len", "len(value: Any) -> i32", "function"},
      {"range", "range(start: i32, end: i32) -> Range", "function"},
      {"sizeof", "sizeof(type) -> i64", "function"},
      {"typeof", "typeof(value) -> str", "function"},
      {"tensor", "tensor(values: List[f32]) -> Tensor[f32, N]", "function"},
      {"matmul", "matmul(a: Tensor[f32, M, K], b: Tensor[f32, K, N])", "function"},
      {"gemm", "gemm(a, b) -> Tensor", "function"},
      {"softmax", "softmax(values: Tensor[f32, ...]) -> Tensor", "function"},
      {"relu", "relu(values: Tensor[f32, ...]) -> Tensor", "function"},
      {"layernorm", "layernorm(values: Tensor[f32, ...]) -> Tensor", "function"},
      {"rmsnorm", "rmsnorm(values: Tensor[f32, ...]) -> Tensor", "function"},
      {"rope", "rope(values: Tensor[f32, ...], position_offset: i32)", "function"},
      {"swiglu", "swiglu(gate: Tensor, up: Tensor) -> Tensor", "function"},
      {"geglu", "geglu(gate: Tensor, up: Tensor) -> Tensor", "function"},
      {"sigmoid", "sigmoid(value: f32) -> f32", "function"},
      {"tanh", "tanh(value: f32) -> f32", "function"},
      {"gelu", "gelu(value: f32) -> f32", "function"},
      {"silu", "silu(value: f32) -> f32", "function"},
      {"sqrt", "sqrt(value: f32) -> f32", "function"},
      {"exp", "exp(value: f32) -> f32", "function"},
      {"log", "log(value: f32) -> f32", "function"},
      {"mse_loss", "mse_loss(prediction, target) -> f32", "function"},
      {"cross_entropy", "cross_entropy(logits, labels) -> f32", "function"},
      {"adamw", "adamw(parameters, gradients, learning_rate)", "function"},
      {"@gpu", "Vulkan compute kernel", "decorator"},
      {"@cuda", "CUDA target annotation", "decorator"},
      {"@rocm", "ROCm target annotation", "decorator"},
      {"@tpu", "TPU target annotation", "decorator"},
      {"@autodiff", "automatic differentiation annotation", "decorator"},
      {"@recompute", "activation checkpointing annotation", "decorator"},
      {"@serve", "inference server annotation", "decorator"},
  };
  return values;
}

const std::set<std::string>& builtin_types() {
  static const std::set<std::string> values = {
      "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64",
      "f16", "bf16", "f32", "f64", "bool", "str", "void", "Tensor",
      "List", "Option", "Result", "vec2", "vec3", "vec4", "fixed32", "fixed64"};
  return values;
}

std::vector<std::string> split_parameters(std::string_view parameters) {
  std::vector<std::string> result;
  std::size_t start = 0;
  int depth = 0;
  for (std::size_t index = 0; index < parameters.size(); ++index) {
    if (parameters[index] == '[' || parameters[index] == '(') ++depth;
    else if (parameters[index] == ']' || parameters[index] == ')') --depth;
    else if (parameters[index] == ',' && depth == 0) {
      result.push_back(trim(parameters.substr(start, index - start)));
      start = index + 1;
    }
  }
  const std::string tail = trim(parameters.substr(start));
  if (!tail.empty()) result.push_back(tail);
  return result;
}

EditorModel analyze_source(const std::vector<std::string>& lines) {
  EditorModel model;
  for (const std::string& original : lines) {
    const std::string line = trim(original);
    const std::size_t fn_pos = line.find("fn ");
    const std::size_t function_pos = line.find("function ");
    const std::size_t declaration_pos = fn_pos != std::string::npos ? fn_pos : function_pos;
    if (declaration_pos != std::string::npos &&
        (declaration_pos == 0 || line[declaration_pos - 1] == '@' ||
         std::isspace(static_cast<unsigned char>(line[declaration_pos - 1])))) {
      const std::size_t name_start = declaration_pos + (fn_pos != std::string::npos ? 3 : 9);
      const std::size_t open = line.find('(', name_start);
      if (open != std::string::npos) {
        const std::string name = trim(std::string_view(line).substr(name_start, open - name_start));
        std::size_t close = open + 1;
        int depth = 1;
        while (close < line.size() && depth > 0) {
          if (line[close] == '(' || line[close] == '[') ++depth;
          else if (line[close] == ')' || line[close] == ']') --depth;
          ++close;
        }
        const std::string params = close > open + 1 ?
            line.substr(open + 1, close - open - 2) : std::string{};
        const std::size_t arrow = line.find("->", close);
        std::string return_type = "void";
        if (arrow != std::string::npos) {
          const std::size_t end = line.find(':', arrow + 2);
          return_type = trim(std::string_view(line).substr(arrow + 2,
              (end == std::string::npos ? line.size() : end) - arrow - 2));
          if (return_type.empty()) return_type = "unknown";
        }
        if (!name.empty()) {
          std::string signature = name + "(";
          bool first = true;
          for (const std::string& parameter : split_parameters(params)) {
            if (!first) signature += ", ";
            signature += parameter;
            first = false;
            const std::size_t colon = parameter.find(':');
            if (colon != std::string::npos) {
              const std::string parameter_name = trim(std::string_view(parameter).substr(0, colon));
              const std::string parameter_type = trim(std::string_view(parameter).substr(colon + 1));
              if (!parameter_name.empty()) model.variables[parameter_name] = parameter_type;
            }
          }
          signature += ") -> " + return_type;
          model.functions[name] = {name, signature, "function"};
        }
      }
      continue;
    }

    if (line.rfind("struct ", 0) == 0 || line.rfind("enum ", 0) == 0 ||
        line.rfind("trait ", 0) == 0) {
      const std::size_t space = line.find(' ');
      const std::size_t end = line.find_first_of(" :{", space + 1);
      const std::string name = line.substr(space + 1,
          (end == std::string::npos ? line.size() : end) - space - 1);
      if (!name.empty()) model.user_types.insert(name);
      continue;
    }

    std::string declaration = line;
    for (const std::string_view prefix : {"let ", "var ", "const "}) {
      if (declaration.rfind(prefix, 0) == 0) {
        declaration = trim(std::string_view(declaration).substr(prefix.size()));
        break;
      }
    }
    const std::size_t equals = declaration.find('=');
    const std::size_t colon = declaration.find(':');
    const std::size_t name_end = std::min(colon == std::string::npos ? declaration.size() : colon,
                                          equals == std::string::npos ? declaration.size() : equals);
    const std::string name = trim(std::string_view(declaration).substr(0, name_end));
    if (name.empty() || !is_identifier_start(name.front()) ||
        !std::all_of(name.begin(), name.end(), is_identifier_continue)) continue;

    if (colon != std::string::npos && (equals == std::string::npos || colon < equals)) {
      const std::size_t type_end = equals == std::string::npos ? declaration.size() : equals;
      const std::string type = trim(std::string_view(declaration).substr(colon + 1,
                                                       type_end - colon - 1));
      if (!type.empty()) model.variables[name] = type;
    } else if (equals != std::string::npos) {
      const std::string value = trim(std::string_view(declaration).substr(equals + 1));
      if (value.rfind("tensor(", 0) == 0 || value.rfind("tensor ([", 0) == 0) {
        model.variables[name] = "Tensor[f32, inferred]";
      } else if (!value.empty() && (value.front() == '"' || value.front() == '\'')) {
        model.variables[name] = "str";
      } else if (!value.empty() && std::isdigit(static_cast<unsigned char>(value.front()))) {
        model.variables[name] = (value.find('.') != std::string::npos ||
                                  value.find('e') != std::string::npos ||
                                  value.find('E') != std::string::npos) ? "f32" : "i32";
      } else {
        const std::size_t call_end = value.find('(');
        if (call_end != std::string::npos) {
          const std::string called = trim(std::string_view(value).substr(0, call_end));
          const auto function = model.functions.find(called);
          if (function != model.functions.end()) {
            const std::size_t arrow = function->second.detail.rfind(" -> ");
            if (arrow != std::string::npos) model.variables[name] = function->second.detail.substr(arrow + 4);
          }
        }
      }
    }
  }
  // Resolve inferred assignments after collecting the full file so forward
  // function calls and values declared later still provide useful type hints.
  for (const std::string& original : lines) {
    std::string declaration = trim(original);
    for (const std::string_view prefix : {"let ", "var ", "const "}) {
      if (declaration.rfind(prefix, 0) == 0) {
        declaration = trim(std::string_view(declaration).substr(prefix.size()));
        break;
      }
    }
    const std::size_t equals = declaration.find('=');
    const std::size_t colon = declaration.find(':');
    if (equals == std::string::npos || (colon != std::string::npos && colon < equals)) continue;
    const std::string name = trim(std::string_view(declaration).substr(0, equals));
    if (name.empty() || !std::all_of(name.begin(), name.end(), is_identifier_continue)) continue;
    const std::string inferred = infer_value_type(declaration.substr(equals + 1), model);
    if (!inferred.empty()) model.variables[name] = inferred;
  }
  return model;
}

std::string_view classify_word(std::string_view word, bool function_position,
                               const EditorModel& model) {
  if (word == "true" || word == "false" || word == "none" || word == "null") return kNumberColor;
  if (builtin_types().contains(std::string(word)) || model.user_types.contains(std::string(word))) return kTypeColor;
  if (function_position || model.functions.contains(std::string(word))) return kFunctionColor;

  if (word == "fn" || word == "function" || word == "struct" || word == "record" ||
      word == "enum" || word == "trait" || word == "macro" || word == "let" ||
      word == "var" || word == "const" || word == "soa") return kDeclarationColor;
  if (word == "coro" || word == "spawn" || word == "await" || word == "yield" ||
      word == "select" || word == "chan") return kConcurrentColor;
  if (word == "distribute" || word == "mesh" || word == "shard" || word == "pipeline" ||
      word == "replicate" || word == "allreduce" || word == "unleash") return kAcceleratorColor;
  ax::TokKind keyword;
  if (ax::is_keyword(std::string(word), keyword)) return kControlColor;
  static const std::set<std::string> functions = {
      "print", "println", "input", "assert", "panic", "len", "range", "sizeof", "typeof",
      "tensor", "matmul", "gemm", "softmax", "relu", "layernorm", "rmsnorm", "rope",
      "swiglu", "geglu", "sigmoid", "tanh", "gelu", "silu", "sqrt", "exp", "log",
      "mse_loss", "cross_entropy", "adamw"};
  if (functions.contains(std::string(word))) return kFunctionColor;
  return kReset;
}

void write_highlighted_line(const std::string& line, const EditorModel& model) {
  std::size_t index = 0;
  bool expect_function_name = false;
  while (index < line.size()) {
    const char current = line[index];
    if ((current == '#' || (current == '/' && index + 1 < line.size() && line[index + 1] == '/'))) {
      std::cout << kCommentColor << line.substr(index) << kReset;
      return;
    }
    if (current == '"' || current == '\'') {
      const char quote = current;
      const std::size_t start = index++;
      while (index < line.size()) {
        if (line[index] == '\\' && index + 1 < line.size()) {
          index += 2;
          continue;
        }
        if (line[index++] == quote) break;
      }
      std::cout << kStringColor << line.substr(start, index - start) << kReset;
      continue;
    }
    if (current == '@') {
      const std::size_t start = index++;
      while (index < line.size() && is_identifier_continue(line[index])) ++index;
      std::cout << kDecoratorColor << kBold << line.substr(start, index - start) << kReset;
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(current)) ||
        (current == '.' && index + 1 < line.size() && std::isdigit(static_cast<unsigned char>(line[index + 1])))) {
      const std::size_t start = index++;
      while (index < line.size() && (std::isalnum(static_cast<unsigned char>(line[index])) ||
             line[index] == '.' || line[index] == '_')) ++index;
      std::cout << kNumberColor << line.substr(start, index - start) << kReset;
      continue;
    }
    if (is_identifier_start(current)) {
      const std::size_t start = index++;
      while (index < line.size() && is_identifier_continue(line[index])) ++index;
      const std::string_view word(line.data() + start, index - start);
      std::size_t next = index;
      while (next < line.size() && std::isspace(static_cast<unsigned char>(line[next]))) ++next;
      const std::string_view color = classify_word(word, expect_function_name ||
          (next < line.size() && line[next] == '('), model);
      std::cout << color << line.substr(start, index - start) << kReset;
      expect_function_name = word == "fn" || word == "function";
      continue;
    }
    if (std::string_view("+-*/%=<>!|&^~").find(current) != std::string_view::npos) {
      std::cout << kOperatorColor << current << kReset;
    } else {
      std::cout << current;
    }
    if (!std::isspace(static_cast<unsigned char>(current))) expect_function_name = false;
    ++index;
  }
}

class Editor {
 public:
  explicit Editor(fs::path file) : path_(std::move(file)) {
    lines_.clear();
    std::ifstream input(path_, std::ios::binary);
    if (input) {
      std::string line;
      while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines_.push_back(std::move(line));
      }
      if (lines_.empty()) lines_.emplace_back();
      saved_ = true;
    } else {
      std::error_code error;
      if (fs::exists(path_, error)) status_message_ = "Cannot read existing file: " + path_.string();
      lines_.emplace_back();
    }
  }

  int run() {
    RawTerminal terminal;
    if (!terminal.active()) {
      std::cerr << "axedit needs an interactive terminal. Open a terminal and run: ax edit <file.ax>\n";
      return 2;
    }
    while (!quit_) {
      render();
      handle(read_key());
    }
    std::cout << "\x1b[0m\x1b[?25h\x1b[?1049l";
    std::cout.flush();
    return 0;
  }

 private:
  fs::path path_;
  std::vector<std::string> lines_;
  std::size_t row_ = 0;
  std::size_t column_ = 0;
  std::size_t scroll_row_ = 0;
  std::size_t scroll_column_ = 0;
  std::size_t completion_index_ = 0;
  bool saved_ = true;
  bool quit_ = false;
  bool force_completion_ = false;
  bool completion_suppressed_ = false;
  bool discard_armed_ = false;
  int screen_rows_ = 24;
  int screen_columns_ = 80;
  std::string status_message_;
  std::vector<Completion> suggestions_;
  std::size_t word_start_ = 0;
  std::string word_prefix_;
  EditorModel model_;

  void update_terminal_size() {
    winsize size{};
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_row > 0 && size.ws_col > 0) {
      screen_rows_ = size.ws_row;
      screen_columns_ = size.ws_col;
    }
  }

  void update_suggestions() {
    model_ = analyze_source(lines_);
    const std::string& line = lines_[row_];
    word_start_ = find_identifier_start(line, column_);
    word_prefix_ = line.substr(word_start_, column_ - word_start_);
    suggestions_.clear();
    if (completion_suppressed_ && !force_completion_) return;
    if (!force_completion_ && word_prefix_.empty()) return;

    std::map<std::string, Completion> unique;
    for (const Completion& item : builtin_completions()) {
      std::string label = item.label;
      if (!label.empty() && label.front() == '@') label.erase(label.begin());
      if (label.rfind(word_prefix_, 0) == 0 && label != word_prefix_) {
        unique.emplace(label, Completion{label, item.detail, item.kind});
      }
    }
    for (const auto& [name, item] : model_.functions) {
      if (name.rfind(word_prefix_, 0) == 0 && name != word_prefix_) unique.emplace(name, item);
    }
    for (const auto& [name, type] : model_.variables) {
      if (name.rfind(word_prefix_, 0) == 0 && name != word_prefix_) {
        unique.emplace(name, Completion{name, type, "variable"});
      }
    }
    for (const std::string& name : model_.user_types) {
      if (name.rfind(word_prefix_, 0) == 0 && name != word_prefix_) {
        unique.emplace(name, Completion{name, "user-defined type", "type"});
      }
    }
    for (const auto& [name, item] : unique) {
      (void)name;
      suggestions_.push_back(item);
    }
    if (suggestions_.empty()) completion_index_ = 0;
    else completion_index_ %= suggestions_.size();
  }

  std::string type_hint() const {
    const std::string& line = lines_[row_];
    std::size_t begin = find_identifier_start(line, column_);
    std::size_t end = std::min(column_, line.size());
    while (end < line.size() && is_identifier_continue(line[end])) ++end;
    const std::string word = line.substr(begin, end - begin);
    const auto variable = model_.variables.find(word);
    if (!word.empty() && variable != model_.variables.end()) return word + " : " + variable->second;
    const auto function = model_.functions.find(word);
    if (!word.empty() && function != model_.functions.end()) return function->second.detail;
    if (!word.empty() && builtin_types().contains(word)) return word + " — built-in AX type";
    if (!word.empty() && model_.user_types.contains(word)) return word + " — user-defined type";
    ax::TokKind keyword;
    if (!word.empty() && ax::is_keyword(word, keyword)) return word + " — AX language keyword";
    if (!word.empty() && std::isdigit(static_cast<unsigned char>(word.front()))) return "numeric literal — f32";
    for (const Completion& item : builtin_completions()) {
      std::string label = item.label;
      if (!label.empty() && label.front() == '@') label.erase(label.begin());
      if (!word.empty() && label == word && item.kind == "function") return item.detail;
    }
    const std::size_t colon = line.rfind(':', column_);
    const std::size_t equals = line.rfind('=', column_);
    const std::size_t arrow = line.rfind("->", column_);
    if ((colon != std::string::npos && (equals == std::string::npos || colon > equals)) ||
        arrow != std::string::npos) return "Type hint: i32 · f32 · bool · str · Tensor[f32, shape]";
    if (!word.empty()) return word + " — type not inferred yet";
    return "AX source · type hints follow declarations and function signatures";
  }

  void render() {
    update_terminal_size();
    update_suggestions();
    const bool show_suggestions = !suggestions_.empty();
    const int popup_rows = show_suggestions ?
        std::min({5, std::max(0, screen_rows_ - 4), static_cast<int>(suggestions_.size()) + 1}) : 0;
    const int content_rows = std::max(1, screen_rows_ - popup_rows - 2);
    const int gutter_width = std::max(4, static_cast<int>(std::to_string(lines_.size()).size()) + 1);
    const std::size_t text_width = static_cast<std::size_t>(std::max(1, screen_columns_ - gutter_width - 1));
    if (row_ < scroll_row_) scroll_row_ = row_;
    if (row_ >= scroll_row_ + static_cast<std::size_t>(content_rows)) {
      scroll_row_ = row_ - static_cast<std::size_t>(content_rows) + 1;
    }
    if (column_ < scroll_column_) {
      scroll_column_ = column_ > text_width ? column_ - text_width + 1 : 0;
    }
    if (column_ >= scroll_column_ + text_width) scroll_column_ = column_ - text_width + 1;

    std::cout << "\x1b[?25l\x1b[H\x1b[2J";
    for (int screen_line = 0; screen_line < content_rows; ++screen_line) {
      const std::size_t line_number = scroll_row_ + static_cast<std::size_t>(screen_line);
      if (line_number < lines_.size()) {
        std::ostringstream gutter;
        gutter << "\x1b[90m" << std::string(gutter_width - std::to_string(line_number + 1).size(), ' ')
               << (line_number + 1) << " " << kReset;
        std::cout << gutter.str();
        write_highlighted_line(lines_[line_number].substr(scroll_column_, text_width), model_);
      } else {
        std::cout << "\x1b[90m~\x1b[0m";
      }
      std::cout << "\x1b[K\r\n";
    }

    if (show_suggestions) {
      std::cout << "\x1b[30;46m  Suggestions  ·  Tab/Enter accept  ·  ↑/↓ choose  \x1b[0m\x1b[K\r\n";
      const int visible = popup_rows - 1;
      for (int index = 0; index < visible; ++index) {
        const std::size_t candidate = static_cast<std::size_t>(index);
        if (candidate >= suggestions_.size()) {
          std::cout << "\x1b[K\r\n";
          continue;
        }
        const Completion& item = suggestions_[candidate];
        if (candidate == completion_index_) std::cout << "\x1b[30;43m > ";
        else std::cout << "\x1b[90m   ";
        std::string detail = item.kind + "  " + item.detail;
        const std::size_t available = static_cast<std::size_t>(std::max(
            0, screen_columns_ - static_cast<int>(item.label.size()) - 7));
        if (detail.size() > available) detail.resize(available);
        std::cout << item.label << "  " << kDim << detail
                  << kReset << "\x1b[K\r\n";
      }
    }

    std::ostringstream status;
    status << (saved_ ? " AX " : " AX * ") << path_.string() << "  Ln " << row_ + 1
           << ", Col " << column_ + 1 << "  │  " << type_hint();
    if (!status_message_.empty()) status << "  │  " << status_message_;
    std::string status_text = status.str();
    if (static_cast<int>(status_text.size()) > screen_columns_) {
      status_text.resize(static_cast<std::size_t>(screen_columns_));
    }
    std::cout << "\x1b[30;47m" << status_text << "\x1b[K\x1b[0m\r\n";
    std::cout << "\x1b[7m ^S/^O Save   ^Q/^X Quit   Tab Complete   ^Space All   ^N/P Select   Esc Close \x1b[0m\x1b[K";

    const std::size_t screen_y = row_ - scroll_row_ + 1;
    const std::size_t visible_column = std::min(column_ - scroll_column_, text_width);
    const std::size_t screen_x = visible_column + static_cast<std::size_t>(gutter_width) + 1;
    std::cout << "\x1b[?25h\x1b[" << screen_y << ";" << std::min(screen_x, static_cast<std::size_t>(screen_columns_)) << "H";
    std::cout.flush();
  }

  void mark_changed() {
    saved_ = false;
    discard_armed_ = false;
    status_message_.clear();
  }

  void insert_text(const std::string& text) {
    completion_suppressed_ = false;
    lines_[row_].insert(column_, text);
    column_ += text.size();
    mark_changed();
  }

  void accept_completion() {
    if (suggestions_.empty()) return;
    const Completion selected = suggestions_[completion_index_];
    std::string& line = lines_[row_];
    line.replace(word_start_, column_ - word_start_, selected.label);
    column_ = word_start_ + selected.label.size();
    force_completion_ = false;
    completion_suppressed_ = true;
    suggestions_.clear();
    mark_changed();
  }

  void insert_newline() {
    const std::string current = lines_[row_];
    const std::string left = current.substr(0, column_);
    std::string right = current.substr(column_);
    std::size_t indent = 0;
    while (indent < current.size() && (current[indent] == ' ' || current[indent] == '\t')) ++indent;
    std::string indentation = current.substr(0, indent);
    if (!left.empty() && left.back() == ':') indentation += "    ";
    if (right.empty()) right = indentation;
    lines_[row_] = left;
    lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(row_ + 1), std::move(right));
    ++row_;
    column_ = indentation.size();
    if (lines_[row_].size() < column_) lines_[row_].insert(0, indentation);
    mark_changed();
  }

  void backspace() {
    if (column_ > 0) {
      lines_[row_].erase(column_ - 1, 1);
      --column_;
      mark_changed();
    } else if (row_ > 0) {
      const std::size_t previous_length = lines_[row_ - 1].size();
      lines_[row_ - 1] += lines_[row_];
      lines_.erase(lines_.begin() + static_cast<std::ptrdiff_t>(row_));
      --row_;
      column_ = previous_length;
      mark_changed();
    }
  }

  void delete_forward() {
    if (column_ < lines_[row_].size()) {
      lines_[row_].erase(column_, 1);
      mark_changed();
    } else if (row_ + 1 < lines_.size()) {
      lines_[row_] += lines_[row_ + 1];
      lines_.erase(lines_.begin() + static_cast<std::ptrdiff_t>(row_ + 1));
      mark_changed();
    }
  }

  void save() {
    if (path_.empty()) {
      status_message_ = "Choose a filename to save this buffer";
      return;
    }
    std::ofstream output(path_, std::ios::binary | std::ios::trunc);
    if (!output) {
      status_message_ = "Could not save " + path_.string();
      return;
    }
    for (std::size_t index = 0; index < lines_.size(); ++index) {
      output << lines_[index];
      if (index + 1 < lines_.size() || !lines_[index].empty()) output << '\n';
    }
    if (!output) {
      status_message_ = "Write failed for " + path_.string();
      return;
    }
    saved_ = true;
    discard_armed_ = false;
    status_message_ = "Saved " + path_.string();
  }

  void handle(Key key) {
    status_message_.clear();
    if (key.kind != KeyKind::Save && key.kind != KeyKind::Quit &&
        key.kind != KeyKind::NextCompletion && key.kind != KeyKind::PreviousCompletion) {
      completion_suppressed_ = false;
    }
    const std::size_t page_step = static_cast<std::size_t>(std::max(1, screen_rows_ - 4));
    if (key.kind != KeyKind::NextCompletion && key.kind != KeyKind::PreviousCompletion &&
        key.kind != KeyKind::Up && key.kind != KeyKind::Down && key.kind != KeyKind::Complete) {
      force_completion_ = false;
    }
    switch (key.kind) {
      case KeyKind::Character:
        insert_text(std::string(1, key.value));
        break;
      case KeyKind::Up:
        if (!suggestions_.empty()) completion_index_ = (completion_index_ + suggestions_.size() - 1) % suggestions_.size();
        else if (row_ > 0) { --row_; column_ = std::min(column_, lines_[row_].size()); }
        break;
      case KeyKind::Down:
        if (!suggestions_.empty()) completion_index_ = (completion_index_ + 1) % suggestions_.size();
        else if (row_ + 1 < lines_.size()) { ++row_; column_ = std::min(column_, lines_[row_].size()); }
        break;
      case KeyKind::Left:
        if (column_ > 0) --column_;
        else if (row_ > 0) { --row_; column_ = lines_[row_].size(); }
        break;
      case KeyKind::Right:
        if (column_ < lines_[row_].size()) ++column_;
        else if (row_ + 1 < lines_.size()) { ++row_; column_ = 0; }
        break;
      case KeyKind::Home: column_ = 0; break;
      case KeyKind::End: column_ = lines_[row_].size(); break;
      case KeyKind::PageUp:
        row_ = row_ > page_step ? row_ - page_step : 0;
        column_ = std::min(column_, lines_[row_].size());
        break;
      case KeyKind::PageDown:
        row_ = std::min(lines_.size() - 1, row_ + page_step);
        column_ = std::min(column_, lines_[row_].size());
        break;
      case KeyKind::Delete: delete_forward(); break;
      case KeyKind::Backspace: backspace(); break;
      case KeyKind::Enter:
        if (!suggestions_.empty()) accept_completion();
        else insert_newline();
        break;
      case KeyKind::Tab:
        if (!suggestions_.empty()) accept_completion();
        else insert_text("    ");
        break;
      case KeyKind::Save: save(); break;
      case KeyKind::Quit:
        if (!saved_ && !discard_armed_) {
          discard_armed_ = true;
          status_message_ = "Unsaved edits — press Ctrl-Q again to discard";
        } else {
          quit_ = true;
        }
        break;
      case KeyKind::Complete:
        force_completion_ = true;
        update_suggestions();
        if (suggestions_.empty()) status_message_ = "No completion matches at this cursor";
        break;
      case KeyKind::NextCompletion:
        if (!suggestions_.empty()) completion_index_ = (completion_index_ + 1) % suggestions_.size();
        break;
      case KeyKind::PreviousCompletion:
        if (!suggestions_.empty()) completion_index_ = (completion_index_ + suggestions_.size() - 1) % suggestions_.size();
        break;
      case KeyKind::Escape:
        force_completion_ = false;
        suggestions_.clear();
        break;
    }
  }
};

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && (std::string_view(argv[1]) == "--help" || std::string_view(argv[1]) == "-h")) {
    std::cout << "AX editor — native terminal editor for .ax source\n"
              << "Usage: axedit [file.ax]\n"
              << "Keys: Ctrl-S/Ctrl-O save, Ctrl-Q/Ctrl-X quit, Tab complete, Ctrl-Space all completions\n";
    return 0;
  }
  if (argc > 2) {
    std::cerr << "Usage: axedit [file.ax]\n";
    return 2;
  }
  return Editor(argc == 2 ? fs::path(argv[1]) : fs::path{}).run();
}
