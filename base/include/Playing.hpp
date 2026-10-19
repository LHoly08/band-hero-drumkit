#ifndef BH_PLAY
#define BH_PLAY

#include "Playable.hpp"
#include "Utils.hpp"
#include <cstdint>
#include <inplace_vector>

namespace bh {

template <State S>
concept Playing = S == State::PlayingSerial || S == State::PlayingFreestyle ||
                  S == State::PlayingHub;

template <State S>
  requires Playing<S>
class Play {
public:
  Play() noexcept;
  ~Play() noexcept;

  void loop() noexcept;

private:
  static Play<S> *instance;
  AtomicState &m_state;

  std::uint32_t m_note;

  std::inplace_vector<Pad<PadType::Pedal>, 4> m_pedals;
  std::inplace_vector<StickPlayable, 30> m_sticks;
};

template <State S>
  requires Playing<S>
Play<S> *Play<S>::instance = nullptr;

template <State S>
  requires Playing<S>
void Play<S>::loop() noexcept {
  std::uint32_t val = m_note;

  for (std::uint8_t i{}; i < m_pedals.size(); ++i) {
    val |= ((1u << i) * m_pedals[i].pressed());
  }

  if (val != m_note) {
    // Send Packet
    m_note = val;
  }
}

} // namespace bh

#endif
