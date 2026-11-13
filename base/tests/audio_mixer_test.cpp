#include "AudioMixer.hpp"

#include <array>
#include <cassert>
#include <cstdio>

int main() {
  const std::uint16_t hit[]{1000, 2000, 3000, 4000};
  std::array<std::int16_t, 4> out{};
  {
    bh::AudioMixer mixer{};
    mixer.render(out);
    assert((out == std::array<std::int16_t, 4>{0, 0, 0, 0}));
    assert(!mixer.add(nullptr, 4));
    assert(!mixer.add(hit, 0));
    assert(mixer.add(hit, 4));
    assert(mixer.add(hit, 4));
    mixer.render(out);
    assert((out == std::array<std::int16_t, 4>{2000, 4000, 6000, 8000}));
    mixer.render(out);
    assert((out == std::array<std::int16_t, 4>{0, 0, 0, 0}));
  }
  {
    bh::AudioMixer mixer{};
    assert(mixer.add(hit, 4));
    std::array<std::int16_t, 2> first{};
    mixer.render(first);
    assert(mixer.add(hit, 4));
    mixer.render(out);
    assert((out == std::array<std::int16_t, 4>{4000, 6000, 3000, 4000}));
  }
  {
    bh::AudioMixer mixer{};
    assert(mixer.add(hit, 4));
    std::array<std::int16_t, 6> tail{};
    mixer.render(tail);
    assert((tail == std::array<std::int16_t, 6>{1000, 2000, 3000, 4000, 0, 0}));
  }
  {
    bh::AudioMixer mixer{};
    const std::uint16_t loud[]{32767, 32768, 32767, 32768};
    for (std::size_t i = 0; i < bh::AudioMixer::maxVoices; ++i) {
      assert(mixer.add(loud, 4));
    }
    assert(!mixer.add(hit, 4));
    mixer.render(out);
    assert((out == std::array<std::int16_t, 4>{32767, -32768, 32767, -32768}));
    assert(mixer.add(hit, 4));
    mixer.render(out);
    assert((out == std::array<std::int16_t, 4>{1000, 2000, 3000, 4000}));
  }
  std::puts("Audio mixer tests passed");
}
