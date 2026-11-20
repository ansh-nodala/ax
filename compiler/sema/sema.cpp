#include "compiler/sema/sema.hpp"
#include "compiler/sema/symbol_table.hpp"
#include "compiler/sema/type_inference.hpp"
#include "compiler/sema/shape_checker.hpp"
#include "compiler/sema/borrow_checker.hpp"
#include <set>
namespace ax {
static void check_stmts(std::vector<Stmt>& stmts, SymbolTable& sym, std::vector<Diag>& diags, std::set<std::string>& prelude);
static void check_stmt(Stmt& s, SymbolTable& sym, std::vector<Diag>& diags, std::set<std::string>& prelude){
  if(s.kind=="assign"){
    auto prior=sym.lookup(s.name);
    if(!s.has_initializer){ Diag d; d.message="binding declaration requires '=' and an initializer"; d.span=s.span; diags.push_back(d); return; }
    if(prior && prior->is_const){ Diag d; d.message="cannot reassign immutable binding '"+s.name+"'"; d.span=s.span; diags.push_back(d); }
    check_borrow_use(s.expr, sym, diags, s.span);
    check_expr_shapes(s.expr, sym, diags, s.span);
    Symbol symb; symb.type = s.type_ann.empty()? infer_expr(s.expr, sym).type : s.type_ann;
    auto inf = infer_expr(s.expr, sym);
    if(!s.type_ann.empty()){ auto sh=parse_shape_from_type(s.type_ann); if(!sh.empty()) symb.shape=sh; else symb.shape=inf.shape; }
    else symb.shape=inf.shape;
    symb.is_tensor = inf.is_tensor || symb.type.find("Tensor")!=std::string::npos;
    symb.is_const = s.is_const || (prior && prior->is_const);
    // shape vs literal check: if annotation shape mismatches literal length
    if(!s.type_ann.empty() && s.expr.kind=="call" && s.expr.name=="tensor"){
      auto sh=parse_shape_from_type(s.type_ann);
      if(sh.size()==1 && !s.expr.args.empty() && s.expr.args[0].kind=="list" && (int)s.expr.args[0].args.size()!=sh[0]){
        Diag d; d.message="tensor literal length "+std::to_string(s.expr.args[0].args.size())+" does not match annotation ["+std::to_string(sh[0])+"] for '"+s.name+"'"; d.span=s.span; diags.push_back(d);
      }
    }
    sym.declare(s.name, symb);
    return;
  }
  if(s.kind=="print" || s.kind=="expr"){
    check_borrow_use(s.expr, sym, diags, s.span); check_expr_shapes(s.expr, sym, diags, s.span); return;
  }
  if(s.kind=="for"){
    sym.push_scope(); Symbol lv; lv.type="i32"; sym.declare(s.loop_var, lv);
    check_borrow_use(s.range_expr, sym, diags, s.span);
    check_stmts(s.body, sym, diags, prelude); sym.pop_scope(); return;
  }
  if(s.kind=="if"){
    check_borrow_use(s.cond, sym, diags, s.span); check_expr_shapes(s.cond, sym, diags, s.span);
    sym.push_scope(); check_stmts(s.body, sym, diags, prelude); sym.pop_scope();
    sym.push_scope(); check_stmts(s.else_body, sym, diags, prelude); sym.pop_scope(); return;
  }
  if(s.kind=="while"){
    check_borrow_use(s.cond, sym, diags, s.span); sym.push_scope(); check_stmts(s.body, sym, diags, prelude); sym.pop_scope(); return;
  }
  if(s.kind=="return"){ check_borrow_use(s.expr, sym, diags, s.span); check_expr_shapes(s.expr, sym, diags, s.span); return; }
  if(s.kind=="fn"){ return; }
  // generic: recurse
  check_borrow_use(s.expr, sym, diags, s.span); check_stmts(s.body, sym, diags, prelude); check_stmts(s.else_body, sym, diags, prelude);
}
static void check_stmts(std::vector<Stmt>& stmts, SymbolTable& sym, std::vector<Diag>& diags, std::set<std::string>& prelude){
  for(auto &s: stmts) check_stmt(s, sym, diags, prelude);
}
Module Sema::check(Module m){
  std::vector<Diag> diags;
  std::set<std::string> prelude = {"print","println","input","assert","panic","len","range","sizeof","typeof","math","os","system","tensor","matmul","gemm","softmax","relu","layernorm","rmsnorm","rope","swiglu","geglu","sigmoid","tanh","gelu","silu","mse_loss","cross_entropy","adamw"};
  // prelude functions are always available: do not error on them
  for(auto &fn: m.fns){
    if(fn.kind!="fn") continue;
    SymbolTable sym; sym.push_scope();
    for(auto &p: fn.params){ Symbol s; s.type=p.second.empty()?"f32":p.second; s.shape=parse_shape_from_type(p.second); s.is_tensor=p.second.find("Tensor")!=std::string::npos; sym.declare(p.first, s); }
    check_stmts(fn.body, sym, diags, prelude);
    sym.pop_scope();
  }
  // also check top-level non-fn stmts (e.g. stray exprs) — they were parsed as fns? skip
  m.diags.insert(m.diags.end(), diags.begin(), diags.end());
  return m;
}
} // namespace ax
