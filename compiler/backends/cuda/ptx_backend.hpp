#pragma once
#include "compiler/common/ax_common.hpp"
#include <string>
namespace ax {
std::string emit_ptx(const Module& m);
std::string emit_cuda_host(const Module& m);
} // namespace ax
