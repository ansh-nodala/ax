#pragma once
#include "compiler/common/ax_common.hpp"
#include <iostream>
namespace ax {
inline void print_diags(const std::vector<Diag>& diags) {
  for (auto &d : diags) std::cerr << d.span.file << ":" << d.span.line << ":" << d.span.col << ": " << (d.is_error?"error: ":"warning: ") << d.message << "\n";
}
inline bool has_error(const std::vector<Diag>& diags) { for (auto &d: diags) if (d.is_error) return true; return false; }
} // namespace ax
