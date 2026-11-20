#pragma once

#include <cstdint>
#include <compare>
#include <array>
#include <utility>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace ax::game {
namespace detail {

using Wide = __int128_t;
using UWide = __uint128_t;

constexpr UWide integer_sqrt(UWide value) noexcept {
  if (value == 0) return 0;
  UWide bit = static_cast<UWide>(1) << 126;
  while (bit > value) bit >>= 2;
  UWide result = 0;
  while (bit != 0) {
    if (value >= result + bit) {
      value -= result + bit;
      result = (result >> 1) + bit;
    } else {
      result >>= 1;
    }
    bit >>= 2;
  }
  return result;
}

} // namespace detail

template <typename Rep, unsigned FractionBits>
class FixedPoint {
  static_assert(std::is_signed_v<Rep> && std::is_integral_v<Rep>);
  static_assert(FractionBits > 0 && FractionBits < sizeof(Rep) * 8 - 1);

 public:
  using rep_type = Rep;
  static constexpr unsigned fractional_bits = FractionBits;
  static constexpr Rep scale = static_cast<Rep>(Rep{1} << FractionBits);

  constexpr FixedPoint() = default;
  static constexpr FixedPoint from_raw(Rep raw) noexcept {
    FixedPoint result;
    result.raw_ = raw;
    return result;
  }
  static FixedPoint from_integer(std::int64_t value) noexcept {
    return from_raw(saturate(static_cast<detail::Wide>(value) * scale));
  }
  static FixedPoint from_ratio(std::int64_t numerator, std::int64_t denominator) {
    if (denominator == 0) throw std::domain_error("fixed-point division by zero");
    const detail::Wide scaled = static_cast<detail::Wide>(numerator) * scale;
    return from_raw(saturate(scaled / denominator));
  }

  constexpr Rep raw() const noexcept { return raw_; }
  constexpr double to_double() const noexcept {
    return static_cast<double>(raw_) / static_cast<double>(scale);
  }

  friend FixedPoint operator+(FixedPoint lhs, FixedPoint rhs) noexcept {
    return from_raw(saturate(static_cast<detail::Wide>(lhs.raw_) + rhs.raw_));
  }
  friend FixedPoint operator-(FixedPoint lhs, FixedPoint rhs) noexcept {
    return from_raw(saturate(static_cast<detail::Wide>(lhs.raw_) - rhs.raw_));
  }
  friend FixedPoint operator*(FixedPoint lhs, FixedPoint rhs) noexcept {
    const detail::Wide product = static_cast<detail::Wide>(lhs.raw_) * rhs.raw_;
    return from_raw(saturate(product / scale));
  }
  friend FixedPoint operator/(FixedPoint lhs, FixedPoint rhs) {
    if (rhs.raw_ == 0) throw std::domain_error("fixed-point division by zero");
    const detail::Wide numerator = static_cast<detail::Wide>(lhs.raw_) * scale;
    return from_raw(saturate(numerator / rhs.raw_));
  }

  static FixedPoint sqrt(FixedPoint value) {
    if (value.raw_ < 0) throw std::domain_error("fixed-point square root of a negative value");
    const detail::UWide scaled = static_cast<detail::UWide>(value.raw_) << FractionBits;
    const detail::UWide root = detail::integer_sqrt(scaled);
    if (root > static_cast<detail::UWide>(std::numeric_limits<Rep>::max())) {
      return from_raw(std::numeric_limits<Rep>::max());
    }
    return from_raw(static_cast<Rep>(root));
  }

  static FixedPoint sin(FixedPoint angle) noexcept {
    const auto [sine, cosine] = sin_cos_raw(angle.raw_);
    (void)cosine;
    return from_raw(sine);
  }

  static FixedPoint cos(FixedPoint angle) noexcept {
    const auto [sine, cosine] = sin_cos_raw(angle.raw_);
    (void)sine;
    return from_raw(cosine);
  }

  friend constexpr bool operator==(FixedPoint, FixedPoint) noexcept = default;
  friend constexpr auto operator<=>(FixedPoint, FixedPoint) noexcept = default;

 private:
  static std::pair<Rep, Rep> sin_cos_raw(Rep raw_angle) noexcept {
    constexpr std::array<std::int64_t, 32> atan_q32{
        3373259426, 1991351318, 1052175346, 534100635, 268086748, 134174063,
        67103403, 33553749, 16777131, 8388597, 4194303, 2097152, 1048576,
        524288, 262144, 131072, 65536, 32768, 16384, 8192, 4096, 2048,
        1024, 512, 256, 128, 64, 32, 16, 8, 4, 2};
    constexpr unsigned shift = 32 - FractionBits;
    constexpr auto scale_constant = [](std::int64_t value) {
      if constexpr (shift == 0) return static_cast<detail::Wide>(value);
      else return (static_cast<detail::Wide>(value) + (static_cast<detail::Wide>(1) << (shift - 1))) >> shift;
    };
    constexpr detail::Wide pi = scale_constant(13493037705LL);
    constexpr detail::Wide half_pi = scale_constant(6746518852LL);
    constexpr detail::Wide two_pi = scale_constant(26986075409LL);
    constexpr detail::Wide gain_inverse = scale_constant(2608131496LL);
    detail::Wide z = static_cast<detail::Wide>(raw_angle) % two_pi;
    if (z > pi) z -= two_pi;
    if (z < -pi) z += two_pi;
    detail::Wide sign = 1;
    if (z > half_pi) { z = pi - z; sign = -1; }
    else if (z < -half_pi) { z = -pi - z; sign = -1; }

    detail::Wide x = gain_inverse;
    detail::Wide y = 0;
    for (unsigned i = 0; i < atan_q32.size(); ++i) {
      const detail::Wide angle = scale_constant(atan_q32[i]);
      const detail::Wide old_x = x;
      const detail::Wide old_y = y;
      if (z >= 0) {
        x = old_x - (old_y >> i);
        y = old_y + (old_x >> i);
        z -= angle;
      } else {
        x = old_x + (old_y >> i);
        y = old_y - (old_x >> i);
        z += angle;
      }
    }
    return {saturate(y * sign), saturate(x * sign)};
  }

  static Rep saturate(detail::Wide value) noexcept {
    if (value > std::numeric_limits<Rep>::max()) return std::numeric_limits<Rep>::max();
    if (value < std::numeric_limits<Rep>::min()) return std::numeric_limits<Rep>::min();
    return static_cast<Rep>(value);
  }

  Rep raw_ = 0;
};

using fixed32 = FixedPoint<std::int32_t, 16>; // Q16.16
using fixed64 = FixedPoint<std::int64_t, 32>; // Q32.32

} // namespace ax::game
