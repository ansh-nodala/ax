#include "compiler/backends/vulkan/gpu_kernel.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>
#include <unistd.h>

namespace ax {
namespace {

constexpr std::uint32_t kElementwiseWidth = 128;
constexpr std::uint32_t kReductionWidth = 128;
constexpr std::uint32_t kMatmulTile = 16;

bool has_decorator(const Stmt& function, const std::string& name) {
  for (const auto& decorator : function.decorators) {
    if (decorator == name) return true;
  }
  return false;
}

std::string compact_type(std::string type) {
  type.erase(std::remove_if(type.begin(), type.end(), [](unsigned char character) {
    return character == ' ' || character == '\t' || character == '\n';
  }), type.end());
  return type;
}

bool parse_tensor_shape(const std::string& source_type,
                        std::vector<std::uint32_t>& dimensions) {
  const std::string type = compact_type(source_type);
  constexpr char prefix[] = "Tensor[f32,";
  if (type.rfind(prefix, 0) != 0 || type.empty() || type.back() != ']') return false;
  const std::string shape = type.substr(sizeof(prefix) - 1,
                                        type.size() - (sizeof(prefix) - 1) - 1);
  std::size_t begin = 0;
  while (begin < shape.size()) {
    const std::size_t end = shape.find(',', begin);
    const std::string_view token(shape.data() + begin,
        (end == std::string::npos ? shape.size() : end) - begin);
    std::uint32_t value = 0;
    const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() || value == 0) {
      return false;
    }
    dimensions.push_back(value);
    if (end == std::string::npos) break;
    begin = end + 1;
  }
  return !dimensions.empty();
}

bool shape_elements(const std::vector<std::uint32_t>& shape,
                    std::uint32_t& count) {
  std::uint64_t product = 1;
  for (const std::uint32_t dimension : shape) {
    product *= dimension;
    if (product > std::numeric_limits<std::uint32_t>::max()) return false;
  }
  count = static_cast<std::uint32_t>(product);
  return count != 0;
}

std::string shell_quote(const std::string& value) {
  std::string result = "'";
  for (char character : value) {
    if (character == '\'') result += "'\\''";
    else result += character;
  }
  result += '\'';
  return result;
}

std::string float_literal(double value) {
  std::ostringstream literal;
  literal << std::setprecision(9) << value;
  std::string text = literal.str();
  if (text.find_first_of(".eE") == std::string::npos) text += ".0";
  return text + "f";
}

bool expression_to_glsl(const Expr& expression,
                        const std::map<std::string, std::size_t>& input_indices,
                        std::string& output, std::string& error) {
  if (expression.kind == "var") {
    const auto found = input_indices.find(expression.name);
    if (found == input_indices.end()) {
      error = "@gpu expression references unsupported name '" + expression.name + "'";
      return false;
    }
    output = "input" + std::to_string(found->second) + ".values[index]";
    return true;
  }
  if (expression.kind == "num") {
    if (!std::isfinite(expression.number) ||
        std::abs(expression.number) > static_cast<double>(std::numeric_limits<float>::max())) {
      error = "@gpu numeric literals must be finite and fit in f32";
      return false;
    }
    output = float_literal(expression.number);
    return true;
  }
  if (expression.kind == "binary" && expression.left && expression.right) {
    if (expression.op != "+" && expression.op != "-" &&
        expression.op != "*" && expression.op != "/") {
      error = "@gpu elementwise kernels support only +, -, *, and /";
      return false;
    }
    std::string left;
    std::string right;
    if (!expression_to_glsl(*expression.left, input_indices, left, error) ||
        !expression_to_glsl(*expression.right, input_indices, right, error)) {
      return false;
    }
    output = "(" + left + " " + expression.op + " " + right + ")";
    return true;
  }
  if (expression.kind == "call") {
    if ((expression.name == "relu" || expression.name == "sigmoid" ||
         expression.name == "silu" || expression.name == "gelu" ||
         expression.name == "tanh" || expression.name == "sqrt" ||
         expression.name == "exp" || expression.name == "log") &&
        expression.args.size() == 1) {
      std::string argument;
      if (!expression_to_glsl(expression.args[0], input_indices, argument, error)) return false;
      if (expression.name == "relu") output = "max(" + argument + ", 0.0f)";
      else if (expression.name == "sigmoid") output = "(1.0f / (1.0f + exp(-(" + argument + "))))";
      else if (expression.name == "silu") output = "(" + argument + " / (1.0f + exp(-(" + argument + "))))";
      else if (expression.name == "gelu") {
        output = "(0.5f * " + argument + " * (1.0f + tanh(0.7978845608f * (" + argument +
                 " + 0.044715f * " + argument + " * " + argument + " * " + argument + " ))))";
      } else {
        output = expression.name + "(" + argument + ")";
      }
      return true;
    }
    if ((expression.name == "swiglu" || expression.name == "geglu") &&
        expression.args.size() == 2) {
      std::string gate;
      std::string up;
      if (!expression_to_glsl(expression.args[0], input_indices, gate, error) ||
          !expression_to_glsl(expression.args[1], input_indices, up, error)) return false;
      if (expression.name == "swiglu") {
        output = "((" + gate + " / (1.0f + exp(-(" + gate + ")))) * " + up + ")";
      } else {
        output = "((0.5f * " + gate + " * (1.0f + tanh(0.7978845608f * (" + gate +
                 " + 0.044715f * " + gate + " * " + gate + " * " + gate + " )))) * " + up + ")";
      }
      return true;
    }
  }
  error = "@gpu supports elementwise arithmetic, relu/sigmoid/silu/gelu/tanh/sqrt/exp/log, "
          "swiglu, and geglu; reductions and matmul have dedicated GPU kernels";
  return false;
}

std::string input_declarations(std::uint32_t input_count) {
  std::ostringstream source;
  for (std::uint32_t index = 0; index < input_count; ++index) {
    source << "layout(set = 0, binding = " << index
           << ", std430) readonly buffer InputBuffer" << index <<
           " { float values[]; } input" << index << ";\n";
  }
  source << "layout(set = 0, binding = " << input_count
         << ", std430) writeonly buffer OutputBuffer { float values[]; } dst;\n"
         << "layout(push_constant) uniform Params { uint output_count; uint rows; "
            "uint columns; uint inner; } params;\n";
  return source.str();
}

std::string matmul_shader() {
  return "#version 450\n"
         "layout(local_size_x = 16, local_size_y = 16) in;\n" +
         input_declarations(2) +
         "shared float tile_a[16][16];\n"
         "shared float tile_b[16][16];\n"
         "void main() {\n"
         "  uint row = gl_WorkGroupID.y * 16u + gl_LocalInvocationID.y;\n"
         "  uint column = gl_WorkGroupID.x * 16u + gl_LocalInvocationID.x;\n"
         "  uint lane_x = gl_LocalInvocationID.x;\n"
         "  uint lane_y = gl_LocalInvocationID.y;\n"
         "  float sum = 0.0f;\n"
         "  for (uint base = 0u; base < params.inner; base += 16u) {\n"
         "    uint a_column = base + lane_x;\n"
         "    uint b_row = base + lane_y;\n"
         "    tile_a[lane_y][lane_x] = (row < params.rows && a_column < params.inner) ? input0.values[row * params.inner + a_column] : 0.0f;\n"
         "    tile_b[lane_y][lane_x] = (b_row < params.inner && column < params.columns) ? input1.values[b_row * params.columns + column] : 0.0f;\n"
         "    barrier();\n"
         "    for (uint k = 0u; k < 16u; ++k) sum = fma(tile_a[lane_y][k], tile_b[k][lane_x], sum);\n"
         "    barrier();\n"
         "  }\n"
         "  if (row < params.rows && column < params.columns) dst.values[row * params.columns + column] = sum;\n"
         "}\n";
}

std::string reduction_shader(VulkanKernelKind kind) {
  std::string body =
      "#version 450\n"
      "layout(local_size_x = 128) in;\n" + input_declarations(1) +
      "shared float scratch[128];\n"
      "void main() {\n"
      "  uint lane = gl_LocalInvocationID.x;\n"
      "  uint row = gl_WorkGroupID.x;\n"
      "  float accumulator = 0.0f;\n";
  if (kind == VulkanKernelKind::Softmax) {
    body +=
        "  float maximum_value = -3.402823466e+38f;\n"
        "  for (uint column = lane; column < params.columns; column += 128u) maximum_value = max(maximum_value, input0.values[row * params.columns + column]);\n"
        "  scratch[lane] = maximum_value;\n"
        "  barrier();\n"
        "  for (uint stride = 64u; stride > 0u; stride >>= 1u) { if (lane < stride) scratch[lane] = max(scratch[lane], scratch[lane + stride]); barrier(); }\n"
        "  maximum_value = scratch[0];\n"
        "  for (uint column = lane; column < params.columns; column += 128u) accumulator += exp(input0.values[row * params.columns + column] - maximum_value);\n"
        "  scratch[lane] = accumulator;\n"
        "  barrier();\n"
        "  for (uint stride = 64u; stride > 0u; stride >>= 1u) { if (lane < stride) scratch[lane] += scratch[lane + stride]; barrier(); }\n"
        "  float inverse_sum = 1.0f / scratch[0];\n"
        "  for (uint column = lane; column < params.columns; column += 128u) { uint index = row * params.columns + column; dst.values[index] = exp(input0.values[index] - maximum_value) * inverse_sum; }\n";
  } else if (kind == VulkanKernelKind::RmsNorm) {
    body +=
        "  for (uint column = lane; column < params.columns; column += 128u) { float value = input0.values[row * params.columns + column]; accumulator = fma(value, value, accumulator); }\n"
        "  scratch[lane] = accumulator;\n"
        "  barrier();\n"
        "  for (uint stride = 64u; stride > 0u; stride >>= 1u) { if (lane < stride) scratch[lane] += scratch[lane + stride]; barrier(); }\n"
        "  float inverse_rms = inversesqrt(scratch[0] / float(params.columns) + 1.0e-5f);\n"
        "  for (uint column = lane; column < params.columns; column += 128u) { uint index = row * params.columns + column; dst.values[index] = input0.values[index] * inverse_rms; }\n";
  } else {
    body +=
        "  for (uint column = lane; column < params.columns; column += 128u) accumulator += input0.values[row * params.columns + column];\n"
        "  scratch[lane] = accumulator;\n"
        "  barrier();\n"
        "  for (uint stride = 64u; stride > 0u; stride >>= 1u) { if (lane < stride) scratch[lane] += scratch[lane + stride]; barrier(); }\n"
        "  float mean_value = scratch[0] / float(params.columns);\n"
        "  accumulator = 0.0f;\n"
        "  for (uint column = lane; column < params.columns; column += 128u) { float delta = input0.values[row * params.columns + column] - mean_value; accumulator = fma(delta, delta, accumulator); }\n"
        "  scratch[lane] = accumulator;\n"
        "  barrier();\n"
        "  for (uint stride = 64u; stride > 0u; stride >>= 1u) { if (lane < stride) scratch[lane] += scratch[lane + stride]; barrier(); }\n"
        "  float inverse_stddev = inversesqrt(scratch[0] / float(params.columns) + 1.0e-5f);\n"
        "  for (uint column = lane; column < params.columns; column += 128u) { uint index = row * params.columns + column; dst.values[index] = (input0.values[index] - mean_value) * inverse_stddev; }\n";
  }
  body += "}\n";
  return body;
}

bool compile_glsl(const Stmt& function, const std::string& shader,
                  std::vector<std::uint32_t>& words, std::string& error) {
  static std::atomic<unsigned long long> next_id{0};
  const auto id = next_id.fetch_add(1, std::memory_order_relaxed);
  const auto prefix = std::filesystem::temp_directory_path() /
      ("ax_gpu_" + std::to_string(static_cast<long long>(getpid())) + "_" +
       std::to_string(id));
  const std::string source_path = prefix.string() + ".comp";
  const std::string binary_path = prefix.string() + ".spv";
  const std::string log_path = prefix.string() + ".log";
  std::ofstream source(source_path, std::ios::binary | std::ios::trunc);
  if (!source || !(source << shader)) {
    error = "could not write temporary Vulkan shader source";
    return false;
  }
  source.close();

  const char* configured_glslc = std::getenv("AX_GLSLC");
  const char* configured_glslang = std::getenv("AX_GLSLANG_VALIDATOR");
  std::string compiler;
  std::string compiler_args;
  if (configured_glslc != nullptr && configured_glslc[0] != '\0') {
    compiler = configured_glslc;
    compiler_args = " --target-env=vulkan1.1 -fshader-stage=compute ";
  } else if (std::system("command -v glslc >/dev/null 2>&1") == 0) {
    compiler = "glslc";
    compiler_args = " --target-env=vulkan1.1 -fshader-stage=compute ";
  } else if (configured_glslang != nullptr && configured_glslang[0] != '\0') {
    compiler = configured_glslang;
    compiler_args = " -V --target-env vulkan1.1 -S comp ";
  } else if (std::system("command -v glslangValidator >/dev/null 2>&1") == 0) {
    compiler = "glslangValidator";
    compiler_args = " -V --target-env vulkan1.1 -S comp ";
  } else {
    error = "@gpu requires glslc or glslangValidator at compile time; install Vulkan shader tools";
    std::error_code ignored;
    std::filesystem::remove(source_path, ignored);
    return false;
  }

  const std::string command = shell_quote(compiler) + compiler_args +
      shell_quote(source_path) + " -o " + shell_quote(binary_path) +
      " >" + shell_quote(log_path) + " 2>&1";
  const int result = std::system(command.c_str());
  std::error_code ignored;
  std::filesystem::remove(source_path, ignored);
  std::ifstream diagnostics(log_path, std::ios::binary);
  std::string compiler_output;
  if (diagnostics) {
    std::ostringstream contents;
    contents << diagnostics.rdbuf();
    compiler_output = contents.str();
    if (compiler_output.size() > 2048) compiler_output.resize(2048);
  }
  std::filesystem::remove(log_path, ignored);
  if (result != 0) {
    std::filesystem::remove(binary_path, ignored);
    error = "could not compile Vulkan shader for @gpu function '" + function.name + "'";
    if (!compiler_output.empty()) error += ":\n" + compiler_output;
    return false;
  }
  std::ifstream input(binary_path, std::ios::binary | std::ios::ate);
  if (!input) {
    error = "shader compiler did not produce a SPIR-V file";
    std::filesystem::remove(binary_path, ignored);
    return false;
  }
  const std::streamsize byte_count = input.tellg();
  if (byte_count < 20 || byte_count % 4 != 0) {
    error = "shader compiler produced a malformed SPIR-V file";
    std::filesystem::remove(binary_path, ignored);
    return false;
  }
  input.seekg(0);
  words.resize(static_cast<std::size_t>(byte_count) / sizeof(std::uint32_t));
  if (!input.read(reinterpret_cast<char*>(words.data()), byte_count) ||
      words.front() != 0x07230203u) {
    words.clear();
    error = "shader compiler output has invalid SPIR-V data";
    std::filesystem::remove(binary_path, ignored);
    return false;
  }
  std::filesystem::remove(binary_path, ignored);
  return true;
}

bool same_shape(const std::vector<std::uint32_t>& left,
                const std::vector<std::uint32_t>& right) {
  return left == right;
}

}  // namespace

bool compile_vulkan_kernel(const Stmt& function, VulkanKernel& kernel,
                           std::string& error) {
  kernel = {};
  if (function.kind != "fn" || function.name == "main" || !has_decorator(function, "gpu")) {
    error = "internal error: requested function is not marked @gpu";
    return false;
  }
  if (function.body.size() != 1 || function.body.front().kind != "return") {
    error = "@gpu function '" + function.name + "' must contain exactly one return statement";
    return false;
  }

  std::vector<std::vector<std::uint32_t>> input_shapes;
  std::map<std::string, std::size_t> input_indices;
  if (function.params.empty() || function.params.size() > 2) {
    error = "@gpu function '" + function.name + "' must take one or two tensor parameters";
    return false;
  }
  for (std::size_t index = 0; index < function.params.size(); ++index) {
    std::vector<std::uint32_t> shape;
    if (!parse_tensor_shape(function.params[index].second, shape)) {
      error = "@gpu parameter '" + function.params[index].first +
              "' must have a fixed-shape Tensor[f32, ...] type";
      return false;
    }
    input_indices.emplace(function.params[index].first, index);
    input_shapes.push_back(std::move(shape));
  }
  std::vector<std::uint32_t> output_shape;
  if (!parse_tensor_shape(function.ret_type, output_shape)) {
    error = "@gpu function '" + function.name + "' must return a fixed-shape Tensor[f32, ...]";
    return false;
  }

  kernel.config.input_count = static_cast<std::uint32_t>(function.params.size());
  kernel.config.local_x = kElementwiseWidth;
  kernel.config.local_y = 1;
  std::uint32_t output_count = 0;
  if (!shape_elements(output_shape, output_count)) {
    error = "@gpu output tensor size overflows 32-bit indexing";
    return false;
  }
  if (output_count > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
    error = "@gpu output tensor exceeds the AX runtime's maximum tensor length";
    return false;
  }
  for (const auto& shape : input_shapes) {
    std::uint32_t input_count = 0;
    if (!shape_elements(shape, input_count) ||
        input_count > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
      error = "@gpu input tensor exceeds the AX runtime's maximum tensor length";
      return false;
    }
  }
  kernel.config.output_count = output_count;

  const Expr& expression = function.body.front().expr;
  std::string shader;
  if (expression.kind == "call" && expression.name == "matmul" && expression.args.size() == 2) {
    if (input_shapes.size() != 2 || input_shapes[0].size() != 2 || input_shapes[1].size() != 2 ||
        output_shape.size() != 2 || input_shapes[0][1] != input_shapes[1][0] ||
        output_shape[0] != input_shapes[0][0] || output_shape[1] != input_shapes[1][1]) {
      error = "@gpu matmul expects A[M,K], B[K,N], and result Tensor[f32,M,N] with fixed dimensions";
      return false;
    }
    std::string left;
    std::string right;
    if (!expression_to_glsl(expression.args[0], input_indices, left, error) ||
        !expression_to_glsl(expression.args[1], input_indices, right, error) ||
        left.find("input0.values[index]") != 0 || right.find("input1.values[index]") != 0) {
      error = "@gpu matmul must be called as matmul(the_first_parameter, the_second_parameter)";
      return false;
    }
    kernel.config.kind = static_cast<std::uint32_t>(VulkanKernelKind::Matmul);
    kernel.config.rows = output_shape[0];
    kernel.config.columns = output_shape[1];
    kernel.config.inner = input_shapes[0][1];
    kernel.config.local_x = kMatmulTile;
    kernel.config.local_y = kMatmulTile;
    shader = matmul_shader();
  } else if (expression.kind == "call" &&
             (expression.name == "softmax" || expression.name == "rmsnorm" ||
              expression.name == "layernorm") && expression.args.size() == 1) {
    if (function.params.size() != 1 || !same_shape(input_shapes[0], output_shape)) {
      error = "@gpu reduction kernels take one input tensor and return the same shape";
      return false;
    }
    if (expression.args[0].kind != "var" ||
        expression.args[0].name != function.params[0].first) {
      error = "@gpu reduction call must use its tensor parameter directly";
      return false;
    }
    if (input_shapes[0].size() == 1) {
      kernel.config.rows = 1;
      kernel.config.columns = input_shapes[0][0];
    } else {
      kernel.config.rows = 1;
      kernel.config.columns = input_shapes[0].back();
      for (std::size_t index = 0; index + 1 < input_shapes[0].size(); ++index) {
        const std::uint64_t rows = static_cast<std::uint64_t>(kernel.config.rows) * input_shapes[0][index];
        if (rows > std::numeric_limits<std::uint32_t>::max()) {
          error = "@gpu reduction row count overflows 32-bit indexing";
          return false;
        }
        kernel.config.rows = static_cast<std::uint32_t>(rows);
      }
    }
    const VulkanKernelKind kind = expression.name == "softmax" ? VulkanKernelKind::Softmax :
        expression.name == "rmsnorm" ? VulkanKernelKind::RmsNorm : VulkanKernelKind::LayerNorm;
    kernel.config.kind = static_cast<std::uint32_t>(kind);
    kernel.config.local_x = kReductionWidth;
    shader = reduction_shader(kind);
  } else {
    if (!same_shape(input_shapes[0], output_shape)) {
      error = "@gpu elementwise kernels must return the same shape as their input";
      return false;
    }
    if (function.params.size() == 2 && !same_shape(input_shapes[0], input_shapes[1])) {
      error = "@gpu elementwise inputs must have identical fixed shapes";
      return false;
    }
    kernel.config.kind = static_cast<std::uint32_t>(VulkanKernelKind::Elementwise);
    kernel.config.rows = 1;
    kernel.config.columns = output_count;
    std::string translated;
    if (!expression_to_glsl(expression, input_indices, translated, error)) {
      error = "@gpu function '" + function.name + "': " + error;
      return false;
    }
    shader = "#version 450\n"
            "layout(local_size_x = 128) in;\n" + input_declarations(function.params.size()) +
            "void main() { uint index = gl_GlobalInvocationID.x; "
            "if (index < params.output_count) dst.values[index] = " + translated + "; }\n";
  }

  std::uint32_t expected_output = 0;
  if (!shape_elements(output_shape, expected_output) || expected_output != kernel.config.output_count) {
    error = "@gpu output tensor dimensions are invalid";
    return false;
  }
  if (kernel.config.kind == static_cast<std::uint32_t>(VulkanKernelKind::Matmul) &&
      static_cast<std::uint64_t>(kernel.config.rows) * kernel.config.columns != kernel.config.output_count) {
    error = "@gpu matmul result size overflows 32-bit indexing";
    return false;
  }
  if (kernel.config.kind != static_cast<std::uint32_t>(VulkanKernelKind::Matmul) &&
      kernel.config.kind != static_cast<std::uint32_t>(VulkanKernelKind::Softmax) &&
      kernel.config.kind != static_cast<std::uint32_t>(VulkanKernelKind::RmsNorm) &&
      kernel.config.kind != static_cast<std::uint32_t>(VulkanKernelKind::LayerNorm)) {
    // Build the generic source only after its typed expression has passed validation.
    std::string translated;
    if (!expression_to_glsl(expression, input_indices, translated, error)) return false;
    shader = "#version 450\n"
            "layout(local_size_x = 128) in;\n" + input_declarations(function.params.size()) +
            "void main() { uint index = gl_GlobalInvocationID.x; "
            "if (index < params.output_count) dst.values[index] = " + translated + "; }\n";
  }
  if (shader.empty()) {
    error = "could not lower @gpu kernel '" + function.name + "'";
    return false;
  }
  return compile_glsl(function, shader, kernel.spirv, error);
}

bool compile_vulkan_kernels(const Module& module, GpuKernelMap& kernels,
                            std::string& error) {
  kernels.clear();
  for (const auto& function : module.fns) {
    if (function.kind != "fn" || !has_decorator(function, "gpu")) continue;
    VulkanKernel kernel;
    if (!compile_vulkan_kernel(function, kernel, error)) return false;
    kernels.emplace(function.name, std::move(kernel));
  }
  return true;
}

}  // namespace ax
