#ifndef BH_PADS
#define BH_PADS

#include "Queue.hpp"
#include "Utils.hpp"

#include "hal/gpio_types.h"

#include <cassert>
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

template <PadType Type, bool Exists> struct Alternate {};

template <PadType Type> struct Alternate<Type, true> {
  float m_alternateRadiusSquared{};
  bh::mp3File m_alternateAudio{};
};

namespace detail {
Vec3 padAxis(Vec3 position, float tilt) noexcept;
} // namespace detail

// ----- Pads -----

template <PadType Type, bool hasAlternate = false> class Pad {
public:
  using Alternate = bh::Alternate<Type, hasAlternate>;

  Pad(bh::Vec3 position, float radius, float tilt, mp3File &&normalAudio,
      Alternate &&alternate) noexcept;
  ~Pad() noexcept = default;
  Pad(const Pad &) = delete;
  Pad &operator=(const Pad &) = delete;
  Pad(Pad &&other) noexcept;
  Pad &operator=(Pad &&other) noexcept;

  inline bool inside(const bh::Vec3 &point,
                     float *savedRadialSquared) const noexcept {
    const bh::Vec3 d = point - m_position;
    const float h = d.dot(m_axis);

    if (std::abs(h) > Tolerance) {
      return false;
    }

    const float radialSquared = d.dot(d) - h * h;
    if constexpr (hasAlternate) {
      *savedRadialSquared = radialSquared;
    }
    return radialSquared <= m_radiusSquared;
  }

  void play(float radialSquared, float speed) const noexcept;

private:
  bh::Vec3 m_position{};
  bh::Vec3 m_axis{};
  float m_radiusSquared{};
  bh::mp3File m_normalAudio{};
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
      m_normalAudio{std::exchange(other.m_normalAudio, bh::mp3File{})},
      m_alternate{std::exchange(other.m_alternate, {})} {}

template <PadType Type, bool hasAlternate>
Pad<Type, hasAlternate> &
Pad<Type, hasAlternate>::operator=(Pad &&other) noexcept {
  if (this != &other) {
    m_position = std::exchange(other.m_position, {});
    m_axis = std::exchange(other.m_axis, {});
    m_radiusSquared = std::exchange(other.m_radiusSquared, {});
    m_normalAudio = std::exchange(other.m_normalAudio, bh::mp3File{});
    m_alternate = std::exchange(other.m_alternate, {});
  }
  return *this;
}

template <PadType Type, bool hasAlternate>
void Pad<Type, hasAlternate>::play(float radialSquared,
                                   float speed) const noexcept {
  if constexpr (hasAlternate && Type == PadType::Drum) {
    if (radialSquared > m_alternate.m_alternateRadiusSquared) {
      // Play Alternate Audio
      return;
    }

  } else if constexpr (hasAlternate && Type == PadType::Cymbal) {
    if (radialSquared <= m_alternate.m_alternateRadiusSquared) {
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

template <bool hasAlternate> class Pad<PadType::Pedal, hasAlternate> {
public:
  Pad(const ::gpio_num_t pin) noexcept;
  ~Pad() = default;
  Pad(const Pad &) = delete;
  Pad &operator=(const Pad &) = delete;
  Pad(Pad &&) = delete;
  Pad &operator=(Pad &&) = delete;

  inline void play() const noexcept {
    if (!m_pressed) {
      return;
    }
    m_pressed = false;

    // Play Audio
  }

  inline bool pressed() const noexcept {
    if (!m_pressed) {
      return false;
    }
    m_pressed = false;
    return true;
  }

  inline static void pedalPlayed(void *args) {
    if (args != nullptr) {
      *static_cast<bool *>(args) = true;
    }
  }

private:
  ::gpio_num_t m_pin;
  mutable bool m_pressed{false};
};

template <bool hasAlternate>
Pad<PadType::Pedal, hasAlternate>::Pad(const ::gpio_num_t pin) noexcept {
  ESP_ERROR_CHECK(::gpio_install_isr_service(ESP_INTR_FLAG_IRAM));
  {
    ::gpio_config_t cfg;

    using ISR_CFG = bh::OppositeEdge<true>;

    cfg.pin_bit_mask = 1ULL << pin;
    cfg.mode = ::GPIO_MODE_INPUT;
    cfg.pull_up_en = (gpio_pullup_t)ISR_CFG::pull_up;
    cfg.pull_down_en = (gpio_pulldown_t)ISR_CFG::pull_down;
    cfg.intr_type = (gpio_int_type_t)ISR_CFG::intr_type;

    ESP_ERROR_CHECK(::gpio_config(&cfg));
  }

  ESP_ERROR_CHECK(::gpio_isr_handler_add(pin, pedalPlayed, &m_pressed));
}

// ----- End of Pedal -----

} // namespace bh

#endif
