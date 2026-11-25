#ifndef BH_OLED_HPP
#define BH_OLED_HPP

#include "Playing.hpp"
#include "Utils.hpp"
#include "u8g2.h"

#include <cstdint>
#include <meta>
#include <ranges>
#include <string>

namespace bh {

class Oled {
public:
  Oled() noexcept;
  ~Oled() noexcept = default;

  inline void clearBuffer() noexcept { ::u8g2_ClearBuffer(&m_oled); }
  inline void sendBuffer() noexcept { ::u8g2_SendBuffer(&m_oled); }

  void drawArrow(bh::State selectedState) noexcept;
  void drawBase() noexcept;
  inline void drawLoading() noexcept {
    ::u8g2_SetFont(&m_oled, ::u8g2_font_6x10_tf);
    ::u8g2_DrawStr(&m_oled, 2, 10, "Loading Config File");
  }
  inline void drawWeb(std::string_view ssid, std::string_view pass,
                      std::string_view ip) {
    ::u8g2_SetFont(&m_oled, ::u8g2_font_6x10_tf);
    ::u8g2_DrawStr(&m_oled, 2, 40, "SSID: ");
    ::u8g2_DrawStr(&m_oled, 32, 40, ssid.data());
    ::u8g2_DrawStr(&m_oled, 2, 48, "PASS: ");
    ::u8g2_DrawStr(&m_oled, 32, 48, pass.data());
    ::u8g2_DrawStr(&m_oled, 2, 56, "IP: ");
    ::u8g2_DrawStr(&m_oled, 20, 56, ip.data());
  }

  template <bh::State S>
    requires bh::Playing<S>
  void drawState() noexcept;

private:
  ::u8g2_t m_oled{};
};

template <bh::State S>
  requires bh::Playing<S>
void Oled::drawState() noexcept {
  ::u8g2_SetFont(&m_oled, ::u8g2_font_6x10_tf);

  ::u8g2_DrawStr(&m_oled, 2, 10, []<bh::State State>() consteval -> auto {
    constexpr std::string_view prefix = "Playing";

    for (const auto &info :
         std::meta::enumerators_of(^^bh::State) | std::ranges::views::drop(2)) {
      if (std::meta::extract<bh::State>(info) == State) {
        return std::define_static_string(
            std::meta::identifier_of(info).substr(prefix.size()));
      }
    }
    std::unreachable();
  }.template operator()<S>());
}

} // namespace bh

#endif
