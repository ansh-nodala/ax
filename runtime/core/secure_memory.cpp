#include "runtime/core/secure_memory.hpp"

#include <atomic>
#include <cstdint>

namespace ax::runtime {

void secure_zero(void* memory, std::size_t size) noexcept {
  auto* bytes = static_cast<volatile std::uint8_t*>(memory);
  while (size != 0) {
    *bytes++ = 0;
    --size;
  }
  std::atomic_signal_fence(std::memory_order_seq_cst);
}

} // namespace ax::runtime
