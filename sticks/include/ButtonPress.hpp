#pragma once

#include <cstdint>

namespace bh {

constexpr std::uint32_t BUTTON_DEBOUNCE_MS = 10;
constexpr std::uint32_t BUTTON_LONG_PRESS_MS = 2000;

// Times are unsigned milliseconds so subtraction also handles wraparound.
class ButtonPress {
public:
  enum class Action { None, Calibrate, Sleep };

  Action update(bool pressed, std::uint32_t now) noexcept {
    if (pressed != m_raw_pressed) {
      m_raw_pressed = pressed;
      m_raw_since = now;
    }
    // Require both edges to remain stable; release bounce must not end a hold.
    if (pressed == m_pressed ||
        std::uint32_t(now - m_raw_since) < BUTTON_DEBOUNCE_MS) {
      return Action::None;
    }
    m_pressed = pressed;
    if (pressed) {
      m_press_ms = m_raw_since;
      return Action::None;
    }
    return std::uint32_t(m_raw_since - m_press_ms) >= BUTTON_LONG_PRESS_MS
               ? Action::Sleep
               : Action::Calibrate;
  }

private:
  std::uint32_t m_raw_since{};
  std::uint32_t m_press_ms{};
  bool m_raw_pressed{};
  bool m_pressed{};
};

} // namespace bh
