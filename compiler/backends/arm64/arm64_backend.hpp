#pragma once
#include "compiler/common/ax_common.hpp"
#include "compiler/backends/vulkan/gpu_kernel.hpp"
#include <string>
namespace ax {
std::string emit_arm64_asm(const Module& m, const MirModule& mir,
                           const GpuKernelMap& gpu_kernels = {});
} // namespace ax
