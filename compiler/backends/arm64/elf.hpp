#pragma once
#include <string>
#include <vector>
#include <cstdint>
namespace ax {
bool write_elf64_object(const std::string& path, const std::vector<uint32_t>& code);
bool write_elf64_executable(const std::string& path, const std::vector<uint32_t>& code);
} // namespace ax
