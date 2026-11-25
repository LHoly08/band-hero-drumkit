#include "Config.hpp"

#include "ButtonPress.hpp"
#include "Utils.hpp"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"

namespace bh {

#if BH_OLED
bh::Config::Config(bh::AtomicState &state, bh::KitConfig &kit,
                   bh::Oled &oled) noexcept
    : m_state(state), m_oled(oled), m_server(kit) {

  showSelection();
}
#else
bh::Config::Config(bh::AtomicState &state, bh::KitConfig &kit) noexcept
    : m_state(state), m_server(kit) {
  m_server.drawServerInfo();
  showSelection();
}
#endif

void bh::Config::showSelection() noexcept {
#if BH_OLED
  m_oled.clearBuffer();
  m_oled.drawBase();
  m_server.drawServerInfo(m_oled);
  m_oled.drawArrow(m_selectedState);
  m_oled.sendBuffer();
#else
  std::puts(bh::stateName(m_selectedState));
#endif
}

bh::Config::~Config() noexcept {}

void bh::Config::loop() noexcept {

  using Action = bh::ButtonPress::Action;

  ::gpio_config_t input{};
  input.pin_bit_mask = 1ULL << bh::STATE_BUTTON_PIN;
  input.mode = GPIO_MODE_INPUT;
  input.pull_up_en = GPIO_PULLUP_ENABLE;
  input.intr_type = GPIO_INTR_DISABLE;
  ESP_ERROR_CHECK(::gpio_config(&input));

  bh::ButtonPress button;
  constexpr auto pollTicks = pdMS_TO_TICKS(bh::BUTTON_POLL_MS);

  while (m_state.load(std::memory_order_relaxed) == bh::State::Config) {

    ::vTaskDelay(pollTicks > 0 ? pollTicks : 1);
    switch (button.update(
        ::gpio_get_level(bh::STATE_BUTTON_PIN) == 0,
        static_cast<std::uint32_t>(::esp_timer_get_time() / 1000))) {
    case Action::ShortPress: { // Change State
      ++m_selectedState;

      showSelection();
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
    ::vTaskDelay(pdMS_TO_TICKS(10));
  }
}

} // namespace bh
