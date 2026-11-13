#ifndef BH_PLAY
#define BH_PLAY

#include "Pads.hpp"
#include "Utils.hpp"

#include "esp_attr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "lua.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <inplace_vector>
#include <lua.hpp>
#include <meta>
#include <ranges>
#include <string_view>
#include <utility>
#include <variant>

namespace bh {

template <bh::State S>
concept Playing =
    S == bh::State::PlayingSerial || S == bh::State::PlayingFreestyle ||
    S == bh::State::PlayingHub;

namespace detail {

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

} // namespace detail

// Keep setup shared between playing modes.
[[gnu::noinline]]
void setup(void *context, bh::State state, ::lua_State *luaFile, bool *error,
           const char *&errorString);

template <bh::State S>
  requires Playing<S>
class Play {
public:
  Play(detail::NeededReferences<S> references, ::lua_State *luaFile,
       bool *error, const char *&errorString) noexcept;
  ~Play() noexcept;

  void loop() noexcept;

private:
  static Play<S> *instance;

  static void ReceivedCallback(const ::esp_now_recv_info_t *esp_now_info,
                               const std::uint8_t *data, int data_len) noexcept;

  friend void setup(void *context, bh::State state, ::lua_State *luaFile,
                    bool *error, const char *&errorString);

  detail::NeededReferences<S> m_references;
  std::uint32_t m_note{};

  using Packet_t = detail::RequiredPacketType<S>::Packet_t;
  bh::Queue<Packet_t, 5> m_queue;
  std::inplace_vector<bh::Pad<bh::PadType::Pedal, false>, 4> m_pedals;
  std::inplace_vector<bh::StickPlayable, 30> m_sticks;
  // Declared last: stops the worker before any pad PCM can be destroyed.
  bh::AudioPlayer m_audio;
};

template <bh::State S>
  requires Playing<S>
Play<S> *Play<S>::instance = nullptr;

// ----- Definitions -----

template <bh::State S>
  requires Playing<S>
Play<S>::Play(detail::NeededReferences<S> references, ::lua_State *luaFile,
              bool *error, const char *&errorString) noexcept
    : m_references{references} {
  assert(error != nullptr);

  if constexpr (S == bh::State::PlayingFreestyle) {
    const auto result = m_audio.start();

    if (result != ESP_OK) {
      *error = true;
      errorString = "Cannot start audio";
      ESP_LOGE("Play", "Cannot start audio: %s", ::esp_err_to_name(result));
      return;
    }
  }

  setup(this, S, luaFile, error, errorString);
}

template <bh::State S>
  requires Playing<S>
Play<S>::~Play() noexcept {
  if (instance == this) {
    ::esp_now_unregister_recv_cb();
    ::esp_now_deinit();
    ::esp_wifi_stop();
    ::esp_wifi_deinit();
    ::gpio_isr_handler_remove(bh::PIN);
    instance = nullptr;
  }
  if constexpr (S == bh::State::PlayingFreestyle) {
    m_audio.stop();
  }
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

    if constexpr (S == bh::State::PlayingFreestyle) {
      for (const auto &pedal : m_pedals) {
        pedal.play(m_audio);
      }
    }

    if (Packet_t packet{}; m_queue.pop(&packet)) [[likely]] {

      if constexpr (S != bh::State::PlayingFreestyle) {

        const std::uint8_t pedalsSize = m_pedals.size();

        std::uint32_t val = m_note;

        for (const auto &[i, pedal] :
             std::as_const(m_pedals) | std::ranges::views::enumerate) {
          val |= ((1u << i) * pedal.pressed());
        }

        for (const auto &[i, pad] :
             std::as_const(m_sticks) | std::ranges::views::enumerate) {
          const bool isInside = std::visit(
              [&packet](const auto &pad) -> bool {
                return pad.inside(packet, nullptr);
              },
              pad);
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

            (void)::esp_now_send(m_references.macAddress.data(),
                                 reinterpret_cast<const std::uint8_t *>(&val),
                                 sizeof(val));
          }
          m_note = val;
        }
      } else { // ----- Play Freestyle -----

        for (const auto &pad : m_sticks) {
          const bool isInside = std::visit(
              [this, &packet](const auto &pad) -> bool {
                float radialSquared{};
                const bool hit = pad.inside(packet.coords, &radialSquared);

                if (hit) {
                  pad.play(m_audio, radialSquared, packet.speed);
                }
                return hit;
              },
              pad);
          if (isInside) {
            break;
          }
        }
      }
    }
  }
}

// ----- End of Definitions -----

} // namespace bh

#endif
