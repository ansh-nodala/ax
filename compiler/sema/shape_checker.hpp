#pragma once
#include "compiler/sema/type_inference.hpp"
namespace ax {
inline void check_expr_shapes(const Expr& e, const SymbolTable& sym, std::vector<Diag>& diags, Span span){
  if(e.kind=="call" && e.name=="matmul" && e.args.size()==2){
    auto a=infer_expr(e.args[0], sym); auto b=infer_expr(e.args[1], sym);
    if(a.shape.size()==2 && b.shape.size()==2 && a.shape[1]!=b.shape[0]){
      Diag d; d.message="tensor shape mismatch in matmul: left is ["+std::to_string(a.shape[0])+" x "+std::to_string(a.shape[1])+"] but right is ["+std::to_string(b.shape[0])+" x "+std::to_string(b.shape[1])+"] — inner dimensions "+std::to_string(a.shape[1])+" != "+std::to_string(b.shape[0]); d.span=span; diags.push_back(d);
    }
  }
  for(auto &arg: e.args) check_expr_shapes(arg, sym, diags, span);
  if(e.left) check_expr_shapes(*e.left, sym, diags, span);
  if(e.right) check_expr_shapes(*e.right, sym, diags, span);
}
} // namespace ax
