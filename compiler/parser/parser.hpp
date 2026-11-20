#pragma once
#include "compiler/common/ax_common.hpp"
#include <vector>
namespace ax {
class Parser {
 public:
  Module parse(const std::vector<Token>& toks, const std::string& file);
 private:
  std::vector<Token> toks_; std::size_t pos_=0; std::string file_;
  const Token& peek(int off=0) const;
  const Token& cur() const { return peek(0); }
  bool at(TokKind k) const { return cur().kind==k; }
  bool eat(TokKind k);
  void skip_newlines();
  Token expect(TokKind k, const std::string& what);
  std::vector<Stmt> parse_block();
  std::vector<Stmt> parse_top();
  Stmt parse_stmt();
  Stmt parse_fn(const std::vector<std::string>& decorators);
  Stmt parse_struct_like(const std::string& kind);
  Stmt parse_load();
  Stmt parse_if();
  Stmt parse_for();
  Stmt parse_while();
  Stmt parse_return();
  Stmt parse_coro_stmt(const std::string& kind);
  Expr parse_expr();
  Expr parse_binary_rhs(int min_prec, Expr lhs);
  Expr parse_unary();
  Expr parse_postfix();
  Expr parse_primary();
  std::string parse_type_ann();
};
} // namespace ax
