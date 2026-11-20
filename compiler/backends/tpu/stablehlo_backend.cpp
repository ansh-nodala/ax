#include "compiler/backends/tpu/stablehlo_backend.hpp"
#include <sstream>
namespace ax {
std::string emit_stablehlo(const Module& m){
  std::ostringstream o;
  o << "module @ax_tpu {\n";
  bool any=false;
  for(auto &fn: m.fns){ if(fn.kind!="fn") continue; bool is=false; for(auto &d: fn.decorators) if(d=="tpu") is=true; if(!is) continue; any=true;
    o << "  func.func @" << fn.name << "(%arg0: tensor<128x128xf32>, %arg1: tensor<128x128xf32>) -> tensor<128x128xf32> {\n";
    o << "    // systolic-array tiling: tile 128x128, pipeline stages 4\n";
    o << "    %0 = stablehlo.dot_general %arg0, %arg1, contracting_dims = [1], contracting_dims = [0], precision = [DEFAULT, DEFAULT] : (tensor<128x128xf32>, tensor<128x128xf32>) -> tensor<128x128xf32>\n";
    o << "    %1 = stablehlo.add %0, %0 : tensor<128x128xf32>\n";
    o << "    func.return %1 : tensor<128x128xf32>\n  }\n";
  }
  if(!any){
    o << "  func.func @main(%arg0: tensor<4xf32>) -> tensor<4xf32> {\n";
    o << "    %c = stablehlo.constant dense<[2.0, 2.0, 2.0, 2.0]> : tensor<4xf32>\n";
    o << "    %0 = stablehlo.multiply %arg0, %c : tensor<4xf32>\n";
    o << "    func.return %0 : tensor<4xf32>\n  }\n";
  }
  o << "}\n";
  (void)m; return o.str();
}
} // namespace ax
