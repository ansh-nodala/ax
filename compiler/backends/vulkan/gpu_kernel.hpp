#pragma once

#include "compiler/common/ax_common.hpp"
#include "runtime/core/vulkan_gpu.hpp"

#include <map>
#include <string>
#include <vector>

namespace ax {

using gpu::VulkanKernel;
using gpu::VulkanKernelConfig;
using gpu::VulkanKernelKind;
using GpuKernelMap = std::map<std::string, VulkanKernel>;

bool compile_vulkan_kernel(const Stmt& function, VulkanKernel& kernel,
                           std::string& error);
bool compile_vulkan_kernels(const Module& module, GpuKernelMap& kernels,
                            std::string& error);

}  // namespace ax
