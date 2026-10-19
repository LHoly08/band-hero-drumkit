#ifndef BH_UTIL
#define BH_UTIL

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

constexpr std::string_view configFile = "Config.lua";

struct Coordinates {
  float pitch{};
  float yaw{};
  float roll{};
};

struct Packet {
  Coordinates coords{};
  float speed{};
};

} // namespace bh

#endif
