#include "compiler/backends/hal/plugin_loader.hpp"
#include <dlfcn.h>
namespace ax {
void* load_plugin(const std::string& path){ return dlopen(path.c_str(), RTLD_NOW); }
void* plugin_symbol(void* h, const std::string& name){ if(!h) return nullptr; return dlsym(h, name.c_str()); }
} // namespace ax
