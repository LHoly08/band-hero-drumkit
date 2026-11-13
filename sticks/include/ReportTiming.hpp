#pragma once
#include <cstdint>

namespace bh {

// SH-2 expands report timestamps to 64 bits. Compare reports in that clock;
// callback receipt times alone cannot detect old samples drained from a backlog.
constexpr bool orientationFresh(std::uint32_t now, std::uint32_t received,
                                std::uint64_t gyro, std::uint64_t orientation) {
  constexpr std::uint32_t maxAge = 50'000;
  const auto separation = gyro >= orientation ? gyro - orientation
                                              : orientation - gyro;
  return std::uint32_t(now - received) <= maxAge && separation <= maxAge;
}

} // namespace bh
