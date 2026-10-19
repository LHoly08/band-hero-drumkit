#ifndef BH_PLAYABLE
#define BH_PLAYABLE

#include "Queue.hpp"
#include "Utils.hpp"

#include "driver/gpio.h"
#include <cmath>
#include <cstdint>
#include <utility>
#include <variant>

namespace bh {

namespace {

// Thickness in meters.
constexpr float PAD_THICKNESS = 0.05f;

enum class PadType : std::uint8_t {
  Drum = 0,
  Cymbal,
  Pedal,
};

struct mp3File {
  std::uint8_t *file{nullptr};
  std::uint32_t size{};
};

template <PadType Type, bool Exists> struct Alternate;

template <> struct Alternate<PadType::Cymbal, true> {
  float m_alternateRadius{};
  mp3File m_alternateAudio{};
};
template <> struct Alternate<PadType::Drum, true> {
  mp3File m_alternateAudio{};
};
template <PadType Type> struct Alternate<Type, false> {};

} // namespace

template <PadType Type, bool hasAlternate = true> class Pad {
public:
  Pad() noexcept;
  ~Pad() noexcept;
  Pad(const Pad &) = delete;
  Pad &operator=(const Pad &) = delete;
  Pad(Pad &&other)
      : m_position{std::exchange(other.m_position, {})},
        m_radius{std::exchange(other.m_radius, {})},
        m_normalAudio{std::exchange(other.m_normalAudio, {})},
        m_alternate{std::exchange(other.m_alternate, {})} {}
  Pad &operator=(Pad &&other) {
    if (this != &other) {
      m_position = std::exchange(other.m_position, {});
      m_radius = std::exchange(other.m_radius, {});
      m_normalAudio = std::exchange(other.m_normalAudio, {});
      m_alternate = std::exchange(other.m_alternate, {});
    }
    return *this;
  }

  inline bool inside(const Coordinates &coords) const noexcept { return false; }

private:
  Coordinates m_position{};
  float m_radius{};
  mp3File m_normalAudio{};
  Alternate<Type, hasAlternate> m_alternate{};
};

using StickPlayable =
    std::variant<Pad<PadType::Drum, true>, Pad<PadType::Drum, false>,
                 Pad<PadType::Cymbal, true>, Pad<PadType::Cymbal, false>>;

template <> class Pad<PadType::Pedal, true> {
public:
  Pad(const gpio_num_t pin) noexcept;
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
  gpio_num_t m_pin;
  bool m_pressed{false};
};

} // namespace bh

#endif
