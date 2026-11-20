#pragma once
#include "compiler/common/ax_common.hpp"
#include "compiler/backends/vulkan/gpu_kernel.hpp"
#include <string>

namespace ax {
std::string emit_x86_64_asm(const Module& module, const MirModule& mir,
                            const GpuKernelMap& gpu_kernels = {});
} // namespace ax
