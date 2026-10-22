#ifndef BH_PADS
#define BH_PADS

#include "Queue.hpp"
#include "Utils.hpp"

#include "driver/gpio.h"

#include <cmath>
#include <cstdint>
#include <utility>
#include <variant>

namespace bh {

// Thickness in meters.
inline float Tolerance = 0.05f;

enum class PadType : std::uint8_t {
  Drum = 0,
  Cymbal,
  Pedal,
};

class mp3File {
public:
  mp3File() noexcept = default;
  mp3File(std::string_view file) noexcept;
  ~mp3File() noexcept;
  mp3File(const mp3File &) = delete;
  mp3File &operator=(const mp3File &) = delete;
  mp3File(mp3File &&other) noexcept
      : m_file{std::exchange(other.m_file, nullptr)},
        m_size{std::exchange(other.m_size, 0)} {}
  mp3File &operator=(mp3File &&other) noexcept {
    if (this != &other) {
      m_file = std::exchange(other.m_file, nullptr);
      m_size = std::exchange(other.m_size, 0);
    }
    return *this;
  }

private:
  std::uint8_t *m_file{nullptr};
  std::uint32_t m_size{};
};

template <PadType Type, bool Exists> struct Alternate {};

template <PadType Type> struct Alternate<Type, true> {
  float m_alternateRadiusSquared{};
  float m_lastRadialSquared{};
  mp3File m_alternateAudio{};
};

namespace detail {
Vec3 padAxis(Vec3 position, float tilt) noexcept;
} // namespace detail

// ----- Pads -----

template <PadType Type, bool hasAlternate = true> class Pad {
public:
  using Alternate = bh::Alternate<Type, hasAlternate>;

  Pad(bh::Vec3 position, float radius, float tilt, mp3File &&normalAudio,
      Alternate &&alternate) noexcept;
  ~Pad() noexcept = default;
  Pad(const Pad &) = delete;
  Pad &operator=(const Pad &) = delete;
  Pad(Pad &&other) noexcept;
  Pad &operator=(Pad &&other) noexcept;

  inline bool inside(const bh::Vec3 &point) noexcept {
    const bh::Vec3 d = point - m_position;
    const float h = d.dot(m_axis);

    if (std::abs(h) > Tolerance) {
      return false;
    }

    const float radialSquared = d.dot(d) - h * h;
    if constexpr (hasAlternate) {
      m_alternate.m_lastRadialSquared = radialSquared;
    }
    return radialSquared <= m_radiusSquared;
  }

  void play() noexcept;

private:
  bh::Vec3 m_position{};
  bh::Vec3 m_axis{};
  float m_radiusSquared{};
  mp3File m_normalAudio{};
  Alternate m_alternate{};
};

// ----- Definitions -----

template <PadType Type, bool hasAlternate>
Pad<Type, hasAlternate>::Pad(Vec3 position, float radius, float tilt,
                            mp3File &&normalAudio,
                            Alternate &&alternate) noexcept
    : m_position{position}, m_axis{detail::padAxis(position, tilt)},
      m_radiusSquared{radius * radius}, m_normalAudio{std::move(normalAudio)},
      m_alternate{std::move(alternate)} {}

template <PadType Type, bool hasAlternate>
Pad<Type, hasAlternate>::Pad(Pad &&other) noexcept
    : m_position{std::exchange(other.m_position, {})},
      m_axis{std::exchange(other.m_axis, {})},
      m_radiusSquared{std::exchange(other.m_radiusSquared, {})},
      m_normalAudio{std::exchange(other.m_normalAudio, {})},
      m_alternate{std::exchange(other.m_alternate, {})} {}

template <PadType Type, bool hasAlternate>
Pad<Type, hasAlternate> &
Pad<Type, hasAlternate>::operator=(Pad &&other) noexcept {
  if (this != &other) {
    m_position = std::exchange(other.m_position, {});
    m_axis = std::exchange(other.m_axis, {});
    m_radiusSquared = std::exchange(other.m_radiusSquared, {});
    m_normalAudio = std::exchange(other.m_normalAudio, {});
    m_alternate = std::exchange(other.m_alternate, {});
  }
  return *this;
}

template <PadType Type, bool hasAlternate>
void Pad<Type, hasAlternate>::play() noexcept {
  if constexpr (hasAlternate && Type == PadType::Drum) {
    if (m_alternate.m_lastRadialSquared >
        m_alternate.m_alternateRadiusSquared) {
      // Play Alternate Audio
      return;
    }

  } else if constexpr (hasAlternate && Type == PadType::Cymbal) {
    if (m_alternate.m_lastRadialSquared <=
        m_alternate.m_alternateRadiusSquared) {
      // Play Alternate Audio
      return;
    }
  }
  // Play Regular Audio
}

// ----- End of Definitions -----

using StickPlayable =
    std::variant<Pad<PadType::Drum, true>, Pad<PadType::Drum, false>,
                 Pad<PadType::Cymbal, true>, Pad<PadType::Cymbal, false>>;

// ----- End of Pads -----

// ----- Pedal -----

template <> class Pad<PadType::Pedal, true> {
public:
  Pad(const ::gpio_num_t pin) noexcept;
  ~Pad() = default;
  Pad(const Pad &) = delete;
  Pad &operator=(const Pad &) = delete;
  Pad(Pad &&) = delete;
  Pad &operator=(Pad &&) = delete;

  inline bool pressed() const noexcept { return m_pressed; }

  inline static void pedalPlayed(void *args) {
    if (args != nullptr) {
      *static_cast<bool *>(args) = true;
    }
  }
  inline static void pedalLifted(void *args) {
    if (args != nullptr) {
      *static_cast<bool *>(args) = false;
    }
  }

private:
  ::gpio_num_t m_pin;
  bool m_pressed{false};
};

// ----- End of Pedal -----

} // namespace bh

#endif
