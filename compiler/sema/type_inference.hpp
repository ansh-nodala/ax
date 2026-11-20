#pragma once
#include "compiler/sema/symbol_table.hpp"
namespace ax {
struct Inferred { std::string type; std::vector<int> shape; bool is_tensor=false; };
inline Inferred infer_expr(const Expr& e, const SymbolTable& sym){
  if(e.kind=="num") return {"f32",{},false};
  if(e.kind=="str") return {"str",{},false};
  if(e.kind=="var"){ auto s=sym.lookup(e.name); if(s) return {s->type, s->shape, s->is_tensor}; return {"unknown",{},false}; }
  if(e.kind=="list"){ if(!e.args.empty()){ auto first=infer_expr(e.args[0], sym); return {"Tensor[f32]", {(int)e.args.size()}, true}; } return {"Tensor[f32]",{0},true}; }
  if(e.kind=="call"){
    if(e.name=="tensor" && !e.args.empty() && e.args[0].kind=="list"){ int n=(int)e.args[0].args.size(); return {"Tensor[f32]",{n},true}; }
    if(e.name=="matmul" && e.args.size()==2){ auto a=infer_expr(e.args[0],sym); auto b=infer_expr(e.args[1],sym); if(a.shape.size()==2 && b.shape.size()==2) return {"Tensor[f32]",{a.shape[0], b.shape[1]},true}; return {"Tensor[f32]",{},true}; }
    if(e.name=="vec4") return {"vec4",{4},true};
    if(!e.args.empty()) return infer_expr(e.args[0], sym);
    return {"f32",{},false};
  }
  if(e.kind=="binary" && e.left && e.right){
    auto a=infer_expr(*e.left, sym); auto b=infer_expr(*e.right, sym);
    if(a.is_tensor||b.is_tensor) return {"Tensor[f32]", a.is_tensor?a.shape:b.shape, true};
    if(e.op=="=="||e.op=="!="||e.op=="<"||e.op==">"||e.op=="<="||e.op==">=") return {"bool",{},false};
    return {"f32",{},false};
  }
  return {"f32",{},false};
}
inline std::vector<int> parse_shape_from_type(const std::string& ty){
  // Tensor[f32, 2, 3] -> {2,3}; Tensor[f32, N] -> {} (symbolic)
  std::vector<int> out; auto lb=ty.find('['); auto rb=ty.find(']'); if(lb==std::string::npos||rb==std::string::npos) return out;
  std::string inner=ty.substr(lb+1, rb-lb-1);
  // split by comma, skip first (dtype)
  std::size_t start=0; int idx=0;
  while(start<inner.size()){ auto comma=inner.find(',', start); std::string part=inner.substr(start, comma==std::string::npos?std::string::npos:comma-start); // trim
    std::string t; for(char c: part) if(c!=' '&&c!='\t') t.push_back(c);
    if(idx>0){ try{ out.push_back(std::stoi(t)); }catch(...){} }
    if(comma==std::string::npos) break;
    start=comma+1;
    idx++;
  }
  return out;
}
} // namespace ax
