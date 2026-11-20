#include "compiler/backends/tpu/pjrt_bridge.hpp"
#include <dlfcn.h>
namespace ax {
int ax_tpu_execute(const std::string& mlir, const void* in, void* out, unsigned n){
  (void)mlir; (void)in; (void)out; (void)n;
  void* h = dlopen("libtpu.so", RTLD_NOW); if(!h) return -1; return 0;
}
} // namespace ax
