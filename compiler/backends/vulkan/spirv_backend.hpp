#pragma once
#include <string>
#include <vector>
#include <cstdint>
namespace ax {
std::vector<uint32_t> emit_spirv_binary(const std::string& fn_name);
bool write_spirv_file(const std::string& path, const std::vector<uint32_t>& words);
bool validate_spirv(const std::vector<uint32_t>& words, std::string& err);
std::string emit_vulkan_host(const std::string& fn_name);
} // namespace ax
