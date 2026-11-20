#include "include/ax_embed.h"

#include "compiler/backends/arm64/arm64_backend.hpp"
#include "compiler/backends/x86_64/x86_64_backend.hpp"
#include "compiler/common/diagnostics.hpp"
#include "compiler/ir/mir.hpp"
#include "compiler/lexer/lexer.hpp"
#include "compiler/opt/dce.hpp"
#include "compiler/opt/fusion.hpp"
#include "compiler/parser/parser.hpp"
#include "compiler/sema/sema.hpp"

#include <cstdlib>
#include <cstring>
#include <exception>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

bool assign_buffer(AxEmbedBuffer* output, const std::string& text) {
  if (output == nullptr) return false;
  output->data = static_cast<char*>(std::malloc(text.size() + 1));
  if (output->data == nullptr) { output->length = 0; return false; }
  std::memcpy(output->data, text.data(), text.size());
  output->data[text.size()] = '\0';
  output->length = text.size();
  return true;
}

std::string format_diagnostics(const std::vector<ax::Diag>& diagnostics,
                               const std::string& fallback_name = "<embedded>") {
  std::ostringstream text;
  for (const ax::Diag& diagnostic : diagnostics) {
    text << (diagnostic.span.file.empty() ? fallback_name : diagnostic.span.file)
         << ':' << diagnostic.span.line << ':' << diagnostic.span.col << ": "
         << (diagnostic.is_error ? "error: " : "warning: ") << diagnostic.message << '\n';
  }
  return text.str();
}

struct CheckedModule {
  ax::Module module;
  bool valid = false;
};

CheckedModule check_source(const char* source, const char* source_name) {
  std::vector<ax::Diag> diagnostics;
  const std::string name = source_name == nullptr ? "<embedded>" : source_name;
  ax::Lexer lexer;
  const auto tokens = lexer.tokenize(source, name, diagnostics);
  ax::Parser parser;
  ax::Module module = parser.parse(tokens, name);
  module.diags.insert(module.diags.end(), diagnostics.begin(), diagnostics.end());
  ax::Sema sema;
  module = sema.check(std::move(module));
  const bool valid = !ax::has_error(module.diags);
  return {std::move(module), valid};
}

ax::MirModule build_mir(ax::Module& module) {
  ax::MirBuilder builder;
  ax::MirModule mir = builder.build(module);
  mir = ax::fuse_ops(std::move(mir));
  return ax::dce_pass(std::move(mir));
}

std::string format_ir(const ax::MirModule& module) {
  std::ostringstream text;
  for (const auto& function : module.funcs) {
    text << "func " << function.name << ":\n";
    for (const auto& instruction : function.insts) {
      text << "  " << instruction.op << ' ' << instruction.dst << " = " << instruction.a
           << ' ' << instruction.b << ' ' << instruction.str << '\n';
    }
  }
  return text.str();
}

int fail_with_message(AxEmbedBuffer* output, const std::string& message) {
  if (output != nullptr) {
    output->data = nullptr;
    output->length = 0;
    (void)assign_buffer(output, message);
  }
  return 2;
}

} // namespace

extern "C" int ax_embed_check(const char* source, const char* source_name,
                              AxEmbedBuffer* diagnostics) {
  if (diagnostics != nullptr) { diagnostics->data = nullptr; diagnostics->length = 0; }
  if (source == nullptr) return fail_with_message(diagnostics, "source pointer is null\n");
  try {
    CheckedModule checked = check_source(source, source_name);
    const std::string messages = format_diagnostics(checked.module.diags,
        source_name == nullptr ? "<embedded>" : source_name);
    if (diagnostics != nullptr && !assign_buffer(diagnostics, messages)) return 2;
    return checked.valid ? 0 : 1;
  } catch (const std::exception& exception) {
    return fail_with_message(diagnostics, std::string("embedded compiler error: ") + exception.what() + '\n');
  } catch (...) {
    return fail_with_message(diagnostics, "embedded compiler error: unknown exception\n");
  }
}

extern "C" int ax_embed_emit_ir(const char* source, const char* source_name,
                                AxEmbedBuffer* output) {
  if (output != nullptr) { output->data = nullptr; output->length = 0; }
  if (source == nullptr || output == nullptr) return 2;
  try {
    CheckedModule checked = check_source(source, source_name);
    if (!checked.valid) return assign_buffer(output, format_diagnostics(checked.module.diags,
        source_name == nullptr ? "<embedded>" : source_name)) ? 1 : 2;
    const ax::MirModule mir = build_mir(checked.module);
    return assign_buffer(output, format_ir(mir)) ? 0 : 2;
  } catch (const std::exception& exception) {
    return fail_with_message(output, std::string("embedded compiler error: ") + exception.what() + '\n');
  } catch (...) {
    return fail_with_message(output, "embedded compiler error: unknown exception\n");
  }
}

extern "C" int ax_embed_emit_asm(const char* source, const char* source_name,
                                 const char* target, AxEmbedBuffer* output) {
  if (output != nullptr) { output->data = nullptr; output->length = 0; }
  if (source == nullptr || output == nullptr) return 2;
  try {
    CheckedModule checked = check_source(source, source_name);
    if (!checked.valid) return assign_buffer(output, format_diagnostics(checked.module.diags,
        source_name == nullptr ? "<embedded>" : source_name)) ? 1 : 2;
    const ax::MirModule mir = build_mir(checked.module);
    std::string architecture = target == nullptr ? "native" : target;
    if (architecture == "native") {
#if defined(__x86_64__) || defined(_M_X64)
      architecture = "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
      architecture = "arm64";
#else
      return fail_with_message(output, "native embedded assembly is unsupported for this host\n");
#endif
    }
    if (architecture == "x86_64") return assign_buffer(output, ax::emit_x86_64_asm(checked.module, mir)) ? 0 : 2;
    if (architecture == "arm64") return assign_buffer(output, ax::emit_arm64_asm(checked.module, mir)) ? 0 : 2;
    return fail_with_message(output, "target must be native, x86_64, or arm64\n");
  } catch (const std::exception& exception) {
    return fail_with_message(output, std::string("embedded compiler error: ") + exception.what() + '\n');
  } catch (...) {
    return fail_with_message(output, "embedded compiler error: unknown exception\n");
  }
}

extern "C" void ax_embed_buffer_release(AxEmbedBuffer* buffer) {
  if (buffer == nullptr) return;
  std::free(buffer->data);
  buffer->data = nullptr;
  buffer->length = 0;
}
