#pragma once
#include "compiler/common/ax_common.hpp"
#include <string>
namespace ax { std::string emit_rocm_hip(const Module& m); int ax_rocm_launch(const char* name, const void* in, void* out, unsigned n); }
