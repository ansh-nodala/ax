#pragma once
#include <string>
namespace ax { int ax_cuda_launch(const char* ptx, const char* entry, const void* in, void* out, unsigned n); }
