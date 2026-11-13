#ifndef BH_PADS
#define BH_PADS

#include "Audio.hpp"
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
      Alternate &&alternate) noexcept
    requires hasAlternate;
  Pad(bh::Vec3 position, float radius, float tilt,
      mp3File &&normalAudio) noexcept
    requires(!hasAlternate);
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
    if (savedRadialSquared) {
      *savedRadialSquared = radialSquared;
    }
    return radialSquared <= m_radiusSquared;
  }

  void play(bh::AudioPlayer &audio, float radialSquared,
            float speed) const noexcept;

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
  requires(hasAlternate)
    : m_position{position}, m_axis{detail::padAxis(position, tilt)},
      m_radiusSquared{radius * radius}, m_normalAudio{std::move(normalAudio)},
      m_alternate{std::move(alternate)} {}

template <PadType Type, bool hasAlternate>
Pad<Type, hasAlternate>::Pad(Vec3 position, float radius, float tilt,
                             mp3File &&normalAudio) noexcept
  requires(!hasAlternate)
    : m_position{position}, m_axis{detail::padAxis(position, tilt)},
      m_radiusSquared{radius * radius}, m_normalAudio{std::move(normalAudio)} {}

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
void Pad<Type, hasAlternate>::play(
    bh::AudioPlayer &audio, float radialSquared,
    [[maybe_unused]] float speed) const noexcept {
  if constexpr (hasAlternate && Type == PadType::Drum) {
    if (radialSquared > m_alternate.m_alternateRadiusSquared) {
      audio.play(m_alternate.m_alternateAudio);
      return;
    }

  } else if constexpr (hasAlternate && Type == PadType::Cymbal) {
    if (radialSquared <= m_alternate.m_alternateRadiusSquared) {
      audio.play(m_alternate.m_alternateAudio);
      return;
    }
  }
  audio.play(m_normalAudio);
}

// ----- End of Definitions -----

using StickPlayable =
    std::variant<Pad<PadType::Drum, true>, Pad<PadType::Drum, false>,
                 Pad<PadType::Cymbal, true>, Pad<PadType::Cymbal, false>>;

// ----- End of Pads -----

// ----- Pedal -----

template <bool hasAlternate> class Pad<PadType::Pedal, hasAlternate> {
public:
  Pad(::gpio_num_t pin, bh::mp3File &&audio) noexcept;
  ~Pad() noexcept { ::gpio_isr_handler_remove(m_pin); }
  Pad(const Pad &) = delete;
  Pad &operator=(const Pad &) = delete;
  Pad(Pad &&) = delete;
  Pad &operator=(Pad &&) = delete;

  inline void play(bh::AudioPlayer &audio) const noexcept {
    if (pressed()) {
      audio.play(m_audio);
    }
  }

  inline bool pressed() const noexcept {
    return m_pressed.exchange(false, std::memory_order_relaxed);
  }

  inline static void pedalPlayed(void *args) {
    if (args != nullptr) {
      static_cast<std::atomic<bool> *>(args)->store(true,
                                                    std::memory_order_relaxed);
    }
  }

private:
  ::gpio_num_t m_pin;
  mutable std::atomic<bool> m_pressed{false};
  bh::mp3File m_audio;
};

template <bool hasAlternate>
Pad<PadType::Pedal, hasAlternate>::Pad(::gpio_num_t pin,
                                       bh::mp3File &&audio) noexcept
    : m_pin{pin}, m_audio{std::move(audio)} {
  const auto result = ::gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
  if (result != ESP_ERR_INVALID_STATE) {
    ESP_ERROR_CHECK(result);
  }
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
