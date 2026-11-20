#include "runtime/core/ffi.hpp"
#include <dlfcn.h>
namespace ax { void* ffi_dlsym(const char* lib, const char* sym){ void* h=dlopen(lib, RTLD_NOW); if(!h) return nullptr; return dlsym(h, sym); } }
