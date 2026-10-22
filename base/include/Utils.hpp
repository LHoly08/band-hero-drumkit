#ifndef BH_UTIL
#define BH_UTIL

#include <array>
#include <atomic>
#include <cstdint>
#include <string_view>

namespace bh {

enum class State : std::uint32_t {
  Config = 0,
  PlayingSerial,
  PlayingHub,
  PlayingFreestyle,
};

using AtomicState = std::atomic<State>;
// ESP-IDF's PSRAM workaround can use software-backed atomics on Xtensa.
// State access must be atomic, but need not always be lock-free.

using MAC = std::array<std::uint8_t, 6>;

constexpr std::string_view configFile = "config.lua";

struct Vec3 {
  float x{};
  float y{};
  float z{};

  Vec3 operator-(const Vec3 &other) const noexcept {
    return Vec3{
        .x = x - other.x,
        .y = y - other.y,
        .z = z - other.z,
    };
  }
  float dot(const Vec3 &other) const noexcept {
    return x * other.x + y * other.y + z * other.z;
  }
};

struct Packet {
  Vec3 coords{};
  float speed{};
};

} // namespace bh

#endif
