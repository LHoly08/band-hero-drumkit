#include "Audio.hpp"
#include "AudioMixer.hpp"

#include "esp_log.h"

#include <array>

namespace bh {

namespace {
constexpr const char *tag = "AudioPlayer";
constexpr std::size_t blockSamples = 128;
}

::esp_err_t AudioPlayer::start() noexcept {
  if (m_channel || m_task) {
    return ESP_ERR_INVALID_STATE;
  }
#if CONFIG_FREERTOS_UNICORE
  ESP_LOGE(tag, "Audio requires core 1; disable CONFIG_FREERTOS_UNICORE");
  return ESP_ERR_NOT_SUPPORTED;
#else
  m_requests = xQueueCreate(bh::AudioMixer::maxVoices, sizeof(Request));
  m_finished = xSemaphoreCreateBinary();
  if (!m_requests || !m_finished) {
    stop();
    return ESP_ERR_NO_MEM;
  }

  ::i2s_chan_config_t channelConfig{};
  channelConfig.id = I2S_NUM_AUTO;
  channelConfig.role = I2S_ROLE_MASTER;
  channelConfig.dma_desc_num = 4;
  channelConfig.dma_frame_num = blockSamples;
  channelConfig.auto_clear = true;
  channelConfig.tx_destination = I2S_DESTINATION_DMA;
  channelConfig.rx_destination = I2S_DESTINATION_DMA;
  auto result = ::i2s_new_channel(&channelConfig, &m_channel, nullptr);
  if (result == ESP_OK) {
    ::i2s_std_config_t standardConfig{};
    standardConfig.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(bh::audioSampleRate);
    standardConfig.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    // The S3 duplicates each mono sample into both PCM5102 output slots.
    standardConfig.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
    standardConfig.gpio_cfg.mclk = GPIO_NUM_NC;
    standardConfig.gpio_cfg.bclk = GPIO_NUM_16;
    standardConfig.gpio_cfg.ws = GPIO_NUM_17;
    standardConfig.gpio_cfg.dout = GPIO_NUM_18;
    standardConfig.gpio_cfg.din = GPIO_NUM_NC;
    result = ::i2s_channel_init_std_mode(m_channel, &standardConfig);
  }
  if (result == ESP_OK) {
    result = ::i2s_channel_enable(m_channel);
  }
  if (result != ESP_OK) {
    stop();
    return result;
  }
  m_running.store(true, std::memory_order_release);
  if (::xTaskCreatePinnedToCore(taskEntry, "audio", 6144, this, 5, &m_task, 1) !=
      pdPASS) {
    m_task = nullptr;
    stop();
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
#endif
}

bool AudioPlayer::play(const bh::mp3File &file) noexcept {
  if (!m_running.load(std::memory_order_acquire) || !m_requests ||
      !file.getFile() || !file.getSize() || file.getSize() % 2) {
    return false;
  }
  const Request request{file.getFile(), file.getSize() / 2};
  if (xQueueSend(m_requests, &request, 0) != pdTRUE) {
    ESP_LOGW(tag, "Audio request queue is full");
    return false;
  }
  return true;
}

void AudioPlayer::stop() noexcept {
  m_running.store(false, std::memory_order_release);
  if (m_task) {
    xSemaphoreTake(m_finished, portMAX_DELAY);
    m_task = nullptr;
  }
  if (m_channel) {
    ::i2s_channel_disable(m_channel);
    ::i2s_del_channel(m_channel);
    m_channel = nullptr;
  }
  if (m_requests) {
    ::vQueueDelete(m_requests);
    m_requests = nullptr;
  }
  if (m_finished) {
    vSemaphoreDelete(m_finished);
    m_finished = nullptr;
  }
}

void AudioPlayer::taskEntry(void *arg) noexcept {
  auto *player = static_cast<AudioPlayer *>(arg);
  player->run();
  const auto finished = player->m_finished;
  xSemaphoreGive(finished);
  ::vTaskDelete(nullptr);
}

void AudioPlayer::run() noexcept {
  bh::AudioMixer mixer{};
  std::array<std::int16_t, blockSamples> output{};
  while (m_running.load(std::memory_order_acquire)) {
    // Bound request processing so producers cannot starve the I2S writer.
    for (std::size_t i = 0; i < bh::AudioMixer::maxVoices; ++i) {
      Request request{};
      if (::xQueueReceive(m_requests, &request, 0) != pdTRUE) {
        break;
      }
      if (!mixer.add(request.pcm, request.samples)) {
        ESP_LOGW(tag, "All 64 audio voices are busy; hit dropped");
      }
    }
    mixer.render(output);
    std::size_t offset = 0;
    while (offset < sizeof(output) &&
           m_running.load(std::memory_order_acquire)) {
      std::size_t written = 0;
      const auto result = ::i2s_channel_write(
          m_channel, reinterpret_cast<const std::uint8_t *>(output.data()) +
                         offset,
          sizeof(output) - offset, &written, 50);
      offset += written;
      if (result != ESP_OK && result != ESP_ERR_TIMEOUT) {
        ESP_LOGE(tag, "I2S write failed: %s", ::esp_err_to_name(result));
        m_running.store(false, std::memory_order_release);
      } else if (!written) {
        ::vTaskDelay(1);
      }
    }
  }
}

} // namespace bh
