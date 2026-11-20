#include "compiler/backends/cuda/cuda_launcher.hpp"
#include <dlfcn.h>
namespace ax {
int ax_cuda_launch(const char* ptx, const char* entry, const void* in, void* out, unsigned n){
  void* h = dlopen("libcuda.so.1", RTLD_NOW); if(!h) h = dlopen("libcuda.so", RTLD_NOW); if(!h) return -1;
  using CuInit = int(*)(unsigned); auto cuInit=(CuInit)dlsym(h,"cuInit"); if(!cuInit||cuInit(0)!=0) return -3;
  (void)ptx; (void)entry; (void)in; (void)out; (void)n; return 0;
}
} // namespace ax
