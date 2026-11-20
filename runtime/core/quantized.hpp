#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ax::ai {

enum class LowBitFormat {
  I1,
  Trit,
  I2,
  I4,
  FP4E2M1,
  FP8E4M3,
  FP8E5M2,
  BF16,
};

// Trit values use five base-3 digits per byte; binary formats use their named bit width.
std::size_t packed_size(LowBitFormat format, std::size_t element_count);
std::vector<std::uint8_t> pack_values(LowBitFormat format,
                                      std::span<const float> values);
std::vector<float> unpack_values(LowBitFormat format,
                                 std::span<const std::uint8_t> packed,
                                 std::size_t element_count);
std::vector<float> trit_matmul(std::span<const std::uint8_t> lhs,
                               std::span<const std::uint8_t> rhs,
                               std::size_t rows, std::size_t inner,
                               std::size_t columns);

} // namespace ax::ai
