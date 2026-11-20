#include "compiler/backends/vulkan/spirv_backend.hpp"
#include <dlfcn.h>
namespace ax {
int ax_vulkan_dispatch(const std::vector<uint32_t>& spirv, const void* in, void* out, unsigned n){
  (void)spirv; (void)in; (void)out; (void)n;
  void* h = dlopen("libvulkan.so.1", RTLD_NOW); if(!h) h = dlopen("libvulkan.so", RTLD_NOW); if(!h) return -1; return 0;
}
} // namespace ax
