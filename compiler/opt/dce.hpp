#pragma once
#include "compiler/common/ax_common.hpp"
#include <set>
namespace ax {
inline MirModule dce_pass(MirModule m){
  for(auto &f: m.funcs){
    std::set<std::string> used;
    for(auto &in: f.insts){ if(!in.a.empty()) used.insert(in.a); if(!in.b.empty()) used.insert(in.b); }
    std::vector<MirInst> out;
    for(auto &in: f.insts){ if(in.op=="const" && used.find(in.dst)==used.end() && in.dst.rfind("t",0)==0){ continue; } out.push_back(in); }
    f.insts=std::move(out);
  }
  return m;
}
} // namespace ax
