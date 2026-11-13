#ifndef BH_AUDIO
#define BH_AUDIO

#include "Utils.hpp"
#include "driver/i2s_std.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

namespace bh {

class AudioPlayer {
public:
  AudioPlayer() noexcept = default;
  ~AudioPlayer() noexcept { stop(); }
  AudioPlayer(const AudioPlayer &) = delete;
  AudioPlayer &operator=(const AudioPlayer &) = delete;

  ::esp_err_t start() noexcept;
  // Task context only. Does not block. False means invalid audio, a stopped
  // player, or a full request queue. The file must outlive stop().
  bool play(const bh::mp3File &file) noexcept;
  // Waits for the worker before releasing I2S and borrowed PCM references.
  // Call start/stop and play from the same producer task.
  void stop() noexcept;

private:
  struct Request {
    const std::uint16_t *pcm;
    std::uint32_t samples;
  };
  static void taskEntry(void *arg) noexcept;
  void run() noexcept;

  ::i2s_chan_handle_t m_channel{};
  ::QueueHandle_t m_requests{};
  ::SemaphoreHandle_t m_finished{};
  ::TaskHandle_t m_task{};
  std::atomic<bool> m_running{false};
};

} // namespace bh

#endif
