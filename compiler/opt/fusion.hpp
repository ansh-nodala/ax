#pragma once
#include "compiler/common/ax_common.hpp"
namespace ax {
inline MirModule fuse_ops(MirModule m){
  // Fuse consecutive elementwise Add/Mul chains: Add(Mul(a,b),c) -> FusedAddMul
  for(auto &f: m.funcs){
    std::vector<MirInst> out;
    for(std::size_t i=0;i<f.insts.size();++i){
      if(i+1<f.insts.size() && f.insts[i].op=="*" && f.insts[i+1].op=="+"){
        MirInst fused; fused.op="fused_mul_add"; fused.dst=f.insts[i+1].dst; fused.a=f.insts[i].a; fused.b=f.insts[i].b; fused.str=f.insts[i+1].b; out.push_back(fused); i++; continue;
      }
      out.push_back(f.insts[i]);
    }
    f.insts=std::move(out);
  }
  return m;
}
} // namespace ax
