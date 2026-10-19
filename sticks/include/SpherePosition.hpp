#ifndef BH_SPHERE_POSITION
#define BH_SPHERE_POSITION

#include <cmath>

namespace bh {

struct Vector3 {
  float x{};
  float y{};
  float z{};
};

struct Quaternion {
  float w{1.0f};
  float x{};
  float y{};
  float z{};
};

struct SphereConfig {
  // Model parameters, in meters. Both sticks use the same chest origin.
  float radius{0.275f};
  Vector3 center{};
  // Sensor-local axis pointing toward the tip. Change for your actual mounting.
  Vector3 tip_axis{1.0f, 0.0f, 0.0f};
};

// An orientation-based estimate, not measured translation. Playing axes:
// X = right, Y = forward, Z = up. Recenter changes heading only, preserving up.
class SpherePosition {
public:
  explicit constexpr SpherePosition(SphereConfig config = {}) noexcept
      : m_config(config) {}

  void invalidate() noexcept { m_calibrated = false; }
  bool calibrated() const noexcept { return m_calibrated; }
  Vector3 position() const noexcept { return m_position; }

  // q maps the sensor frame into the BNO085 world frame (Z up).
  // On recenter, point the tip level and forward. Vertical poses cannot define
  // forward and are rejected; the caller can keep its reset request pending.
  bool update(Quaternion q, bool recenter = false) noexcept {
    const float norm2 = q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z;
    const auto axis = m_config.tip_axis;
    const float axis2 = axis.x * axis.x + axis.y * axis.y + axis.z * axis.z;
    if (!std::isfinite(norm2) || norm2 < 1e-12f ||
        !std::isfinite(axis2) || axis2 < 1e-12f ||
        !std::isfinite(m_config.radius) || m_config.radius <= 0.0f ||
        !std::isfinite(m_config.center.x) ||
        !std::isfinite(m_config.center.y) ||
        !std::isfinite(m_config.center.z)) {
      return false;
    }
    const float inverse_norm = 1.0f / std::sqrt(norm2);
    q.w *= inverse_norm;
    q.x *= inverse_norm;
    q.y *= inverse_norm;
    q.z *= inverse_norm;
    const float inverse_axis = 1.0f / std::sqrt(axis2);
    const Vector3 v{axis.x * inverse_axis, axis.y * inverse_axis,
                    axis.z * inverse_axis};
    // Rotate v by q: v + w * (2 * q.xyz cross v)
    //                  + q.xyz cross (2 * q.xyz cross v).
    const Vector3 t{2.0f * (q.y * v.z - q.z * v.y),
                    2.0f * (q.z * v.x - q.x * v.z),
                    2.0f * (q.x * v.y - q.y * v.x)};
    const Vector3 world{v.x + q.w * t.x + q.y * t.z - q.z * t.y,
                        v.y + q.w * t.y + q.z * t.x - q.x * t.z,
                        v.z + q.w * t.z + q.x * t.y - q.y * t.x};

    if (recenter) {
      const float horizontal2 = world.x * world.x + world.y * world.y;
      if (horizontal2 < 0.01f) {
        return false;
      }
      const float inverse_horizontal = 1.0f / std::sqrt(horizontal2);
      m_forward_cos = world.x * inverse_horizontal;
      m_forward_sin = world.y * inverse_horizontal;
      m_calibrated = true;
    }
    if (!m_calibrated) {
      return false;
    }

    const float forward = m_forward_cos * world.x + m_forward_sin * world.y;
    const float left = -m_forward_sin * world.x + m_forward_cos * world.y;
    m_position = {m_config.center.x - m_config.radius * left,
                  m_config.center.y + m_config.radius * forward,
                  m_config.center.z + m_config.radius * world.z};
    return true;
  }

private:
  SphereConfig m_config;
  Vector3 m_position{};
  float m_forward_cos{1.0f};
  float m_forward_sin{};
  bool m_calibrated{};
};

} // namespace bh

#endif
