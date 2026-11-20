#pragma once

#include <array>
#include <cstdint>

namespace ax::runtime {

class Philox4x32 {
 public:
  explicit constexpr Philox4x32(std::uint64_t seed) noexcept
      : key_{static_cast<std::uint32_t>(seed), static_cast<std::uint32_t>(seed >> 32)} {}

  constexpr std::array<std::uint32_t, 4> operator()(
      std::array<std::uint32_t, 4> counter) const noexcept {
    constexpr std::uint32_t multiplier0 = 0xD2511F53u;
    constexpr std::uint32_t multiplier1 = 0xCD9E8D57u;
    constexpr std::uint32_t Weyl0 = 0x9E3779B9u;
    constexpr std::uint32_t Weyl1 = 0xBB67AE85u;
    std::uint32_t key0 = key_[0];
    std::uint32_t key1 = key_[1];
    for (unsigned round = 0; round < 10; ++round) {
      const std::uint64_t product0 = static_cast<std::uint64_t>(multiplier0) * counter[0];
      const std::uint64_t product1 = static_cast<std::uint64_t>(multiplier1) * counter[2];
      counter = {
          static_cast<std::uint32_t>(product1 >> 32) ^ counter[1] ^ key0,
          static_cast<std::uint32_t>(product1),
          static_cast<std::uint32_t>(product0 >> 32) ^ counter[3] ^ key1,
          static_cast<std::uint32_t>(product0)};
      if (round != 9) {
        key0 += Weyl0;
        key1 += Weyl1;
      }
    }
    return counter;
  }

  constexpr std::array<std::uint32_t, 4> block(std::uint64_t counter,
                                               std::uint64_t stream = 0) const noexcept {
    return (*this)({static_cast<std::uint32_t>(counter),
                    static_cast<std::uint32_t>(counter >> 32),
                    static_cast<std::uint32_t>(stream),
                    static_cast<std::uint32_t>(stream >> 32)});
  }

  constexpr double uniform01(std::uint64_t counter, std::uint64_t stream = 0,
                             unsigned lane = 0) const noexcept {
    const auto values = block(counter, stream);
    return (static_cast<double>(values[lane & 3u]) + 0.5) / 4294967296.0;
  }

 private:
  std::array<std::uint32_t, 2> key_;
};

} // namespace ax::runtime
