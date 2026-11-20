#include "runtime/core/quantized.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ax::ai {
namespace {

unsigned bits_per_element(LowBitFormat format) {
  switch (format) {
    case LowBitFormat::I1: return 1;
    case LowBitFormat::Trit:
    case LowBitFormat::I2: return 2;
    case LowBitFormat::I4:
    case LowBitFormat::FP4E2M1: return 4;
    case LowBitFormat::FP8E4M3:
    case LowBitFormat::FP8E5M2: return 8;
    case LowBitFormat::BF16: return 16;
  }
  throw std::invalid_argument("unknown low-bit format");
}

std::uint32_t read_code(std::span<const std::uint8_t> bytes, std::size_t index,
                        unsigned width) {
  const std::size_t bit = index * width;
  const std::size_t byte_index = bit / 8;
  const unsigned shift = static_cast<unsigned>(bit % 8);
  const std::size_t required = (bit + width + 7) / 8;
  if (bytes.size() < required) throw std::invalid_argument("packed buffer is too small");
  std::uint32_t word = bytes[byte_index];
  if (shift + width > 8) word |= static_cast<std::uint32_t>(bytes[byte_index + 1]) << 8;
  return (word >> shift) & ((1u << width) - 1u);
}

void write_code(std::span<std::uint8_t> bytes, std::size_t index,
                unsigned width, std::uint32_t code) {
  const std::size_t bit = index * width;
  const std::size_t byte_index = bit / 8;
  const unsigned shift = static_cast<unsigned>(bit % 8);
  const std::size_t required = (bit + width + 7) / 8;
  if (bytes.size() < required) throw std::invalid_argument("packed buffer is too small");
  const std::uint32_t mask = (1u << width) - 1u;
  const std::uint32_t shifted_mask = mask << shift;
  std::uint32_t word = bytes[byte_index];
  if (shift + width > 8) word |= static_cast<std::uint32_t>(bytes[byte_index + 1]) << 8;
  word = (word & ~shifted_mask) | ((code & mask) << shift);
  bytes[byte_index] = static_cast<std::uint8_t>(word & 0xffu);
  if (shift + width > 8) bytes[byte_index + 1] = static_cast<std::uint8_t>((word >> 8) & 0xffu);
}

float decode_fp4(std::uint32_t code) {
  const bool negative = (code & 0x8u) != 0;
  const unsigned exponent = (code >> 1) & 0x3u;
  const unsigned mantissa = code & 1u;
  float value = 0.0f;
  if (exponent == 0) {
    value = mantissa == 0 ? 0.0f : 0.5f;
  } else {
    const float significand = 1.0f + static_cast<float>(mantissa) * 0.5f;
    value = significand * std::ldexp(1.0f, static_cast<int>(exponent) - 1);
  }
  return negative ? -value : value;
}

std::uint32_t encode_fp4(float value) {
  const bool negative = std::signbit(value);
  const float magnitude = std::fabs(value);
  unsigned best = 0;
  float best_error = std::numeric_limits<float>::infinity();
  for (unsigned code = 0; code < 8; ++code) {
    const float candidate = decode_fp4(code);
    const float error = std::fabs(candidate - magnitude);
    if (error < best_error) {
      best_error = error;
      best = code;
    }
  }
  return best | (negative ? 0x8u : 0u);
}

float decode_fp8(std::uint32_t code, bool e4m3) {
  const bool negative = (code & 0x80u) != 0;
  const unsigned exponent_bits = e4m3 ? 4u : 5u;
  const unsigned mantissa_bits = e4m3 ? 3u : 2u;
  const unsigned exponent_mask = (1u << exponent_bits) - 1u;
  const unsigned mantissa_mask = (1u << mantissa_bits) - 1u;
  const unsigned exponent = (code >> mantissa_bits) & exponent_mask;
  const unsigned mantissa = code & mantissa_mask;
  const unsigned bias = e4m3 ? 7u : 15u;
  float value;
  if (exponent == 0) {
    value = std::ldexp(static_cast<float>(mantissa) / static_cast<float>(1u << mantissa_bits),
                       1 - static_cast<int>(bias));
  } else if (e4m3 && exponent == exponent_mask && mantissa == mantissa_mask) {
    value = std::numeric_limits<float>::quiet_NaN();
  } else if (!e4m3 && exponent == exponent_mask) {
    value = mantissa == 0 ? std::numeric_limits<float>::infinity()
                          : std::numeric_limits<float>::quiet_NaN();
  } else {
    value = std::ldexp(1.0f + static_cast<float>(mantissa) /
                                  static_cast<float>(1u << mantissa_bits),
                       static_cast<int>(exponent) - static_cast<int>(bias));
  }
  return negative ? -value : value;
}

std::uint32_t encode_fp8(float value, bool e4m3) {
  if (std::isnan(value)) return e4m3 ? 0x7fu : 0x7eu;
  const bool negative = std::signbit(value);
  if (std::isinf(value)) {
    const std::uint32_t code = e4m3 ? 0x7eu : 0x7cu;
    return code | (negative ? 0x80u : 0u);
  }
  const float magnitude = std::fabs(value);
  float best_error = std::numeric_limits<float>::infinity();
  std::uint32_t best = 0;
  for (std::uint32_t code = 0; code < 128; ++code) {
    const float candidate = decode_fp8(code, e4m3);
    if (!std::isfinite(candidate)) continue;
    const float error = std::fabs(candidate - magnitude);
    if (error < best_error || (error == best_error && (code & 1u) == 0)) {
      best_error = error;
      best = code;
    }
  }
  return best | (negative ? 0x80u : 0u);
}

std::uint32_t encode_integer(float value, LowBitFormat format) {
  if (format == LowBitFormat::I1) return value >= 0.5f ? 1u : 0u;
  if (format == LowBitFormat::Trit) {
    const long rounded = std::lround(value);
    if (rounded < 0) return 2u;
    if (rounded > 0) return 1u;
    return 0u;
  }
  const unsigned width = bits_per_element(format);
  const int minimum = -(1 << (width - 1));
  const int maximum = (1 << (width - 1)) - 1;
  const int rounded = static_cast<int>(std::lround(value));
  const int clamped = std::clamp(rounded, minimum, maximum);
  return static_cast<std::uint32_t>(clamped) & ((1u << width) - 1u);
}

float decode_integer(std::uint32_t code, LowBitFormat format) {
  if (format == LowBitFormat::I1) return static_cast<float>(code);
  if (format == LowBitFormat::Trit) {
    if (code == 1u) return 1.0f;
    if (code == 2u) return -1.0f;
    return 0.0f;
  }
  const unsigned width = bits_per_element(format);
  const std::uint32_t sign_bit = 1u << (width - 1);
  const int value = static_cast<int>((code ^ sign_bit) - sign_bit);
  return static_cast<float>(value);
}

std::uint32_t trit_digit(float value) {
  const long rounded = std::lround(value);
  if (rounded < 0) return 0u;
  if (rounded > 0) return 2u;
  return 1u;
}

float decode_trit(std::span<const std::uint8_t> packed, std::size_t index) {
  const std::size_t byte_index = index / 5;
  if (byte_index >= packed.size()) throw std::invalid_argument("packed trit buffer is too small");
  std::uint32_t code = packed[byte_index];
  for (std::size_t digit = 0; digit < index % 5; ++digit) code /= 3u;
  const std::uint32_t value = code % 3u;
  if (value == 0u) return -1.0f;
  if (value == 2u) return 1.0f;
  return 0.0f;
}

} // namespace

std::size_t packed_size(LowBitFormat format, std::size_t element_count) {
  if (format == LowBitFormat::Trit) return element_count / 5 + (element_count % 5 != 0 ? 1 : 0);
  const std::size_t width = bits_per_element(format);
  if (element_count > std::numeric_limits<std::size_t>::max() / width) {
    throw std::length_error("packed tensor size overflow");
  }
  const std::size_t bits = element_count * width;
  return bits / 8 + (bits % 8 != 0 ? 1 : 0);
}

std::vector<std::uint8_t> pack_values(LowBitFormat format,
                                      std::span<const float> values) {
  if (format == LowBitFormat::Trit) {
    std::vector<std::uint8_t> packed(packed_size(format, values.size()), 0);
    for (std::size_t i = 0; i < values.size(); ++i) {
      static constexpr std::uint32_t powers_of_three[]{1u, 3u, 9u, 27u, 81u};
      packed[i / 5] = static_cast<std::uint8_t>(packed[i / 5] +
          trit_digit(values[i]) * powers_of_three[i % 5]);
    }
    return packed;
  }
  const unsigned width = bits_per_element(format);
  std::vector<std::uint8_t> packed(packed_size(format, values.size()), 0);
  for (std::size_t i = 0; i < values.size(); ++i) {
    std::uint32_t code = 0;
    switch (format) {
      case LowBitFormat::Trit:
        throw std::logic_error("ternary packing path was not selected");
      case LowBitFormat::I1:
      case LowBitFormat::I2:
      case LowBitFormat::I4:
        code = encode_integer(values[i], format);
        break;
      case LowBitFormat::FP4E2M1:
        code = encode_fp4(values[i]);
        break;
      case LowBitFormat::FP8E4M3:
        code = encode_fp8(values[i], true);
        break;
      case LowBitFormat::FP8E5M2:
        code = encode_fp8(values[i], false);
        break;
      case LowBitFormat::BF16: {
        std::uint32_t raw = std::bit_cast<std::uint32_t>(values[i]);
        const std::uint32_t bias = 0x7fffu + ((raw >> 16) & 1u);
        code = (raw + bias) >> 16;
        break;
      }
    }
    write_code(packed, i, width, code);
  }
  return packed;
}

std::vector<float> unpack_values(LowBitFormat format,
                                 std::span<const std::uint8_t> packed,
                                 std::size_t element_count) {
  const unsigned width = bits_per_element(format);
  if (packed.size() < packed_size(format, element_count)) {
    throw std::invalid_argument("packed buffer is too small");
  }
  std::vector<float> values(element_count);
  for (std::size_t i = 0; i < element_count; ++i) {
    const std::uint32_t code = format == LowBitFormat::Trit ? 0u : read_code(packed, i, width);
    switch (format) {
      case LowBitFormat::I1:
      case LowBitFormat::I2:
      case LowBitFormat::I4:
        values[i] = decode_integer(code, format);
        break;
      case LowBitFormat::Trit:
        values[i] = decode_trit(packed, i);
        break;
      case LowBitFormat::FP4E2M1:
        values[i] = decode_fp4(code);
        break;
      case LowBitFormat::FP8E4M3:
        values[i] = decode_fp8(code, true);
        break;
      case LowBitFormat::FP8E5M2:
        values[i] = decode_fp8(code, false);
        break;
      case LowBitFormat::BF16:
        values[i] = std::bit_cast<float>(code << 16);
        break;
    }
  }
  return values;
}

std::vector<float> trit_matmul(std::span<const std::uint8_t> lhs,
                               std::span<const std::uint8_t> rhs,
                               std::size_t rows, std::size_t inner,
                               std::size_t columns) {
  const std::size_t lhs_count = rows * inner;
  const std::size_t rhs_count = inner * columns;
  if (lhs.size() < packed_size(LowBitFormat::Trit, lhs_count) ||
      rhs.size() < packed_size(LowBitFormat::Trit, rhs_count)) {
    throw std::invalid_argument("packed trit matrix is too small");
  }
  std::vector<float> result(rows * columns, 0.0f);
  for (std::size_t row = 0; row < rows; ++row) {
    for (std::size_t column = 0; column < columns; ++column) {
      int sum = 0;
      for (std::size_t k = 0; k < inner; ++k) {
        const int a = static_cast<int>(decode_trit(lhs, row * inner + k));
        const int b = static_cast<int>(decode_trit(rhs, k * columns + column));
        sum += a * b;
      }
      result[row * columns + column] = static_cast<float>(sum);
    }
  }
  return result;
}

} // namespace ax::ai
