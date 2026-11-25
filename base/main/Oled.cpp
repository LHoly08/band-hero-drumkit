#include <meta>
#include <ranges>
#include <string_view>
#include <vector>

#include "Oled.hpp"

#include "Utils.hpp"
#include "esp32_hw_i2c.h"
#include "esp_err.h"
#include "esp_mac.h"

#include "driver/gpio.h"

namespace bh {

bh::Oled::Oled() noexcept {

  static ::u8g2_esp32_i2c_ctx_t i2c{};
  constexpr std::uint8_t oledAddress = 0x3D;

  i2c.cfg = U8G2_ESP32_I2C_CONFIG_DEFAULT();
#if CONFIG_IDF_TARGET_ESP32
  i2c.cfg.sda_pin = ::GPIO_NUM_21;
  i2c.cfg.scl_pin = ::GPIO_NUM_22;
#else
  i2c.cfg.sda_pin = ::GPIO_NUM_5; // D4
  i2c.cfg.scl_pin = ::GPIO_NUM_6; // D5
#endif
  i2c.cfg.clk_hz = 100000;
  i2c.cfg.dev_addr_7bit = oledAddress;
  i2c.cfg.reset_pin = U8G2_ESP32_PIN_UNUSED;

  ESP_ERROR_CHECK(::u8g2_esp32_i2c_set_default_context(&i2c));

  ::u8g2_Setup_sh1106_128x64_noname_f(&m_oled, U8G2_R0,
                                      ::u8x8_byte_esp32_hw_i2c,
                                      ::u8x8_gpio_and_delay_esp32_i2c);

  // The ESP driver takes a seven-bit address. u8g2 expects it shifted into the
  // address byte.
  u8g2_SetI2CAddress(&m_oled, oledAddress << 1);
  u8g2_InitDisplay(&m_oled);
  u8g2_SetPowerSave(&m_oled, 0);
}

void bh::Oled::drawArrow(bh::State selectedState) noexcept {

  ::u8g2_SetFont(&m_oled, ::u8g2_font_6x10_tf);

  const std::uint8_t line =
      static_cast<std::uint8_t>(selectedState) -
      static_cast<std::uint8_t>(bh::State::PlayingFreestyle);

  ::u8g2_DrawStr(&m_oled, 100, 10 + (line * 10), "<");
}

void bh::Oled::drawBase() noexcept {

  ::u8g2_SetFont(&m_oled, ::u8g2_font_6x10_tf);

  for (auto [i, text] : std::ranges::views::enumerate([] consteval -> auto {
         auto playingStates = std::meta::enumerators_of(^^bh::State) |
                              std::ranges::views::drop(2);
         std::vector<const char *> names;
         constexpr std::string_view prefix = "Playing";

         for (const auto &state : playingStates) {
           auto identifier = std::meta::identifier_of(state);
           names.push_back(std::define_static_string(
               identifier.substr(prefix.size())));
         }
         return std::define_static_array(names);
       }())) {
    ::u8g2_DrawStr(&m_oled, 2, 10 + (i * 10), text);
  }
}

} // namespace bh
