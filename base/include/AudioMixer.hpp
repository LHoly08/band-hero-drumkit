#ifndef BH_AUDIO_MIXER
#define BH_AUDIO_MIXER

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

namespace bh {

// Single consumer: only the audio task touches the mixer. PCM is borrowed and
// must stay alive until playback stops. Each hit gets its own play position.
class AudioMixer {
public:
  static constexpr std::size_t maxVoices = 64;

  bool add(const std::uint16_t *pcm, std::uint32_t samples) noexcept {
    if (!pcm || !samples) {
      return false;
    }
    for (auto &voice : m_voices) {
      if (!voice.pcm) {
        voice = {pcm, samples, 0};
        return true;
      }
    }
    return false;
  }

  void render(std::span<std::int16_t> output) noexcept {
    for (auto &sample : output) {
      std::int32_t mixed = 0;
      for (auto &voice : m_voices) {
        if (!voice.pcm) {
          continue;
        }
        mixed += std::bit_cast<std::int16_t>(voice.pcm[voice.position++]);
        if (voice.position >= voice.samples) {
          voice.pcm = nullptr;
        }
      }
      sample = static_cast<std::int16_t>(
          std::clamp<std::int32_t>(mixed, -32768, 32767));
    }
  }

private:
  struct Voice {
    const std::uint16_t *pcm{};
    std::uint32_t samples{};
    std::uint32_t position{};
  };
  std::array<Voice, maxVoices> m_voices{};
};

} // namespace bh

#endif
