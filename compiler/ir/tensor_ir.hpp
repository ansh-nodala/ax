#pragma once
#include "compiler/common/ax_common.hpp"
namespace ax {
struct TensorNode { std::string op; std::string name; std::vector<int> shape; std::vector<std::string> inputs; };
struct TensorGraph { std::vector<TensorNode> nodes; };
inline TensorGraph build_tensor_graph(const MirModule& mir){
  TensorGraph g;
  for(auto &f: mir.funcs) for(auto &in: f.insts){
    if(in.op=="call" && (in.a=="matmul"||in.a=="softmax"||in.a=="relu"||in.a=="layernorm")){
      TensorNode n; n.op=in.a; n.name=in.dst; n.inputs={in.b}; g.nodes.push_back(std::move(n));
    }
  }
  return g;
}
} // namespace ax
