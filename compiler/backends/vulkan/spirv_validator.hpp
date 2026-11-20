#pragma once
#include "compiler/backends/vulkan/spirv_backend.hpp"
namespace ax { inline bool spirv_valid(const std::vector<uint32_t>& w){ std::string e; return validate_spirv(w,e); } }
