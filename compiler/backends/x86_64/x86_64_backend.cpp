#include "compiler/backends/x86_64/x86_64_backend.hpp"

#include <iomanip>
#include <map>
#include <set>
#include <sstream>

namespace ax {
namespace {

std::string escape_asm_string(const std::string& value) {
  std::ostringstream escaped;
  for (unsigned char c : value) {
    switch (c) {
      case '\\': escaped << "\\\\"; break;
      case '"': escaped << "\\\""; break;
      case '\n': escaped << "\\n"; break;
      case '\r': escaped << "\\r"; break;
      case '\t': escaped << "\\t"; break;
      default:
        if (c < 32 || c >= 127) {
          escaped << "\\" << std::oct << std::setw(3) << std::setfill('0')
                  << static_cast<unsigned int>(c) << std::dec;
        } else {
          escaped << static_cast<char>(c);
        }
    }
  }
  return escaped.str();
}

struct X86Gen {
  std::ostringstream body;
  std::vector<std::pair<std::string, double>> double_constants;
  std::vector<std::pair<std::string, std::string>> string_constants;
  std::vector<std::pair<std::string, std::vector<double>>> array_constants;
  std::map<std::string, int> variable_offsets;
  int label_counter = 0;

  std::string fresh(const std::string& prefix) {
    return ".L" + prefix + "_" + std::to_string(label_counter++);
  }

  std::string double_label(double value) {
    for (const auto& entry : double_constants) {
      if (entry.second == value) return entry.first;
    }
    std::string label = ".Ld_" + std::to_string(double_constants.size());
    double_constants.push_back({label, value});
    return label;
  }

  std::string string_label(const std::string& value) {
    for (const auto& entry : string_constants) {
      if (entry.second == value) return entry.first;
    }
    std::string label = ".Ls_" + std::to_string(string_constants.size());
    string_constants.push_back({label, value});
    return label;
  }

  std::string array_label(const std::vector<double>& values) {
    std::string label = ".La_" + std::to_string(array_constants.size());
    array_constants.push_back({label, values});
    return label;
  }

  int variable_offset(const std::string& name) {
    auto it = variable_offsets.find(name);
    if (it != variable_offsets.end()) return it->second;
    int offset = -16 - static_cast<int>(variable_offsets.size()) * 8;
    variable_offsets[name] = offset;
    return offset;
  }

  void collect(const std::vector<Stmt>& statements) {
    for (const auto& statement : statements) {
      if (statement.kind == "assign") variable_offset(statement.name);
      if (statement.kind == "for") {
        variable_offset(statement.loop_var);
        variable_offset("__end_" + statement.loop_var);
      }
      collect(statement.body);
      collect(statement.else_body);
    }
  }

  void emit_expression(const Expr& expression,
                       const std::set<std::string>& integer_variables,
                       const std::string& current_function);
  void emit_statement(const Stmt& statement,
                      const std::set<std::string>& integer_variables,
                      const std::string& current_function, bool is_main);
  void emit_print(const Expr& expression,
                  const std::set<std::string>& integer_variables,
                  const std::string& current_function, bool empty_argument);
  void emit_builtin_call(const std::string& name, const std::vector<Expr>& args,
                         const std::set<std::string>& integer_variables,
                         const std::string& current_function);
};

void X86Gen::emit_print(const Expr& expression,
                        const std::set<std::string>& integer_variables,
                        const std::string& current_function, bool empty_argument) {
  if (empty_argument) {
    const std::string label = string_label("");
    body << "    leaq " << label << "(%rip), %rdi\n"
         << "    call ax_string@PLT\n";
  } else {
    emit_expression(expression, integer_variables, current_function);
    body << "    movq %rax, %rdi\n";
  }
  body << "    call ax_print@PLT\n";
}

void X86Gen::emit_builtin_call(const std::string& name, const std::vector<Expr>& args,
                              const std::set<std::string>& integer_variables,
                              const std::string& current_function) {
  static const std::map<std::string, std::string> unary_builtins = {
      {"len", "ax_len"}, {"matmul", "ax_matmul"}, {"gemm", "ax_gemm"},
      {"softmax", "ax_softmax"}, {"relu", "ax_relu"},
      {"layernorm", "ax_layernorm"}, {"rmsnorm", "ax_rmsnorm"},
      {"rope", "ax_rope"}, {"swiglu", "ax_swiglu"},
      {"geglu", "ax_geglu_self"},
      {"sigmoid", "ax_sigmoid"}, {"tanh", "ax_tanh"},
      {"gelu", "ax_gelu"}, {"silu", "ax_silu"},
      {"mse_loss", "ax_mse_loss"}, {"cross_entropy", "ax_cross_entropy"}};

  if (name == "tensor" && !args.empty()) {
    emit_expression(args[0], integer_variables, current_function);
    return;
  }

  if (name == "adamw" && args.size() == 3) {
    body << "    subq $16, %rsp\n";
    emit_expression(args[0], integer_variables, current_function);
    body << "    movq %rax, (%rsp)\n";
    emit_expression(args[1], integer_variables, current_function);
    body << "    movq %rax, 8(%rsp)\n";
    emit_expression(args[2], integer_variables, current_function);
    body << "    movq %rax, %rdi\n"
         << "    call ax_to_double@PLT\n"
         << "    movq (%rsp), %rdi\n"
         << "    movq 8(%rsp), %rsi\n"
         << "    addq $16, %rsp\n"
         << "    call ax_adamw@PLT\n";
    return;
  }

  auto builtin = unary_builtins.find(name);
  if (builtin != unary_builtins.end() && args.size() <= 2) {
    std::string target = builtin->second;
    if (name == "rope" && args.size() == 2) target = "ax_rope_offset";
    if (name == "swiglu" && args.size() == 2) target = "ax_swiglu2";
    if (name == "geglu" && args.size() == 2) target = "ax_geglu";
    if (args.empty()) {
      body << "    xorq %rdi, %rdi\n";
    } else if (args.size() == 1) {
      emit_expression(args[0], integer_variables, current_function);
      body << "    movq %rax, %rdi\n";
    } else {
      body << "    subq $16, %rsp\n";
      emit_expression(args[0], integer_variables, current_function);
      body << "    movq %rax, (%rsp)\n";
      emit_expression(args[1], integer_variables, current_function);
      body << "    movq %rax, %rsi\n"
           << "    movq (%rsp), %rdi\n"
           << "    addq $16, %rsp\n";
    }
    body << "    call " << target << "@PLT\n";
    return;
  }

  // Evaluate up to six pointer arguments in a 16-byte aligned temporary area.
  // This covers the System V x86-64 integer argument registers.
  if (args.size() > 6) {
    body << "    xorq %rax, %rax # unsupported call with more than six arguments\n";
    return;
  }
  const int frame_size = static_cast<int>(((args.size() * 8 + 15) / 16) * 16);
  if (frame_size) body << "    subq $" << frame_size << ", %rsp\n";
  for (std::size_t i = 0; i < args.size(); ++i) {
    emit_expression(args[i], integer_variables, current_function);
    body << "    movq %rax, " << (i * 8) << "(%rsp)\n";
  }
  static const char* argument_registers[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
  for (std::size_t i = 0; i < args.size(); ++i) {
    body << "    movq " << (i * 8) << "(%rsp), " << argument_registers[i] << "\n";
  }
  if (frame_size) body << "    addq $" << frame_size << ", %rsp\n";
  const std::string callee = (name == "rope" && args.size() == 4) ? "ax_rope_ex" : name;
  body << "    call " << callee << "@PLT\n";
}

void X86Gen::emit_expression(const Expr& expression,
                             const std::set<std::string>& integer_variables,
                             const std::string& current_function) {
  if (expression.kind == "num") {
    const std::string label = double_label(expression.number);
    body << "    movsd " << label << "(%rip), %xmm0\n"
         << "    call ax_scalar@PLT\n";
    return;
  }
  if (expression.kind == "str") {
    const std::string label = string_label(expression.str);
    body << "    leaq " << label << "(%rip), %rdi\n"
         << "    call ax_string@PLT\n";
    return;
  }
  if (expression.kind == "var") {
    auto it = variable_offsets.find(expression.name);
    const int offset = it == variable_offsets.end() ? -16 : it->second;
    body << "    movq " << offset << "(%rbp), %rax\n";
    if (integer_variables.count(expression.name)) {
      body << "    cvtsi2sdq %rax, %xmm0\n"
           << "    call ax_scalar@PLT\n";
    }
    return;
  }
  if (expression.kind == "list") {
    bool all_numbers = true;
    std::vector<double> values;
    for (const auto& item : expression.args) {
      if (item.kind != "num") {
        all_numbers = false;
        break;
      }
      values.push_back(item.number);
    }
    if (all_numbers) {
      const std::string label = array_label(values);
      body << "    leaq " << label << "(%rip), %rdi\n"
           << "    movl $" << values.size() << ", %esi\n"
           << "    call ax_tensor_vals@PLT\n";
      return;
    }
    const int frame_size = static_cast<int>(((expression.args.size() * 8 + 15) / 16) * 16);
    if (frame_size) body << "    subq $" << frame_size << ", %rsp\n";
    for (std::size_t i = 0; i < expression.args.size(); ++i) {
      emit_expression(expression.args[i], integer_variables, current_function);
      body << "    movq %rax, %rdi\n"
           << "    call ax_to_double@PLT\n"
           << "    movsd %xmm0, " << (i * 8) << "(%rsp)\n";
    }
    body << "    movq %rsp, %rdi\n"
         << "    movl $" << expression.args.size() << ", %esi\n"
         << "    call ax_tensor_vals@PLT\n";
    if (frame_size) body << "    addq $" << frame_size << ", %rsp\n";
    return;
  }
  if (expression.kind == "call") {
    if (expression.name == "print" || expression.name == "println") {
      if (!expression.args.empty()) {
        emit_print(expression.args[0], integer_variables, current_function, false);
      } else {
        Expr empty;
        emit_print(empty, integer_variables, current_function, true);
      }
      body << "    xorpd %xmm0, %xmm0\n"
           << "    call ax_scalar@PLT\n";
      return;
    }
    if (expression.name == "vec4" && expression.args.size() <= 4) {
      body << "    subq $32, %rsp\n";
      for (std::size_t i = 0; i < 4; ++i) {
        if (i < expression.args.size()) {
          emit_expression(expression.args[i], integer_variables, current_function);
        } else {
          body << "    xorpd %xmm0, %xmm0\n"
               << "    call ax_scalar@PLT\n";
        }
        body << "    movq %rax, " << (i * 8) << "(%rsp)\n";
      }
      body << "    movq 0(%rsp), %rdi\n"
           << "    movq 8(%rsp), %rsi\n"
           << "    movq 16(%rsp), %rdx\n"
           << "    movq 24(%rsp), %rcx\n"
           << "    addq $32, %rsp\n"
           << "    call ax_vec4@PLT\n";
      return;
    }
    emit_builtin_call(expression.name, expression.args, integer_variables, current_function);
    return;
  }
  if (expression.kind == "binary" && expression.left && expression.right) {
    emit_expression(*expression.left, integer_variables, current_function);
    body << "    subq $16, %rsp\n"
         << "    movq %rax, (%rsp)\n";
    emit_expression(*expression.right, integer_variables, current_function);
    body << "    movq %rax, %rsi\n"
         << "    movq (%rsp), %rdi\n"
         << "    addq $16, %rsp\n";
    std::string function = "ax_add";
    if (expression.op == "-") function = "ax_sub";
    else if (expression.op == "*") function = "ax_mul";
    else if (expression.op == "/") function = "ax_div";
    else if (expression.op == "%") function = "ax_mod";
    else if (expression.op == "==") function = "ax_eq";
    else if (expression.op == "!=") function = "ax_ne";
    else if (expression.op == "<") function = "ax_lt";
    else if (expression.op == ">") function = "ax_gt";
    else if (expression.op == "<=") function = "ax_le";
    else if (expression.op == ">=") function = "ax_ge";
    body << "    call " << function << "@PLT\n";
    return;
  }
  body << "    xorpd %xmm0, %xmm0\n"
       << "    call ax_scalar@PLT\n";
}

void X86Gen::emit_statement(const Stmt& statement,
                            const std::set<std::string>& integer_variables,
                            const std::string& current_function, bool is_main) {
  if (statement.kind == "assign") {
    emit_expression(statement.expr, integer_variables, current_function);
    body << "    movq %rax, " << variable_offset(statement.name) << "(%rbp)\n";
    return;
  }
  if (statement.kind == "expr") {
    if (statement.expr.kind == "call" &&
        (statement.expr.name == "print" || statement.expr.name == "println")) {
      if (statement.expr.args.empty()) {
        Expr empty;
        emit_print(empty, integer_variables, current_function, true);
      } else {
        emit_print(statement.expr.args[0], integer_variables, current_function, false);
      }
    } else {
      emit_expression(statement.expr, integer_variables, current_function);
    }
    return;
  }
  if (statement.kind == "for" || statement.kind == "parallel_for") {
    const int loop_offset = variable_offset(statement.loop_var);
    const int end_offset = variable_offset("__end_" + statement.loop_var);
    Expr start;
    start.kind = "num";
    start.number = 0;
    Expr end;
    end.kind = "num";
    end.number = 0;
    if (statement.range_expr.kind == "call" && statement.range_expr.name == "range") {
      if (statement.range_expr.args.size() == 1) {
        end = statement.range_expr.args[0];
      } else if (statement.range_expr.args.size() >= 2) {
        start = statement.range_expr.args[0];
        end = statement.range_expr.args[1];
      }
    } else {
      end = statement.range_expr;
    }
    emit_expression(start, integer_variables, current_function);
    body << "    movq %rax, %rdi\n"
         << "    call ax_to_double@PLT\n"
         << "    cvttsd2siq %xmm0, %rax\n"
         << "    movq %rax, " << loop_offset << "(%rbp)\n";
    emit_expression(end, integer_variables, current_function);
    body << "    movq %rax, %rdi\n"
         << "    call ax_to_double@PLT\n"
         << "    cvttsd2siq %xmm0, %rax\n"
         << "    movq %rax, " << end_offset << "(%rbp)\n";
    const std::string top = fresh("for");
    const std::string done = fresh("forend");
    body << top << ":\n"
         << "    movq " << loop_offset << "(%rbp), %rax\n"
         << "    cmpq " << end_offset << "(%rbp), %rax\n"
         << "    jge " << done << "\n";
    std::set<std::string> child_integer_variables = integer_variables;
    child_integer_variables.insert(statement.loop_var);
    for (const auto& child : statement.body) {
      emit_statement(child, child_integer_variables, current_function, is_main);
    }
    body << "    addq $1, " << loop_offset << "(%rbp)\n"
         << "    jmp " << top << "\n"
         << done << ":\n";
    return;
  }
  if (statement.kind == "if") {
    emit_expression(statement.cond, integer_variables, current_function);
    body << "    movq %rax, %rdi\n"
         << "    call ax_truthy@PLT\n"
         << "    testl %eax, %eax\n";
    const std::string otherwise = fresh("else");
    const std::string done = fresh("endif");
    body << "    je " << otherwise << "\n";
    for (const auto& child : statement.body) {
      emit_statement(child, integer_variables, current_function, is_main);
    }
    body << "    jmp " << done << "\n"
         << otherwise << ":\n";
    for (const auto& child : statement.else_body) {
      emit_statement(child, integer_variables, current_function, is_main);
    }
    body << done << ":\n";
    return;
  }
  if (statement.kind == "while") {
    const std::string top = fresh("while");
    const std::string done = fresh("whileend");
    body << top << ":\n";
    emit_expression(statement.cond, integer_variables, current_function);
    body << "    movq %rax, %rdi\n"
         << "    call ax_truthy@PLT\n"
         << "    testl %eax, %eax\n"
         << "    je " << done << "\n";
    for (const auto& child : statement.body) {
      emit_statement(child, integer_variables, current_function, is_main);
    }
    body << "    jmp " << top << "\n"
         << done << ":\n";
    return;
  }
  if (statement.kind == "return") {
    if (is_main) {
      if (!statement.expr.kind.empty()) {
        emit_expression(statement.expr, integer_variables, current_function);
        body << "    movq %rax, %rdi\n"
             << "    call ax_to_double@PLT\n"
             << "    cvttsd2sil %xmm0, %eax\n";
      } else {
        body << "    xorl %eax, %eax\n";
      }
    } else if (statement.expr.kind.empty()) {
      body << "    xorpd %xmm0, %xmm0\n"
           << "    call ax_scalar@PLT\n";
    } else {
      emit_expression(statement.expr, integer_variables, current_function);
    }
    body << "    leave\n"
         << "    ret\n";
    return;
  }
  if (!statement.body.empty() || !statement.else_body.empty()) {
    for (const auto& child : statement.body) {
      emit_statement(child, integer_variables, current_function, is_main);
    }
    for (const auto& child : statement.else_body) {
      emit_statement(child, integer_variables, current_function, is_main);
    }
  } else if (!statement.expr.kind.empty()) {
    emit_expression(statement.expr, integer_variables, current_function);
  }
}

} // namespace

std::string emit_x86_64_asm(const Module& module, const MirModule& mir,
                            const GpuKernelMap& gpu_kernels) {
  (void)mir;
  X86Gen generator;
  generator.string_label("");

  std::ostringstream text;
  for (const auto& function : module.fns) {
    if (function.kind != "fn") continue;
    generator.variable_offsets.clear();
    generator.collect(function.body);
    for (const auto& parameter : function.params) generator.variable_offset(parameter.first);

    const int local_bytes = static_cast<int>(generator.variable_offsets.size()) * 8 + 64;
    const int stack_size = (local_bytes + 15) / 16 * 16;
    const bool is_main = function.name == "main";
    text << "    .text\n"
         << "    .globl " << function.name << "\n"
         << "    .type " << function.name << ", @function\n"
         << function.name << ":\n"
         << "    pushq %rbp\n"
         << "    movq %rsp, %rbp\n"
         << "    subq $" << stack_size << ", %rsp\n";

    static const char* parameter_registers[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
    for (std::size_t i = 0; i < function.params.size() && i < 6; ++i) {
      text << "    movq " << parameter_registers[i] << ", "
           << generator.variable_offsets[function.params[i].first] << "(%rbp)\n";
    }

    const auto gpu_kernel = gpu_kernels.find(function.name);
    if (gpu_kernel != gpu_kernels.end()) {
      const std::size_t input_area = ((function.params.size() * 8 + 15) / 16) * 16;
      text << "    subq $" << input_area << ", %rsp\n";
      for (std::size_t index = 0; index < function.params.size(); ++index) {
        text << "    movq " << generator.variable_offsets[function.params[index].first]
             << "(%rbp), %rax\n"
             << "    movq %rax, " << index * sizeof(void*) << "(%rsp)\n";
      }
      text << "    leaq .Lax_gpu_kernel_" << function.name << "(%rip), %rdi\n"
           << "    movq $" << gpu_kernel->second.spirv.size() << ", %rsi\n"
           << "    movq %rsp, %rdx\n"
           << "    movq $" << function.params.size() << ", %rcx\n"
           << "    leaq .Lax_gpu_config_" << function.name << "(%rip), %r8\n"
           << "    call ax_vulkan_run_spirv@PLT\n"
           << "    addq $" << input_area << ", %rsp\n"
           << "    leave\n"
           << "    ret\n"
           << "    .size " << function.name << ", .-" << function.name << "\n";
      continue;
    }

    generator.body.str("");
    generator.body.clear();
    for (const auto& statement : function.body) {
      generator.emit_statement(statement, {}, function.name, is_main);
    }
    text << generator.body.str();

    bool has_top_level_return = false;
    for (const auto& statement : function.body) {
      if (statement.kind == "return") has_top_level_return = true;
    }
    if (!has_top_level_return) {
      if (is_main) {
        text << "    xorl %eax, %eax\n";
      } else {
        text << "    xorpd %xmm0, %xmm0\n"
             << "    call ax_scalar@PLT\n";
      }
      text << "    leave\n"
           << "    ret\n";
    }
    text << "    .size " << function.name << ", .-" << function.name << "\n";
  }

  std::ostringstream rodata;
  rodata << "    .section .rodata\n";
  for (const auto& entry : generator.double_constants) {
    rodata << entry.first << ":\n"
           << "    .double " << std::setprecision(17) << entry.second << "\n";
  }
  for (const auto& entry : generator.string_constants) {
    rodata << entry.first << ":\n"
           << "    .asciz \"" << escape_asm_string(entry.second) << "\"\n";
  }
  for (const auto& entry : generator.array_constants) {
    rodata << entry.first << ":\n";
    for (double value : entry.second) {
      rodata << "    .double " << std::setprecision(17) << value << "\n";
    }
  }
  for (const auto& entry : gpu_kernels) {
    const VulkanKernelConfig& config = entry.second.config;
    rodata << "    .p2align 2\n.Lax_gpu_config_" << entry.first << ":\n"
           << "    .long " << static_cast<std::uint32_t>(config.kind) << "\n"
           << "    .long " << config.input_count << "\n"
           << "    .long " << config.output_count << "\n"
           << "    .long " << config.rows << "\n"
           << "    .long " << config.columns << "\n"
           << "    .long " << config.inner << "\n"
           << "    .long " << config.local_x << "\n"
           << "    .long " << config.local_y << "\n"
           << "    .p2align 2\n.Lax_gpu_kernel_" << entry.first << ":\n";
    for (std::uint32_t word : entry.second.spirv) {
      rodata << "    .long 0x" << std::hex << std::setw(8) << std::setfill('0') << word
             << std::dec << std::setfill(' ') << "\n";
    }
  }
  rodata << "    .section .note.GNU-stack,\"\",@progbits\n";
  return text.str() + rodata.str();
}

} // namespace ax
