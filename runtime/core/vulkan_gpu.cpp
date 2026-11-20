#include "runtime/core/vulkan_gpu.hpp"

#include "include/ax_runtime.h"

#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef AX_HAVE_VULKAN
#define AX_HAVE_VULKAN 0
#endif

#if AX_HAVE_VULKAN
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace {

void vk_check(VkResult result, const char* action) {
  if (result != VK_SUCCESS) {
    throw std::runtime_error(std::string(action) + " failed with Vulkan error " +
                             std::to_string(static_cast<int>(result)));
  }
}

VkInstance make_instance() {
  VkApplicationInfo application{};
  application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  application.pApplicationName = "AX GPU runtime";
  application.applicationVersion = 1;
  application.pEngineName = "AX";
  application.engineVersion = 1;
  application.apiVersion = VK_API_VERSION_1_1;
  VkInstanceCreateInfo create{};
  create.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  create.pApplicationInfo = &application;
  VkInstance instance = VK_NULL_HANDLE;
  vk_check(vkCreateInstance(&create, nullptr, &instance), "vkCreateInstance");
  return instance;
}

bool compute_queue(VkPhysicalDevice physical, std::uint32_t& family_index) {
  std::uint32_t count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
  if (count == 0) return false;
  std::vector<VkQueueFamilyProperties> properties(count);
  vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, properties.data());
  for (std::uint32_t index = 0; index < count; ++index) {
    if (properties[index].queueCount > 0 &&
        (properties[index].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0) {
      family_index = index;
      return true;
    }
  }
  return false;
}

bool physical_is_gpu(VkPhysicalDevice physical) {
  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(physical, &properties);
  return properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
         properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ||
         properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU;
}

struct MappedBuffer {
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  void* mapped = nullptr;
  bool coherent = false;
};

struct PipelineBundle {
  VkDevice device = VK_NULL_HANDLE;
  VkDescriptorSetLayout descriptor_layout = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
  VkShaderModule shader = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;

  ~PipelineBundle() {
    if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, nullptr);
    if (shader != VK_NULL_HANDLE) vkDestroyShaderModule(device, shader, nullptr);
    if (pipeline_layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
    if (descriptor_layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, descriptor_layout, nullptr);
  }
};

struct VulkanContext {
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physical = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  std::uint32_t queue_family = 0;
  std::mutex pipeline_mutex;
  std::mutex queue_mutex;
  std::unordered_map<std::string, std::unique_ptr<PipelineBundle>> pipelines;

  ~VulkanContext() {
    if (device != VK_NULL_HANDLE) (void)vkDeviceWaitIdle(device);
    pipelines.clear();
    if (device != VK_NULL_HANDLE) vkDestroyDevice(device, nullptr);
    if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, nullptr);
  }
};

struct DispatchResources {
  explicit DispatchResources(VulkanContext& shared_context) : context(shared_context) {}

  VulkanContext& context;
  VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
  VkCommandPool command_pool = VK_NULL_HANDLE;
  std::vector<MappedBuffer> inputs;
  MappedBuffer output;

  ~DispatchResources() {
    for (const MappedBuffer& input : inputs) {
      if (input.mapped != nullptr) vkUnmapMemory(context.device, input.memory);
    }
    if (output.mapped != nullptr) vkUnmapMemory(context.device, output.memory);
    if (command_pool != VK_NULL_HANDLE) vkDestroyCommandPool(context.device, command_pool, nullptr);
    if (descriptor_pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr);
    if (output.buffer != VK_NULL_HANDLE) vkDestroyBuffer(context.device, output.buffer, nullptr);
    for (const MappedBuffer& input : inputs) {
      if (input.buffer != VK_NULL_HANDLE) vkDestroyBuffer(context.device, input.buffer, nullptr);
      if (input.memory != VK_NULL_HANDLE) vkFreeMemory(context.device, input.memory, nullptr);
    }
    if (output.memory != VK_NULL_HANDLE) vkFreeMemory(context.device, output.memory, nullptr);
  }

  void create_buffer(VkDeviceSize size, MappedBuffer& target) {
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vk_check(vkCreateBuffer(context.device, &buffer_info, nullptr, &target.buffer), "vkCreateBuffer");

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(context.device, target.buffer, &requirements);
    VkPhysicalDeviceMemoryProperties memory_properties{};
    vkGetPhysicalDeviceMemoryProperties(context.physical, &memory_properties);
    std::uint32_t memory_type = std::numeric_limits<std::uint32_t>::max();
    for (std::uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index) {
      if ((requirements.memoryTypeBits & (1u << index)) == 0) continue;
      const VkMemoryPropertyFlags flags = memory_properties.memoryTypes[index].propertyFlags;
      if ((flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0) continue;
      memory_type = index;
      if ((flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0) {
        target.coherent = true;
        break;
      }
    }
    if (memory_type == std::numeric_limits<std::uint32_t>::max()) {
      throw std::runtime_error("Vulkan GPU has no host-visible storage-buffer memory");
    }
    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memory_type;
    vk_check(vkAllocateMemory(context.device, &allocation, nullptr, &target.memory), "vkAllocateMemory");
    vk_check(vkBindBufferMemory(context.device, target.buffer, target.memory, 0), "vkBindBufferMemory");
    vk_check(vkMapMemory(context.device, target.memory, 0, VK_WHOLE_SIZE, 0, &target.mapped), "vkMapMemory");
  }

  void flush_if_needed(const MappedBuffer& buffer) {
    if (buffer.coherent) return;
    VkMappedMemoryRange range{};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = buffer.memory;
    range.offset = 0;
    range.size = VK_WHOLE_SIZE;
    vk_check(vkFlushMappedMemoryRanges(context.device, 1, &range), "vkFlushMappedMemoryRanges");
  }

  void invalidate_if_needed(const MappedBuffer& buffer) {
    if (buffer.coherent) return;
    VkMappedMemoryRange range{};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = buffer.memory;
    range.offset = 0;
    range.size = VK_WHOLE_SIZE;
    vk_check(vkInvalidateMappedMemoryRanges(context.device, 1, &range), "vkInvalidateMappedMemoryRanges");
  }
};

void select_device(VulkanContext& context) {
  std::size_t wanted_device = 0;
  if (const char* configured = std::getenv("AX_GPU_DEVICE")) {
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(configured, &end, 10);
    if (end == configured || end == nullptr || *end != '\0') {
      throw std::runtime_error("AX_GPU_DEVICE must be a numeric index shown by `ax devices`");
    }
    wanted_device = static_cast<std::size_t>(parsed);
  }
  context.instance = make_instance();
  std::uint32_t count = 0;
  vk_check(vkEnumeratePhysicalDevices(context.instance, &count, nullptr),
           "vkEnumeratePhysicalDevices");
  if (count == 0) throw std::runtime_error("no Vulkan physical device was found");
  std::vector<VkPhysicalDevice> devices(count);
  vk_check(vkEnumeratePhysicalDevices(context.instance, &count, devices.data()),
           "vkEnumeratePhysicalDevices");
  bool selected = false;
  std::size_t gpu_index = 0;
  for (const VkPhysicalDevice physical : devices) {
    std::uint32_t family = 0;
    if (physical_is_gpu(physical) && compute_queue(physical, family)) {
      if (gpu_index++ != wanted_device) continue;
      context.physical = physical;
      context.queue_family = family;
      selected = true;
      break;
    }
  }
  if (!selected) {
    if (gpu_index == 0) throw std::runtime_error("no Vulkan GPU with a compute queue was found");
    throw std::runtime_error("AX_GPU_DEVICE is out of range; run `ax devices` to list valid indices");
  }

  const float priority = 1.0f;
  VkDeviceQueueCreateInfo queue_info{};
  queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue_info.queueFamilyIndex = context.queue_family;
  queue_info.queueCount = 1;
  queue_info.pQueuePriorities = &priority;
  VkDeviceCreateInfo device_info{};
  device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  device_info.queueCreateInfoCount = 1;
  device_info.pQueueCreateInfos = &queue_info;
  vk_check(vkCreateDevice(context.physical, &device_info, nullptr, &context.device),
           "vkCreateDevice");
  vkGetDeviceQueue(context.device, context.queue_family, 0, &context.queue);
}

VulkanContext& vulkan_context() {
  static VulkanContext context;
  static std::once_flag initialized;
  static std::string initialization_error;
  std::call_once(initialized, []() {
    try {
      select_device(context);
    } catch (const std::exception& error) {
      initialization_error = error.what();
      if (context.device != VK_NULL_HANDLE) {
        vkDestroyDevice(context.device, nullptr);
        context.device = VK_NULL_HANDLE;
        context.queue = VK_NULL_HANDLE;
      }
      if (context.instance != VK_NULL_HANDLE) {
        vkDestroyInstance(context.instance, nullptr);
        context.instance = VK_NULL_HANDLE;
      }
    }
  });
  if (!initialization_error.empty()) {
    throw std::runtime_error(initialization_error);
  }
  return context;
}

PipelineBundle& pipeline_for(VulkanContext& context, const std::uint32_t* words,
                             std::size_t word_count, std::uint32_t input_count) {
  std::string key(reinterpret_cast<const char*>(words), word_count * sizeof(std::uint32_t));
  key.append(reinterpret_cast<const char*>(&input_count), sizeof(input_count));
  std::lock_guard lock(context.pipeline_mutex);
  const auto existing = context.pipelines.find(key);
  if (existing != context.pipelines.end()) return *existing->second;

  auto bundle = std::make_unique<PipelineBundle>();
  bundle->device = context.device;
  const std::uint32_t descriptor_count = input_count + 1;
  std::vector<VkDescriptorSetLayoutBinding> bindings(descriptor_count);
  for (std::uint32_t index = 0; index < descriptor_count; ++index) {
    bindings[index].binding = index;
    bindings[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[index].descriptorCount = 1;
    bindings[index].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  }
  VkDescriptorSetLayoutCreateInfo descriptor_info{};
  descriptor_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptor_info.bindingCount = descriptor_count;
  descriptor_info.pBindings = bindings.data();
  vk_check(vkCreateDescriptorSetLayout(context.device, &descriptor_info, nullptr,
                                        &bundle->descriptor_layout),
           "vkCreateDescriptorSetLayout");

  VkPushConstantRange push_constant{};
  push_constant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  push_constant.offset = 0;
  push_constant.size = 4 * sizeof(std::uint32_t);
  VkPipelineLayoutCreateInfo layout_info{};
  layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layout_info.setLayoutCount = 1;
  layout_info.pSetLayouts = &bundle->descriptor_layout;
  layout_info.pushConstantRangeCount = 1;
  layout_info.pPushConstantRanges = &push_constant;
  vk_check(vkCreatePipelineLayout(context.device, &layout_info, nullptr,
                                  &bundle->pipeline_layout),
           "vkCreatePipelineLayout");

  VkShaderModuleCreateInfo shader_info{};
  shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  shader_info.codeSize = word_count * sizeof(std::uint32_t);
  shader_info.pCode = words;
  vk_check(vkCreateShaderModule(context.device, &shader_info, nullptr, &bundle->shader),
           "vkCreateShaderModule");
  VkPipelineShaderStageCreateInfo stage{};
  stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  stage.module = bundle->shader;
  stage.pName = "main";
  VkComputePipelineCreateInfo pipeline_info{};
  pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipeline_info.stage = stage;
  pipeline_info.layout = bundle->pipeline_layout;
  vk_check(vkCreateComputePipelines(context.device, VK_NULL_HANDLE, 1, &pipeline_info,
                                    nullptr, &bundle->pipeline),
           "vkCreateComputePipelines");
  PipelineBundle* result = bundle.get();
  context.pipelines.emplace(std::move(key), std::move(bundle));
  return *result;
}

void dispatch_shader(DispatchResources& resources, const std::uint32_t* words,
                     std::size_t word_count,
                     const std::vector<std::vector<float>>& inputs,
                     std::vector<float>& output,
                     const ax::gpu::VulkanKernelConfig& config) {
  if (words == nullptr || word_count < 5 || words[0] != 0x07230203u) {
    throw std::runtime_error("embedded shader is not a valid SPIR-V module");
  }
  if (inputs.empty() || inputs.size() != config.input_count || output.size() != config.output_count) {
    throw std::runtime_error("embedded GPU kernel launch metadata does not match its arguments");
  }
  if (output.empty()) return;
  VulkanContext& context = resources.context;
  PipelineBundle& pipeline = pipeline_for(context, words, word_count, config.input_count);
  VkPhysicalDeviceProperties device_properties{};
  vkGetPhysicalDeviceProperties(context.physical, &device_properties);
  const auto& limits = device_properties.limits;
  const std::uint64_t workgroup_invocations =
      static_cast<std::uint64_t>(config.local_x) * config.local_y;
  if (config.local_x == 0 || config.local_y == 0 ||
      config.local_x > limits.maxComputeWorkGroupSize[0] ||
      config.local_y > limits.maxComputeWorkGroupSize[1] ||
      workgroup_invocations > limits.maxComputeWorkGroupInvocations) {
    throw std::runtime_error("selected Vulkan GPU does not support this kernel's workgroup size");
  }
  std::uint32_t dispatch_x = 1;
  std::uint32_t dispatch_y = 1;
  if (config.kind == static_cast<std::uint32_t>(ax::gpu::VulkanKernelKind::Matmul)) {
    dispatch_x = (config.columns + config.local_x - 1) / config.local_x;
    dispatch_y = (config.rows + config.local_y - 1) / config.local_y;
  } else if (config.kind == static_cast<std::uint32_t>(ax::gpu::VulkanKernelKind::Softmax) ||
             config.kind == static_cast<std::uint32_t>(ax::gpu::VulkanKernelKind::RmsNorm) ||
             config.kind == static_cast<std::uint32_t>(ax::gpu::VulkanKernelKind::LayerNorm)) {
    dispatch_x = config.rows;
  } else {
    dispatch_x = (config.output_count + config.local_x - 1) / config.local_x;
  }
  if (dispatch_x > limits.maxComputeWorkGroupCount[0] ||
      dispatch_y > limits.maxComputeWorkGroupCount[1]) {
    throw std::runtime_error("GPU kernel grid exceeds this device's Vulkan dispatch limits");
  }
  resources.inputs.resize(inputs.size());
  const VkDeviceSize output_bytes = static_cast<VkDeviceSize>(output.size() * sizeof(float));
  for (std::size_t index = 0; index < inputs.size(); ++index) {
    const VkDeviceSize input_bytes = static_cast<VkDeviceSize>(inputs[index].size() * sizeof(float));
    if (input_bytes > limits.maxStorageBufferRange) {
      throw std::runtime_error("GPU input exceeds this device's maximum storage-buffer range");
    }
    resources.create_buffer(input_bytes, resources.inputs[index]);
    std::memcpy(resources.inputs[index].mapped, inputs[index].data(),
                static_cast<std::size_t>(input_bytes));
    resources.flush_if_needed(resources.inputs[index]);
  }
  if (output_bytes > limits.maxStorageBufferRange) {
    throw std::runtime_error("GPU output exceeds this device's maximum storage-buffer range");
  }
  resources.create_buffer(output_bytes, resources.output);

  const std::uint32_t descriptor_count = config.input_count + 1;

  VkDescriptorPoolSize pool_size{};
  pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  pool_size.descriptorCount = descriptor_count;
  VkDescriptorPoolCreateInfo pool_info{};
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.maxSets = 1;
  pool_info.poolSizeCount = 1;
  pool_info.pPoolSizes = &pool_size;
  vk_check(vkCreateDescriptorPool(context.device, &pool_info, nullptr,
                                  &resources.descriptor_pool),
           "vkCreateDescriptorPool");
  VkDescriptorSetAllocateInfo set_info{};
  set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  set_info.descriptorPool = resources.descriptor_pool;
  set_info.descriptorSetCount = 1;
  set_info.pSetLayouts = &pipeline.descriptor_layout;
  VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
  vk_check(vkAllocateDescriptorSets(context.device, &set_info, &descriptor_set),
           "vkAllocateDescriptorSets");

  std::vector<VkDescriptorBufferInfo> buffer_infos(descriptor_count);
  std::vector<VkWriteDescriptorSet> writes(descriptor_count);
  for (std::uint32_t index = 0; index < config.input_count; ++index) {
    buffer_infos[index] = {resources.inputs[index].buffer, 0,
        static_cast<VkDeviceSize>(inputs[index].size() * sizeof(float))};
  }
  buffer_infos[config.input_count] = {resources.output.buffer, 0, output_bytes};
  for (std::uint32_t index = 0; index < descriptor_count; ++index) {
    writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[index].dstSet = descriptor_set;
    writes[index].dstBinding = index;
    writes[index].descriptorCount = 1;
    writes[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[index].pBufferInfo = &buffer_infos[index];
  }
  vkUpdateDescriptorSets(context.device, descriptor_count, writes.data(), 0, nullptr);

  VkCommandPoolCreateInfo command_pool_info{};
  command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  command_pool_info.queueFamilyIndex = context.queue_family;
  vk_check(vkCreateCommandPool(context.device, &command_pool_info, nullptr,
                               &resources.command_pool),
           "vkCreateCommandPool");
  VkCommandBufferAllocateInfo command_info{};
  command_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  command_info.commandPool = resources.command_pool;
  command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  command_info.commandBufferCount = 1;
  VkCommandBuffer command_buffer = VK_NULL_HANDLE;
  vk_check(vkAllocateCommandBuffers(context.device, &command_info, &command_buffer),
           "vkAllocateCommandBuffers");
  VkCommandBufferBeginInfo begin{};
  begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vk_check(vkBeginCommandBuffer(command_buffer, &begin), "vkBeginCommandBuffer");
  vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline);
  vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                          pipeline.pipeline_layout, 0, 1, &descriptor_set, 0, nullptr);
  struct PushConstants {
    std::uint32_t output_count;
    std::uint32_t rows;
    std::uint32_t columns;
    std::uint32_t inner;
  } push{config.output_count, config.rows, config.columns, config.inner};
  vkCmdPushConstants(command_buffer, pipeline.pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                     0, sizeof(push), &push);
  if (config.kind == static_cast<std::uint32_t>(ax::gpu::VulkanKernelKind::Matmul)) {
    vkCmdDispatch(command_buffer, dispatch_x, dispatch_y, 1);
  } else if (config.kind == static_cast<std::uint32_t>(ax::gpu::VulkanKernelKind::Softmax) ||
             config.kind == static_cast<std::uint32_t>(ax::gpu::VulkanKernelKind::RmsNorm) ||
             config.kind == static_cast<std::uint32_t>(ax::gpu::VulkanKernelKind::LayerNorm)) {
    vkCmdDispatch(command_buffer, dispatch_x, 1, 1);
  } else {
    vkCmdDispatch(command_buffer, dispatch_x, 1, 1);
  }
  VkBufferMemoryBarrier host_read{};
  host_read.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
  host_read.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  host_read.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  host_read.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  host_read.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  host_read.buffer = resources.output.buffer;
  host_read.offset = 0;
  host_read.size = output_bytes;
  vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &host_read, 0, nullptr);
  vk_check(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer");
  VkSubmitInfo submit{};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &command_buffer;
  {
    std::lock_guard queue_lock(context.queue_mutex);
    vk_check(vkQueueSubmit(context.queue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit");
    vk_check(vkQueueWaitIdle(context.queue), "vkQueueWaitIdle");
  }
  resources.invalidate_if_needed(resources.output);
  std::memcpy(output.data(), resources.output.mapped, static_cast<std::size_t>(output_bytes));
}

}  // namespace
#endif

namespace ax::gpu {

bool vulkan_support_compiled() noexcept {
#if AX_HAVE_VULKAN
  return true;
#else
  return false;
#endif
}

std::vector<VulkanDeviceInfo> enumerate_vulkan_gpus() {
  std::vector<VulkanDeviceInfo> result;
#if AX_HAVE_VULKAN
  VkInstance instance = VK_NULL_HANDLE;
  try {
    instance = make_instance();
    std::uint32_t count = 0;
    vk_check(vkEnumeratePhysicalDevices(instance, &count, nullptr),
             "vkEnumeratePhysicalDevices");
    std::vector<VkPhysicalDevice> devices(count);
    if (count != 0) {
      vk_check(vkEnumeratePhysicalDevices(instance, &count, devices.data()),
               "vkEnumeratePhysicalDevices");
    }
    for (VkPhysicalDevice device : devices) {
      if (!physical_is_gpu(device)) continue;
      std::uint32_t queue_family = 0;
      if (!compute_queue(device, queue_family)) continue;
      (void)queue_family;
      VkPhysicalDeviceProperties properties{};
      vkGetPhysicalDeviceProperties(device, &properties);
      result.push_back({properties.deviceName, properties.vendorID, properties.deviceID,
                        properties.apiVersion});
    }
  } catch (...) {
    result.clear();
  }
  if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, nullptr);
#endif
  return result;
}

}  // namespace ax::gpu

extern "C" AxVal* ax_vulkan_run_spirv(
    const std::uint32_t* spirv, std::size_t word_count, AxVal* const* input_values,
    std::size_t input_count, const AxGpuKernelConfig* config) {
#if AX_HAVE_VULKAN
  if (config == nullptr || input_values == nullptr || input_count == 0 ||
      input_count != config->input_count || input_count > 2 || config->output_count == 0) {
    std::fprintf(stderr, "ax GPU runtime: invalid GPU launch arguments\n");
    std::exit(70);
  }
  if (config->kind > static_cast<std::uint32_t>(ax::gpu::VulkanKernelKind::LayerNorm)) {
    std::fprintf(stderr, "ax GPU runtime: unknown embedded kernel kind\n");
    std::exit(70);
  }
  try {
    std::vector<std::vector<float>> inputs(input_count);
    for (std::size_t input_index = 0; input_index < input_count; ++input_index) {
      const AxVal* value = input_values[input_index];
      if (value == nullptr || value->kind != 1 || value->len <= 0 || value->arr == nullptr) {
        throw std::runtime_error("@gpu arguments must be non-empty tensor values");
      }
      const std::size_t count = static_cast<std::size_t>(value->len);
      inputs[input_index].resize(count);
      for (std::size_t index = 0; index < count; ++index) {
        inputs[input_index][index] = static_cast<float>(value->arr[index]);
      }
    }
    std::size_t expected_input0 = config->output_count;
    std::size_t expected_input1 = config->output_count;
    if (config->kind == static_cast<std::uint32_t>(ax::gpu::VulkanKernelKind::Matmul)) {
      expected_input0 = static_cast<std::size_t>(config->rows) * config->inner;
      expected_input1 = static_cast<std::size_t>(config->inner) * config->columns;
    }
    if (inputs[0].size() != expected_input0 ||
        (input_count == 2 && inputs[1].size() != expected_input1)) {
      throw std::runtime_error("@gpu tensor length does not match the kernel's fixed Tensor shape");
    }
    std::vector<float> result(config->output_count);
    VulkanContext& context = vulkan_context();
    DispatchResources resources(context);
    dispatch_shader(resources, spirv, word_count, inputs, result, *config);
    std::vector<double> output(result.size());
    for (std::size_t index = 0; index < result.size(); ++index) {
      output[index] = static_cast<double>(result[index]);
    }
    return ax_tensor_vals(output.data(), static_cast<int>(output.size()));
  } catch (const std::exception& error) {
    std::fprintf(stderr, "ax GPU runtime: %s\n", error.what());
    std::exit(70);
  }
#else
  (void)spirv;
  (void)word_count;
  (void)input_values;
  (void)input_count;
  (void)config;
  std::fprintf(stderr, "ax GPU runtime: this AX build has no Vulkan support\n");
  std::exit(70);
#endif
}
