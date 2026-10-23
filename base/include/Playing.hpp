#ifndef BH_PLAY
#define BH_PLAY

#include "Pads.hpp"
#include "Utils.hpp"

#include "esp_err.h"
#include "esp_now.h"
#include "esp_wifi.h"

#include <cstdint>
#include <cstdio>
#include <inplace_vector>
#include <lua.hpp>

namespace bh {

template <bh::State S>
concept Playing =
    S == bh::State::PlayingSerial || S == bh::State::PlayingFreestyle ||
    S == bh::State::PlayingHub;

namespace {

template <bh::State S>
  requires Playing<S>
struct NeededReferences {
  bh::AtomicState &state;
};

template <> struct NeededReferences<bh::State::PlayingHub> {
  bh::AtomicState &state;
  const bh::MAC &macAddress;
};

template <bh::State S>
  requires Playing<S>
struct RequiredPacketType {
  using Packet_t = bh::Vec3;
};

template <> struct RequiredPacketType<bh::State::PlayingFreestyle> {
  using Packet_t = bh::Packet;
};

} // namespace

template <bh::State S>
  requires Playing<S>
class Play {
public:
  Play(NeededReferences<S> references, lua_State *luaFile,
       bool *error) noexcept;
  ~Play() noexcept = default;

  void loop() noexcept;

private:
  static Play<S> *instance;

  static void ReceivedCallback(const ::esp_now_recv_info_t *esp_now_info,
                               const std::uint8_t *data, int data_len) noexcept;

  NeededReferences<S> m_references;
  std::uint32_t m_note;

  using Packet_t = RequiredPacketType<S>::Packet_t;
  bh::Queue<Packet_t, 5> m_queue;
  std::inplace_vector<bh::Pad<bh::PadType::Pedal, false>, 4> m_pedals;
  std::inplace_vector<bh::StickPlayable, 30> m_sticks;
};

template <bh::State S>
  requires Playing<S>
Play<S> *Play<S>::instance = nullptr;

// ----- Definitions -----

template <bh::State S>
  requires Playing<S>
Play<S>::Play(NeededReferences<S> references, lua_State *luaFile,
              bool *error) noexcept
    : m_references{references} {
  assert(error != nullptr);

  if (true) {
  }

  *error = true;

  if constexpr (S == bh::State::PlayingHub) {
    ESP_ERROR_CHECK(::gpio_install_isr_service(ESP_INTR_FLAG_IRAM));
    {
      ::wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
      ESP_ERROR_CHECK(::esp_wifi_init(&cfg));
      ESP_ERROR_CHECK(::esp_wifi_set_mode(::WIFI_MODE_STA));
      ESP_ERROR_CHECK(::esp_wifi_start());

      ESP_ERROR_CHECK(::esp_now_init());
    }
  }
  {
    using ISR_CFG = bh::OppositeEdge<true>;

    ::gpio_config_t cfg;

    cfg.pin_bit_mask = 1ULL << bh::PIN;
    cfg.mode = ::GPIO_MODE_INPUT;
    cfg.pull_up_en = (gpio_pullup_t)ISR_CFG::pull_up;
    cfg.pull_down_en = (gpio_pulldown_t)ISR_CFG::pull_down;
    cfg.intr_type = (gpio_int_type_t)ISR_CFG::intr_type;

    ESP_ERROR_CHECK(::gpio_config(&cfg));
  }

  instance = this;

  ESP_ERROR_CHECK(
      ::gpio_isr_handler_add(bh::PIN, modeButtonPressed, &m_references.state));
  ESP_ERROR_CHECK(::esp_now_register_recv_cb(ReceivedCallback));
}

template <bh::State S>
  requires Playing<S>
void Play<S>::ReceivedCallback(const ::esp_now_recv_info_t *esp_now_info,
                               const std::uint8_t *data,
                               int data_len) noexcept {
  if (instance == nullptr || esp_now_info == nullptr ||
      esp_now_info->src_addr == nullptr || data == nullptr ||
      data_len != sizeof(bh::Packet)) {
    return;
  }

  Packet_t packet;
  std::memcpy(&packet, data, sizeof(packet));

  instance->m_queue.push(packet);
}

template <bh::State S>
  requires Playing<S>
void Play<S>::loop() noexcept {

  while (m_references.state.load(std::memory_order_relaxed) == S) {

    if (Packet_t packet{}; m_queue.pop(&packet)) [[likely]] {

      if constexpr (S != bh::State::PlayingFreestyle) {
        const std::uint8_t pedalsSize = m_pedals.size();

        std::uint32_t val = m_note;

        for (std::uint8_t i{}; i < pedalsSize; ++i) {
          val |= ((1u << i) * m_pedals[i].pressed());
        }

        for (std::uint8_t i{}; i < m_sticks.size(); ++i) {
          const bool isInside = std::visit(
              [&packet](const auto &pad) -> bool {
                return pad.inside(packet, nullptr);
              },
              m_sticks[i]);
          if (isInside) {
            val |= (1u << (i + pedalsSize));
            break;
          }
        }

        if (val != m_note) {
          if constexpr (S == bh::State::PlayingSerial) {

            std::fwrite(&val, 1, sizeof(val), stdout);
            std::fflush(stdout);

          } else if constexpr (S == bh::State::PlayingHub) {

            ::esp_err_t status = ::esp_now_send(
                m_references.macAddress.data(),
                reinterpret_cast<const std::uint8_t *>(&val), sizeof(val));

            for (unsigned char i{}; i < 4 && status != ESP_OK; ++i) {

              status = ::esp_now_send(
                  m_references.macAddress.data(),
                  reinterpret_cast<const std::uint8_t *>(&val), sizeof(val));
            }
          }
          m_note = val;
        }
      } else { // ----- Play Freestyle -----

        for (const auto &pedal : m_pedals) {
          pedal.play();
        }

        for (const auto &pad : m_sticks) {
          const bool isInside = std::visit(
              [&packet](const auto &pad) -> bool {
                float radialSquared{};
                const bool hit = pad.inside(packet.coords, &radialSquared);

                if (hit) {
                  pad.play(radialSquared, packet.speed);
                }
                return hit;
              },
              pad);
          if (isInside) {
            break;
          }
        }
        // Play Audio File
      }
    }
  }
}

// ----- End of Definitions -----

} // namespace bh

#endif
