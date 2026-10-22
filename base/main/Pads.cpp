#include <cmath>
#include <cstdint>
#include <utility>

#include "Playable.hpp"

namespace bh {

template <bh::PadType Type>
bh::Pad<Type, true>::Pad(bh::Vec3 position, float radius, float tilt,
                         bh::mp3File &&normalAudio,
                         bh::Alternate &&alternate) noexcept
    : m_position{position}, m_axis{[position, tilt]() -> Vec3 {
        auto fastInverseSquareRoot = [](float number) -> float {
          // Quake III Fast Inverse Square Root Algorithm
          constexpr float ThreeHalfs = 1.5f;

          float x2 = number * 0.5f;
          float y = number;
          std::uint32_t i = std::bit_cast<std::uint32_t>(y);
          i = 0x5F3759DF - (i >> 1);
          y = std::bit_cast<float>(i);
          y = y * (ThreeHalfs - (x2 * y * y)); // 1st Iteration
          y = y * (ThreeHalfs - (x2 * y * y)); // 2nd Iteration
          y = y * (ThreeHalfs - (x2 * y * y)); // 3rd Iteration

          return y;
        };

        const float sumSides =
            position.x * position.x + position.y * position.y;

        // At the XY origin there is no inward direction, so stay horizontal.
        if (sumSides == 0.0f) {
          return {0.0f, 0.0f, 1.0f};
        }

        const float inverseDistance = fastInverseSquareRoot(sumSides);

        const float lean = std::sin(tilt);
        return {-(position.x * inverseDistance) * lean,
                -(position.y * inverseDistance) * lean, std::cos(tilt)};
      }()},
      m_radiusSquared{radius * radius}, m_normalAudio{std::move(normalAudio)},
      m_alternate{.m_alternateAudio = std::move(alternate.m_alternateAudio),
                  .m_alternateRadiusSquared =
                      alternate.m_alternateRadiusSquared} {}

template <bh::PadType Type>
bh::Pad<Type, false>::Pad(bh::Vec3 position, float radius, float tilt,
                          bh::mp3File &&normalAudio,
                          bh::Alternate &&alternate) noexcept
    : m_position{position}, m_axis{[position, tilt]() -> Vec3 {
        // Quake III Fast Inverse Square Root Algorithm
        auto fastInverseSquareRoot = [](float number) -> float {
          constexpr float ThreeHalfs = 1.5f;

          float x2 = number * 0.5f;
          float y = number;
          std::uint32_t i = std::bit_cast<std::uint32_t>(y);
          i = 0x5F3759DF - (i >> 1);
          y = std::bit_cast<float>(i);
          y = y * (ThreeHalfs - (x2 * y * y)); // 1st Iteration
          y = y * (ThreeHalfs - (x2 * y * y)); // 2nd Iteration
          y = y * (ThreeHalfs - (x2 * y * y)); // 3rd Iteration

          return y;
        };

        const float sumSides =
            position.x * position.x + position.y * position.y;

        // At the XY origin there is no inward direction, so stay horizontal.
        if (sumSides == 0.0f) {
          return {0.0f, 0.0f, 1.0f};
        }

        const float inverseDistance = fastInverseSquareRoot(sumSides);

        const float lean = std::sin(tilt);
        return {-(position.x * inverseDistance) * lean,
                -(position.y * inverseDistance) * lean, std::cos(tilt)};
      }()},
      m_radiusSquared{radius * radius}, m_normalAudio{std::move(normalAudio)},
      m_alternate{} {}

} // namespace bh
