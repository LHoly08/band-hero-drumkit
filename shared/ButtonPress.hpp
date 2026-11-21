#pragma once

#include <cstdint>

namespace bh {

constexpr std::uint32_t BUTTON_DEBOUNCE_MS = 10;
constexpr std::uint32_t BUTTON_LONG_PRESS_MS = 2000;

// Times are unsigned milliseconds so subtraction also handles wraparound.
class ButtonPress {
public:
  enum class Action { None, LongPress, ShortPress };

  Action update(bool pressed, std::uint32_t now) noexcept {
    if (pressed != ((m_flags & RAW_PRESSED) != 0)) {
      m_flags ^= RAW_PRESSED;
      m_raw_since = now;
    }
    // Require both edges to remain stable; release bounce must not end a hold.
    if (pressed == ((m_flags & DEBOUNCED_PRESSED) != 0) ||
        std::uint32_t(now - m_raw_since) < BUTTON_DEBOUNCE_MS) {
      return Action::None;
    }
    m_flags ^= DEBOUNCED_PRESSED;
    if (pressed) {
      m_press_ms = m_raw_since;
      return Action::None;
    }
    return std::uint32_t(m_raw_since - m_press_ms) >= BUTTON_LONG_PRESS_MS
               ? Action::LongPress
               : Action::ShortPress;
  }

private:
  std::uint32_t m_raw_since{};
  std::uint32_t m_press_ms{};
  static constexpr std::uint8_t RAW_PRESSED = 1u << 0;
  static constexpr std::uint8_t DEBOUNCED_PRESSED = 1u << 1;
  std::uint8_t m_flags{};
};

} // namespace bh
