#ifndef BH_CONFIG
#define BH_CONFIG

#include "Oled.hpp"
#include "Utils.hpp"

namespace bh {

class Config {
public:
  explicit Config(bh::AtomicState &state, bh::MAC &macAddress,
                  bh::Oled &oled) noexcept;
  ~Config() noexcept;

  void loop() noexcept;

private:
  bh::AtomicState &m_state;
  bh::MAC &m_macAddress;
  bh::Oled &m_oled;

  bh::State m_selectedState{bh::State::PlayingFreestyle};
};

} // namespace bh

#endif
