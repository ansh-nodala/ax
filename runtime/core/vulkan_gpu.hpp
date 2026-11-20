#pragma once

#include "include/ax_runtime.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ax::gpu {

struct VulkanDeviceInfo {
  std::string name;
  std::uint32_t vendor_id = 0;
  std::uint32_t device_id = 0;
  std::uint32_t api_version = 0;
};

enum class VulkanKernelKind : std::uint32_t {
  Elementwise = 0,
  Matmul = 1,
  Softmax = 2,
  RmsNorm = 3,
  LayerNorm = 4,
};

using VulkanKernelConfig = AxGpuKernelConfig;

struct VulkanKernel {
  std::vector<std::uint32_t> spirv;
  VulkanKernelConfig config;
};

bool vulkan_support_compiled() noexcept;
std::vector<VulkanDeviceInfo> enumerate_vulkan_gpus();

}  // namespace ax::gpu

extern "C" struct AxVal;
extern "C" AxVal* ax_vulkan_run_spirv(const std::uint32_t* spirv,
                                       std::size_t word_count,
                                       AxVal* const* inputs,
                                       std::size_t input_count,
                                       const AxGpuKernelConfig* config);
