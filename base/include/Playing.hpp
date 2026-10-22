#ifndef BH_PLAY
#define BH_PLAY

#include "Playable.hpp"
#include "Utils.hpp"

#include "esp_err.h"

#include <cstdint>
#include <cstdio>
#include <inplace_vector>

namespace bh {

namespace {

template <bh::State S>
concept Playing =
    S == bh::State::PlayingSerial || S == bh::State::PlayingFreestyle ||
    S == bh::State::PlayingHub;

template <bh::State S>
  requires Playing<S>
struct NeededReferences;

template <bh::State S>
  requires Playing<S>
struct NeededReferences<S> {
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

template <> struct RequiredQueue<bh::State::PlayingFreestyle> {
  using Packet_t = bh::Packet;
};

} // namespace

template <bh::State S>
  requires Playing<S>
class Play {
public:
  Play() noexcept;
  ~Play() noexcept;

  void loop() noexcept;

private:
  static Play<S> *instance;

  static void ReceivedCallback(const ::esp_now_recv_info_t *esp_now_info,
                               const std::uint8_t *data, int data_len) noexcept;

  static void IRAM_ATTR ButtonPressed(void *args) noexcept {
    if (args != nullptr && mode_button_debounce.accept()) {
      static_cast<bh::AtomicState *>(args)->store(bh::State::Configuring,
                                                  std::memory_order_relaxed);
    }
  }
  NeededReferences m_references;
  std::uint32_t m_note;

  using Packet_t = RequiredQueue<S>::Packet_t;
  bh::Queue<Packet_t, 5> m_queue;
  std::inplace_vector<bh::Pad<bh::PadType::Pedal>, 4> m_pedals;
  std::inplace_vector<bh::StickPlayable, 30> m_sticks;
};

template <bh::State S>
  requires Playing<S>
Play<S> *Play<S>::instance = nullptr;

template <bh::State S>
  requires Playing<S>
void Play<S>::ReceivedCallback(const ::esp_now_recv_info_t *esp_now_info,
                               const std::uint8_t *data,
                               int data_len) noexcept {
  if (instance == nullptr || esp_now_info == nullptr ||
      esp_now_info->src_addr == nullptr || data == nullptr ||
      data_len != sizeof(Packet_t)) {
    return;
  }

  Packet_t packet;
  std::memcpy(&packet, data, data_len);

  instance->m_queue.push(packet);
}

template <bh::State S>
  requires Playing<S>
void Play<S>::loop() noexcept {
  std::uint32_t val = m_note;

  for (std::uint8_t i{}; i < m_pedals.size(); ++i) {
    val |= ((1u << i) * m_pedals[i].pressed());
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

        status = ::esp_now_send(m_references.macAddress.data(),
                                reinterpret_cast<const std::uint8_t *>(&val),
                                sizeof(val));
      }

    } else if constexpr (S == bh::State::PlayingFreestyle) {
      // Play Audio File
    }
    m_note = val;
  }
}

} // namespace bh

#endif
