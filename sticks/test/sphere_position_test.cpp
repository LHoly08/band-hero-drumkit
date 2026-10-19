#include "SpherePosition.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

void check(bool ok, const char *message) {
  if (!ok) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

void near(bh::Vector3 actual, bh::Vector3 expected, const char *message) {
  check(std::abs(actual.x - expected.x) < 1e-5f &&
            std::abs(actual.y - expected.y) < 1e-5f &&
            std::abs(actual.z - expected.z) < 1e-5f,
        message);
}

} // namespace

int main() {
  constexpr float radius = 0.275f;
  const float half = std::sqrt(0.5f);
  const bh::Quaternion left{half, 0, 0, half};
  const bh::Quaternion right{half, 0, 0, -half};
  const bh::Quaternion up{half, 0, -half, 0};
  const bh::Quaternion down{half, 0, half, 0};
  bh::SpherePosition model;

  check(!model.update({}), "must calibrate before producing a position");
  check(model.update({}, true), "calibrate forward");
  near(model.position(), {0, radius, 0}, "forward lies on sphere, not at center");
  check(model.update(left), "rotate left");
  near(model.position(), {-radius, 0, 0}, "left is negative X");
  check(model.update(right), "rotate right");
  near(model.position(), {radius, 0, 0}, "right is positive X");
  check(model.update(up), "rotate up");
  near(model.position(), {0, 0, radius}, "up is positive Z");
  check(model.update(down), "rotate down");
  near(model.position(), {0, 0, -radius}, "down is negative Z");

  check(model.update({0, 0, 0, 1}), "rotate backward");
  near(model.position(), {0, -radius, 0}, "backward is negative Y");
  check(model.update({half, half, 0, 0}), "roll along stick axis");
  near(model.position(), {0, radius, 0}, "roll does not change tip position");
  // 90 degree yaw followed by local 90 degree roll.
  check(model.update({0.5f, 0.5f, 0.5f, 0.5f}), "combined yaw and roll");
  near(model.position(), {-radius, 0, 0}, "roll invariance away from forward");

  check(model.update(left, true), "recenter at a different heading");
  near(model.position(), {0, radius, 0}, "new reference becomes forward");
  check(model.update({}), "old forward after recenter");
  near(model.position(), {radius, 0, 0}, "old forward is now right");
  check(model.update({-half * 2, 0, 0, -half * 2}), "scaled negative quaternion");
  near(model.position(), {0, radius, 0}, "quaternion scale and sign invariance");

  check(!model.update({0, 0, 0, 0}), "reject zero quaternion");
  check(!model.update({std::numeric_limits<float>::quiet_NaN(), 0, 0, 0}),
        "reject NaN quaternion");
  model.invalidate();
  check(!model.calibrated() && !model.update({}), "reset invalidates reference");
  check(!model.update(up, true), "vertical pose cannot calibrate heading");
  check(!model.calibrated(), "invalid calibration remains uncalibrated");
  check(model.update({}, true), "recover with valid reference");

  // 45 degree upward tilt: heading reset must preserve gravity-relative height.
  const float angle = std::acos(-1.0f) / 8.0f;
  check(model.update({std::cos(angle), 0, -std::sin(angle), 0}, true),
        "calibrate tilted pose");
  near(model.position(), {0, radius * half, radius * half},
       "heading calibration preserves elevation");

  bh::SpherePosition mounted({0.5f, {1, 2, 3}, {0, 0, 2}});
  check(mounted.update(down, true), "alternate sensor mounting");
  near(mounted.position(), {1, 2.5f, 3}, "configured radius, center and axis");
  check(mounted.update({}), "alternate mounted stick points up");
  near(mounted.position(), {1, 2, 3.5f}, "alternate mounting rotation");

  // Sweep through rotations, checking the fixed-radius geometric constraint.
  for (int i = -180; i <= 180; ++i) {
    const float a = i * std::acos(-1.0f) / 360.0f;
    check(model.update({std::cos(a), 0, 0, std::sin(a)}), "yaw sweep");
    const auto p = model.position();
    check(std::abs(std::sqrt(p.x*p.x + p.y*p.y + p.z*p.z) - radius) < 1e-5f,
          "position stays on sphere");
  }
  bh::SpherePosition invalid_radius({0, {}, {1, 0, 0}});
  check(!invalid_radius.update({}, true), "reject zero radius");
  bh::SpherePosition invalid_axis({radius, {}, {}});
  check(!invalid_axis.update({}, true), "reject zero mounting axis");
  std::cout << "Sphere position tests passed\n";
}
