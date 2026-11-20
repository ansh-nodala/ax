#pragma once
#include "compiler/common/ax_common.hpp"
#include <map>
#include <vector>
namespace ax {
struct RegAlloc { std::map<std::string,int> gpr; std::map<std::string,int> fpr; std::map<std::string,int> spill; int num_spills=0; };
// Linear-scan: assign x0-x28 for GPR temps, v0-v31 for FP temps, spill to stack beyond.
inline RegAlloc linear_scan_alloc(const MirModule& mir){
  RegAlloc out; int gpr_next=0; int fpr_next=0;
  for(auto &f: mir.funcs) for(auto &in: f.insts){
    if(in.dst.empty()) continue;
    // temps tN are GPR pointers (AxVal*), others are FP if const
    if(in.op=="const"){ if(fpr_next<32) out.fpr[in.dst]=fpr_next++; else out.spill[in.dst]=(out.num_spills++*8); }
    else { if(gpr_next<29) out.gpr[in.dst]=gpr_next++; else out.spill[in.dst]=(out.num_spills++*8); }
  }
  return out;
}
} // namespace ax
