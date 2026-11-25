#ifndef BH_CONFIG
#define BH_CONFIG

#include "Utils.hpp"
#include "Web.hpp"

namespace bh {

class Config {
public:
#if BH_OLED
  explicit Config(bh::AtomicState &state, bh::KitConfig &kit,
                  bh::Oled &oled) noexcept;
#else
  explicit Config(bh::AtomicState &state, bh::KitConfig &kit) noexcept;
#endif
  ~Config() noexcept;

  void loop() noexcept;

private:
  void showSelection() noexcept;

  bh::AtomicState &m_state;
#if BH_OLED
  bh::Oled &m_oled;
#endif

  bh::State m_selectedState{bh::State::PlayingFreestyle};
  bh::Web m_server;
};

} // namespace bh

#endif
