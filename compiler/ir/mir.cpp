#include "compiler/ir/mir.hpp"
#include <string>
namespace ax {
static void lower_expr(const Expr& e, std::vector<MirInst>& out, std::string& dst, int& tmp){
  std::string d = "t"+std::to_string(tmp++);
  if(e.kind=="num"){ MirInst mi; mi.op="const"; mi.dst=d; mi.imm=e.number; out.push_back(mi); dst=d; return; }
  if(e.kind=="str"){ MirInst mi; mi.op="const_str"; mi.dst=d; mi.str=e.str; out.push_back(mi); dst=d; return; }
  if(e.kind=="var"){ MirInst mi; mi.op="load"; mi.dst=d; mi.a=e.name; out.push_back(mi); dst=d; return; }
  if(e.kind=="list"){ MirInst mi; mi.op="tensor_list"; mi.dst=d; for(auto &a: e.args){ std::string ad; lower_expr(a, out, ad, tmp); mi.a += ad+","; } out.push_back(mi); dst=d; return; }
  if(e.kind=="call"){
    std::vector<std::string> argd;
    for(auto &a: e.args){ std::string ad; lower_expr(a, out, ad, tmp); argd.push_back(ad); }
    MirInst mi; mi.op="call"; mi.dst=d; mi.a=e.name; for(auto &x: argd) mi.b += x+","; out.push_back(mi); dst=d; return;
  }
  if(e.kind=="binary" && e.left && e.right){
    std::string ld, rd; lower_expr(*e.left, out, ld, tmp); lower_expr(*e.right, out, rd, tmp);
    MirInst mi; mi.op=e.op; mi.dst=d; mi.a=ld; mi.b=rd; out.push_back(mi); dst=d; return;
  }
  MirInst mi; mi.op="const"; mi.dst=d; mi.imm=0; out.push_back(mi); dst=d;
}
static void lower_stmts(const std::vector<Stmt>& stmts, std::vector<MirInst>& out, int& tmp){
  for(auto &s: stmts){
    if(s.kind=="assign"){ std::string d; lower_expr(s.expr, out, d, tmp); MirInst mi; mi.op="store"; mi.dst=s.name; mi.a=d; mi.type=s.type_ann; out.push_back(mi); }
    else if(s.kind=="expr"){ std::string d; lower_expr(s.expr, out, d, tmp); MirInst mi; mi.op="eval"; mi.dst="_"; mi.a=d; out.push_back(mi); }
    else if(s.kind=="for"){ MirInst mi; mi.op="for"; mi.dst=s.loop_var; std::string rd; lower_expr(s.range_expr, out, rd, tmp); mi.a=rd; out.push_back(mi); lower_stmts(s.body, out, tmp); MirInst me; me.op="endfor"; out.push_back(me); }
    else if(s.kind=="if"){ std::string cd; lower_expr(s.cond, out, cd, tmp); MirInst mi; mi.op="if"; mi.a=cd; out.push_back(mi); lower_stmts(s.body, out, tmp); MirInst me; me.op="else"; out.push_back(me); lower_stmts(s.else_body, out, tmp); MirInst mend; mend.op="endif"; out.push_back(mend); }
    else if(s.kind=="while"){ std::string cd; lower_expr(s.cond, out, cd, tmp); MirInst mi; mi.op="while"; mi.a=cd; out.push_back(mi); lower_stmts(s.body, out, tmp); MirInst me; me.op="endwhile"; out.push_back(me); }
    else if(s.kind=="return"){ std::string d; lower_expr(s.expr, out, d, tmp); MirInst mi; mi.op="return"; mi.a=d; out.push_back(mi); }
    else if(s.kind=="fn"){ /* nested fns handled at module level */ }
    else { if(s.expr.kind!=""){ std::string d; lower_expr(s.expr, out, d, tmp); } lower_stmts(s.body, out, tmp); lower_stmts(s.else_body, out, tmp); }
  }
}
MirModule MirBuilder::build(const Module& m){
  MirModule mm; for(auto &fn: m.fns){ if(fn.kind!="fn") continue; MirFunc mf; mf.name=fn.name; mf.decorators=fn.decorators; int tmp=0; lower_stmts(fn.body, mf.insts, tmp); mm.funcs.push_back(std::move(mf)); } return mm;
}
} // namespace ax
