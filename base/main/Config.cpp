#include "Config.hpp"
#include "ButtonPress.hpp"
#include "Utils.hpp"

#include "esp_timer.h"
#include "freertos/projdefs.h"
#include "portmacro.h"

namespace bh {

bh::Config::Config(bh::AtomicState &state, bh::MAC &macAddress,
                   bh::Oled &oled) noexcept
    : m_state(state), m_macAddress(macAddress), m_oled(oled) {}

bh::Config::~Config() noexcept {}

void bh::Config::loop() noexcept {

  using Action = bh::ButtonPress::Action;

  bh::ButtonPress button;

  while (m_state.load(std::memory_order_relaxed) == bh::State::Config) {

    // GPIO notifications wake the task on either edge.
    (void)xTaskNotifyWait(0, UINT32_MAX, nullptr, 0);
    switch (button.update(
        ::gpio_get_level(bh::STATE_BUTTON_PIN) == 0,
        static_cast<std::uint32_t>(::esp_timer_get_time() / 1000))) {
    case Action::ShortPress: { // Change State
      ++m_selectedState;
      break;
    }
    case Action::LongPress: { // Confirm Selected State
      m_state.store(m_selectedState, std::memory_order_relaxed);
      return;
    }
    case Action::None: {
      break;
    }
    }

    m_oled.drawBase();
    m_oled.drawArrow(m_selectedState);
  }
}

} // namespace bh
