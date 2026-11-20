#pragma once
#include "compiler/common/ax_common.hpp"
#include <string>
#include <vector>
namespace ax {
class Lexer {
 public:
  std::vector<Token> tokenize(const std::string& src, const std::string& file, std::vector<Diag>& diags);
};
} // namespace ax
