#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <new>

#include "Utils.hpp"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_audio_simple_dec.h"
#include "esp_mp3_dec.h"
#include "esp_ae_rate_cvt.h"

namespace bh {

void IRAM_ATTR modeButtonPressed(void *args) noexcept {
  if (args != nullptr && mode_button_debounce.accept()) {
    static_cast<bh::AtomicState *>(args)->store(bh::State::Config,
                                              std::memory_order_relaxed);
  }
}

bh::mp3File::mp3File(std::string_view file) noexcept {
  constexpr const char *tag = "mp3File";
  if (file.empty() || file.find('\0') != std::string_view::npos) {
    ESP_LOGE(tag, "Invalid file path");
    return;
  }
  // string_view need not be null terminated. Keep allocation failure noexcept.
  std::unique_ptr<char[]> path{new (std::nothrow) char[file.size() + 1]};
  if (!path) {
    ESP_LOGE(tag, "Cannot allocate file path");
    return;
  }
  std::memcpy(path.get(), file.data(), file.size());
  path[file.size()] = '\0';
  File input{path.get(), "rb"};
  if (!input) {
    ESP_LOGE(tag, "Cannot open %s", path.get());
    return;
  }

  // Register only MP3, once; other live decoders may share this registration.
  static const auto registration = ::esp_mp3_dec_register();
  if (registration != ESP_AUDIO_ERR_OK) {
    ESP_LOGE(tag, "Cannot register MP3 decoder: %d", registration);
    return;
  }
  esp_audio_simple_dec_cfg_t cfg{};
  cfg.dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
  esp_audio_simple_dec_handle_t handle{};
  const auto openResult = ::esp_audio_simple_dec_open(&cfg, &handle);
  if (openResult != ESP_AUDIO_ERR_OK) {
    ESP_LOGE(tag, "Cannot open MP3 decoder: %d", openResult);
    return;
  }
  std::unique_ptr<void, decltype(&::esp_audio_simple_dec_close)> decoder{
      handle, &::esp_audio_simple_dec_close};
  std::unique_ptr<void, decltype(&::esp_ae_rate_cvt_close)> resampler{
      nullptr, &::esp_ae_rate_cvt_close};
  using Buffer = std::unique_ptr<std::uint8_t, decltype(&::heap_caps_free)>;
  constexpr std::uint32_t inputSize = 4096;
  std::uint32_t frameSize = 4608; // 1152 samples * 2 channels * 16 bits.
  Buffer encoded{static_cast<std::uint8_t *>(
                     ::heap_caps_malloc(inputSize, MALLOC_CAP_8BIT)),
                 &::heap_caps_free};
  Buffer frame{static_cast<std::uint8_t *>(
                   ::heap_caps_malloc(frameSize, MALLOC_CAP_8BIT)),
               &::heap_caps_free};
  Buffer pcm{nullptr, &::heap_caps_free};
  if (!encoded || !frame) {
    ESP_LOGE(tag, "Cannot allocate decoder buffers");
    return;
  }

  std::uint32_t size = 0;
  std::uint32_t capacity = 0;
  esp_audio_simple_dec_info_t format{};
  for (;;) {
    const auto count = std::fread(encoded.get(), 1, inputSize, input.get());
    if (std::ferror(input.get())) {
      ESP_LOGE(tag, "Cannot read %s", path.get());
      return;
    }
    esp_audio_simple_dec_raw_t raw{};
    raw.buffer = encoded.get();
    raw.len = static_cast<std::uint32_t>(count);
    raw.eos = std::feof(input.get()) != 0;
    // Also call with empty input at EOF to flush any cached final frame.
    do {
      esp_audio_simple_dec_out_t out{};
      out.buffer = frame.get();
      out.len = frameSize;
      raw.consumed = 0;
      const auto result =
          ::esp_audio_simple_dec_process(decoder.get(), &raw, &out);
      if (result == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH &&
          out.needed_size > frameSize) {
        auto *larger = static_cast<std::uint8_t *>(::heap_caps_realloc(
            frame.get(), out.needed_size, MALLOC_CAP_8BIT));
        if (!larger) {
          ESP_LOGE(tag, "Cannot grow decoder frame buffer");
          return;
        }
        frame.release();
        frame.reset(larger);
        frameSize = out.needed_size;
        continue; // Retry the same input, as required by the codec API.
      }
      if (result != ESP_AUDIO_ERR_OK || raw.consumed > raw.len ||
          out.decoded_size > out.len) {
        ESP_LOGE(tag, "MP3 decode failed: %d", result);
        return;
      }
      if (out.decoded_size) {
        esp_audio_simple_dec_info_t info{};
        if (::esp_audio_simple_dec_get_info(decoder.get(), &info) !=
                ESP_AUDIO_ERR_OK ||
            info.bits_per_sample != 16 || !info.sample_rate ||
            (info.channel != 1 && info.channel != 2) ||
            out.decoded_size % (sizeof(std::uint16_t) * info.channel) != 0 ||
            (format.sample_rate &&
             (info.sample_rate != format.sample_rate ||
              info.channel != format.channel))) {
          ESP_LOGE(tag, "Unsupported or changing PCM format");
          return;
        }
        if (!format.sample_rate && info.sample_rate != bh::audioSampleRate) {
          esp_ae_rate_cvt_cfg_t rateConfig{};
          rateConfig.src_rate = info.sample_rate;
          rateConfig.dest_rate = bh::audioSampleRate;
          rateConfig.channel = 1;
          rateConfig.bits_per_sample = 16;
          rateConfig.complexity = 3;
          rateConfig.perf_type = ESP_AE_RATE_CVT_PERF_TYPE_MEMORY;
          esp_ae_rate_cvt_handle_t rateHandle{};
          const auto rateResult =
              ::esp_ae_rate_cvt_open(&rateConfig, &rateHandle);
          resampler.reset(rateHandle);
          if (rateResult != ESP_AE_ERR_OK || !resampler) {
            ESP_LOGE(tag, "Cannot resample MP3 from %lu to %lu Hz: %d",
                     static_cast<unsigned long>(info.sample_rate),
                     static_cast<unsigned long>(bh::audioSampleRate), rateResult);
            return;
          }
        }
        format = info;
        // MP3 decodes to signed 16-bit samples. Downmix stereo in place using
        // a wider accumulator so adding two full-scale samples cannot overflow.
        const auto monoSize = out.decoded_size / info.channel;
        if (info.channel == 2) {
          for (std::uint32_t offset = 0; offset < monoSize;
               offset += sizeof(std::int16_t)) {
            std::int16_t stereo[2];
            std::memcpy(stereo, out.buffer + offset * 2, sizeof(stereo));
            const auto mono = static_cast<std::int16_t>(
                (static_cast<std::int32_t>(stereo[0]) + stereo[1]) / 2);
            std::memcpy(out.buffer + offset, &mono, sizeof(mono));
          }
        }
        constexpr auto maxSize = std::numeric_limits<std::uint32_t>::max();
        constexpr std::uint32_t sampleBytes = sizeof(std::int16_t);
        const auto inputSamples = monoSize / sampleBytes;
        std::uint32_t outputSamples = inputSamples;
        if (resampler &&
            ::esp_ae_rate_cvt_get_max_out_sample_num(
                resampler.get(), inputSamples, &outputSamples) != ESP_AE_ERR_OK) {
          ESP_LOGE(tag, "Cannot determine resampled frame size");
          return;
        }
        if (!outputSamples ||
            outputSamples > (maxSize - size) / sampleBytes) {
          ESP_LOGE(tag, "Decoded PCM is too large");
          return;
        }
        const auto required = size + outputSamples * sampleBytes;
        if (required > capacity) {
          const auto nextCapacity = std::max(
              required, capacity == 0 ? 16384u
                                     : (capacity > maxSize / 2 ? maxSize
                                                               : capacity * 2));
          auto *larger = static_cast<std::uint8_t *>(::heap_caps_realloc(
              pcm.get(), nextCapacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
          if (!larger) {
            ESP_LOGE(tag, "Not enough PSRAM for decoded PCM");
            return;
          }
          pcm.release();
          pcm.reset(larger);
          capacity = nextCapacity;
        }
        if (resampler) {
          // Convert one decoded frame at a time, directly into PSRAM. Keep
          // filter state between frames; never retain the full source-rate PCM.
          const auto maxOutputSamples = outputSamples;
          const auto rateResult = ::esp_ae_rate_cvt_process(
              resampler.get(), out.buffer, inputSamples, pcm.get() + size,
              &outputSamples);
          if (rateResult != ESP_AE_ERR_OK || outputSamples > maxOutputSamples) {
            ESP_LOGE(tag, "MP3 resampling failed: %d", rateResult);
            return;
          }
          size += outputSamples * sampleBytes;
        } else {
          std::memcpy(pcm.get() + size, out.buffer, monoSize);
          size = required;
        }
      }
      raw.buffer += raw.consumed;
      raw.len -= raw.consumed;
      if (!raw.consumed && !out.decoded_size) {
        if (raw.len) {
          ESP_LOGE(tag, "MP3 decoder made no progress");
          return;
        }
        break;
      }
      // Drain cached output at EOF until the decoder stops producing PCM.
    } while (raw.len || raw.eos);
    if (raw.eos) {
      break;
    }
  }
  if (!size) {
    ESP_LOGE(tag, "File contains no decodable MP3 audio");
    return;
  }
  // Shrinking is optional; keep the original allocation if it fails.
  if (auto *smaller = static_cast<std::uint8_t *>(::heap_caps_realloc(
          pcm.get(), size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))) {
    pcm.release();
    pcm.reset(smaller);
  }
  m_file = reinterpret_cast<std::uint16_t *>(pcm.release());
  m_size = size;
}

mp3File &mp3File::operator=(mp3File &&other) noexcept {
  if (this != &other) {
    ::heap_caps_free(m_file);
    m_file = std::exchange(other.m_file, nullptr);
    m_size = std::exchange(other.m_size, 0);
  }
  return *this;
}

bh::mp3File::~mp3File() noexcept {
  if (m_file) {
    ::heap_caps_free(m_file);
  }
}

} // namespace bh
