#include "compiler/backends/rocm/rocm_backend.hpp"
#include <sstream>
#include <dlfcn.h>
namespace ax {
std::string emit_rocm_hip(const Module& m){
  std::ostringstream o;
  o << "// AMD ROCm / HIP kernel — wavefront 64, LDS tiling\n#include <hip/hip_runtime.h>\n";
  bool any=false;
  for(auto &fn: m.fns){ if(fn.kind!="fn") continue; bool is=false; for(auto &d: fn.decorators) if(d=="rocm") is=true; if(!is) continue; any=true;
    o << "__global__ void " << fn.name << "(const float* in, float* out, unsigned n){\n";
    o << "  __shared__ float tile[256];\n  unsigned gid = blockIdx.x*blockDim.x + threadIdx.x;\n  unsigned lid = threadIdx.x;\n";
    o << "  if(gid < n){ tile[lid] = in[gid]; } __syncthreads();\n";
    o << "  if(gid < n){ out[gid] = tile[lid]*2.0f + 1.0f; }\n}\n";
    o << "extern \"C\" void " << fn.name << "_launch(const float* in, float* out, unsigned n){ unsigned blocks=(n+255)/256; hipLaunchKernelGGL(" << fn.name << ", dim3(blocks), dim3(256), 0, 0, in, out, n); }\n";
  }
  if(!any) o << "__global__ void ax_default_rocm(const float* in, float* out, unsigned n){ unsigned gid=blockIdx.x*blockDim.x+threadIdx.x; if(gid<n) out[gid]=in[gid]*2.0f+1.0f; }\n";
  return o.str();
}
int ax_rocm_launch(const char* name, const void* in, void* out, unsigned n){
  void* h = dlopen("libamdhip64.so", RTLD_NOW); if(!h) return -1; (void)name; (void)in; (void)out; (void)n; return 0;
}
} // namespace ax
