#pragma once
#include "compiler/sema/symbol_table.hpp"
namespace ax {
inline void check_borrow_use(const Expr& e, SymbolTable& sym, std::vector<Diag>& diags, Span span){
  if(e.kind=="var"){
    auto s=sym.lookup(e.name);
    if(!s){ Diag d; d.message="use of undeclared variable '"+e.name+"'"; d.span=span; diags.push_back(d); }
    else if(s->moved){ Diag d; d.message="use of moved value '"+e.name+"' — value was moved and cannot be used again"; d.span=span; diags.push_back(d); }
  }
  for(auto &a: e.args) check_borrow_use(a, sym, diags, span);
  if(e.left) check_borrow_use(*e.left, sym, diags, span);
  if(e.right) check_borrow_use(*e.right, sym, diags, span);
}
} // namespace ax
