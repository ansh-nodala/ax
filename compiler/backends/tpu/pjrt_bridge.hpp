#pragma once
#include <string>
namespace ax { int ax_tpu_execute(const std::string& mlir, const void* in, void* out, unsigned n); }
