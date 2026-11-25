#include "Playing.hpp"

#include <algorithm>
#include <array>
#include <type_traits>

namespace bh {

void setup(void *context, bh::State state, const KitConfig &kit, bool *error,
           const char *&errorString) {
  *error = false;
  errorString = nullptr;
  auto fail = [&](const char *message) {
    *error = true;
    errorString = message;
  };
  if (!context) {
    fail("Invalid setup context");
    return;
  }
  std::variant<Play<bh::State::PlayingFreestyle> *,
               Play<bh::State::PlayingHub> *, Play<bh::State::PlayingSerial> *>
      play;
  switch (state) {
  case bh::State::PlayingFreestyle:
    play = static_cast<Play<bh::State::PlayingFreestyle> *>(context);
    break;
  case bh::State::PlayingHub:
    play = static_cast<Play<bh::State::PlayingHub> *>(context);
    break;
  case bh::State::PlayingSerial:
    play = static_cast<Play<bh::State::PlayingSerial> *>(context);
    break;
  default:
    break;
  }

  const auto result = ::gpio_install_isr_service(0);
  if (result != ESP_ERR_INVALID_STATE) {
    ESP_ERROR_CHECK(result);
  }
  ::wifi_init_config_t wifi = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(::esp_wifi_init(&wifi));
  ESP_ERROR_CHECK(::esp_wifi_set_mode(::WIFI_MODE_STA));
  ESP_ERROR_CHECK(::esp_wifi_start());
  ESP_ERROR_CHECK(::esp_now_init());

  using ISR_CFG = bh::OppositeEdge<true>;
  ::gpio_config_t cfg{};
  cfg.pin_bit_mask = 1ULL << bh::PIN;
  cfg.mode = ::GPIO_MODE_INPUT;
  cfg.pull_up_en = static_cast<::gpio_pullup_t>(ISR_CFG::pull_up);
  cfg.pull_down_en = static_cast<::gpio_pulldown_t>(ISR_CFG::pull_down);
  cfg.intr_type = static_cast<::gpio_int_type_t>(ISR_CFG::intr_type);
  ESP_ERROR_CHECK(::gpio_config(&cfg));
  std::visit(
      [&](auto *selected) {
        selected->instance = selected;
        ESP_ERROR_CHECK(::esp_now_register_recv_cb(selected->ReceivedCallback));

        if constexpr (std::is_same_v<std::remove_pointer_t<decltype(selected)>,
                                     Play<bh::State::PlayingHub>>) {
          ::esp_now_peer_info_t peer{};
          std::copy(kit.macAddress.begin(),
                    kit.macAddress.end(), peer.peer_addr);
          peer.ifidx = ::WIFI_IF_STA;
          if (::esp_now_add_peer(&peer) != ESP_OK) {
            fail("Cannot register hub peer; check MAC configuration");
          }
        }
      },
      play);
  if (*error) {
    return;
  }

  // Keep Lua order within each group. Overlapping stick pads use the first match,
  // and pedal order sets their bit positions.
  for (const auto &pad : kit.pads) {
    bh::mp3File audio;
    if (state == bh::State::PlayingFreestyle) {
      audio = bh::mp3File{pad.audio};
    }
    if (state == bh::State::PlayingFreestyle && !audio.getFile()) {
      fail("Cannot load pad audio");
      return;
    }
    if (pad.type == bh::PadType::Pedal) {
#if CONFIG_IDF_TARGET_ESP32
      static constexpr std::array pins{::GPIO_NUM_25, ::GPIO_NUM_26, ::GPIO_NUM_27,
                                       ::GPIO_NUM_32};
#else
      static constexpr std::array pins{::GPIO_NUM_1, ::GPIO_NUM_2, ::GPIO_NUM_3,
                                       ::GPIO_NUM_4};
#endif
      std::visit(
          [&](auto *selected) {
            selected->m_pedals.emplace_back(pins[pad.pedalIndex],
                                            std::move(audio));
          },
          play);
    } else {
      bh::mp3File alternateAudio;
      if (pad.hasAlternate && state == bh::State::PlayingFreestyle) {
        alternateAudio = bh::mp3File{pad.alternateAudio};
        if (!alternateAudio.getFile()) {
          fail("Cannot load alternate audio");
          return;
        }
      }
      template for (constexpr auto type : std::define_static_array(
                        std::meta::enumerators_of(^^bh::PadType) |
                        std::views::take(2))) {
        constexpr bh::PadType Pad_t = [:type:];
        if (pad.type == Pad_t) {
          if (pad.hasAlternate) {
            bh::Alternate<Pad_t, true> alternate{pad.alternateRadius *
                                                     pad.alternateRadius,
                                                 std::move(alternateAudio)};
            bh::Pad<Pad_t, true> value{pad.position, pad.radius, pad.tilt,
                                       std::move(audio), std::move(alternate)};
            std::visit(
                [&value](auto *selected) {
                  selected->m_sticks.emplace_back(std::move(value));
                },
                play);
          } else {
            bh::Pad<Pad_t, false> value{pad.position, pad.radius, pad.tilt,
                                        std::move(audio)};
            std::visit(
                [&value](auto *selected) {
                  selected->m_sticks.emplace_back(std::move(value));
                },
                play);
          }
        }
      }
    }
  }
}

} // namespace bh
