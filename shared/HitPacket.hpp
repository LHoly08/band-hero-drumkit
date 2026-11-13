#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace bh {

struct Vec3 {
  float x{};
  float y{};
  float z{};

  Vec3 operator-(const Vec3 &other) const noexcept {
    return {x - other.x, y - other.y, z - other.z};
  }
  float dot(const Vec3 &other) const noexcept {
    return x * other.x + y * other.y + z * other.z;
  }
};

// Stick -> base, identical in every playing mode. XYZ is in meters:
// X right, Y forward, Z up. Speed is the detector's peak scaled gyro value.
// ESP32-C6 and ESP32-S3 transmit four little-endian IEEE-754 binary32 floats.
struct Packet {
  Vec3 coords{};
  float speed{};
};

static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
static_assert(std::endian::native == std::endian::little);
static_assert(std::is_standard_layout_v<Vec3> && sizeof(Vec3) == 12);
static_assert(offsetof(Vec3, x) == 0 && offsetof(Vec3, y) == 4 &&
              offsetof(Vec3, z) == 8);
static_assert(std::is_standard_layout_v<Packet> &&
              std::is_trivially_copyable_v<Packet>);
static_assert(sizeof(Packet) == 16 && offsetof(Packet, coords) == 0 &&
              offsetof(Packet, speed) == 12);

} // namespace bh
