#pragma once

#include <cstddef>

namespace ax::runtime {

void secure_zero(void* memory, std::size_t size) noexcept;

} // namespace ax::runtime
