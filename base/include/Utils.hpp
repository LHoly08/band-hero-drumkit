#ifndef BH_UTIL
#define BH_UTIL

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <meta>
#include <ranges>
#include <string_view>
#include <utility>

#include "HitPacket.hpp"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace bh {

enum class State : std::uint32_t {
  Config = 0,
  ConfigError,
  PlayingFreestyle,
  PlayingSerial,
  PlayingHub,
};

constexpr const char *stateName(State state) noexcept {
  template for (constexpr auto value : std::define_static_array(
                    std::meta::enumerators_of(^^State))) {
    if (state == [:value:]) {
      return std::define_static_string(std::meta::identifier_of(value));
    }
  }
  return "Unknown";
}

inline void operator++(State &s) {
  std::uint32_t value = static_cast<std::uint32_t>(s) + 1;
  constexpr std::uint32_t lastValue =
      static_cast<std::uint32_t>([:std::meta::enumerators_of(^^State).back():]);
  s = (value > lastValue) ? State::PlayingFreestyle : static_cast<State>(value);
}

constexpr ::gpio_num_t STATE_BUTTON_PIN = ::GPIO_NUM_0; // Not Final!!!

constexpr std::uint32_t BUTTON_POLL_MS = 10;

using AtomicState = std::atomic<State>;
// ESP-IDF's PSRAM workaround can use software-backed atomics on Xtensa.
// State access must be atomic, but need not always be lock-free.

using MAC = std::array<std::uint8_t, 6>;

inline std::array<char, 8> MACToBase64(const MAC &macAddress) {
  constexpr char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::uint64_t bits{};
  for (auto byte : macAddress | std::views::reverse) {
    bits = (bits << 8) | byte;
  }
  std::array<char, 8> result;
  for (char &digit : result) {
    digit = alphabet[bits & 0x3F];
    bits >>= 6;
  }
  return result;
}

inline constexpr std::uint32_t audioSampleRate = 24000;

class File {
public:
  File(const char *filename, const char *mode) noexcept
      : m_file(filename && mode ? std::fopen(filename, mode) : nullptr) {}

  ~File() noexcept {
    if (m_file) {
      std::fclose(m_file);
    }
  }

  File(const File &) = delete;
  File &operator=(const File &) = delete;

  File(File &&other) noexcept : m_file(std::exchange(other.m_file, nullptr)) {}

  File &operator=(File &&other) noexcept {
    if (this != &other) {
      if (m_file) {
        std::fclose(m_file);
      }
      m_file = std::exchange(other.m_file, nullptr);
    }

    return *this;
  }

  // Borrowed handle; ownership stays with this object.
  std::FILE *get() const noexcept { return m_file; }
  explicit operator bool() const noexcept { return m_file != nullptr; }

private:
  std::FILE *m_file;
};

class mp3File {
public:
  mp3File() = default;
  mp3File(std::string_view file, std::uint32_t maxPcmBytes = UINT32_MAX) noexcept;
  ~mp3File() noexcept;
  mp3File(const mp3File &) = delete;
  mp3File &operator=(const mp3File &) = delete;
  mp3File(mp3File &&other) noexcept
      : m_file{std::exchange(other.m_file, nullptr)},
        m_size{std::exchange(other.m_size, 0)} {}
  mp3File &operator=(mp3File &&other) noexcept;

  // Mono signed 16-bit PCM at audioSampleRate, exposed as raw 16-bit words.
  // A failed load leaves a null pointer and zero size.
  inline std::uint16_t *getFile() const noexcept { return m_file; }
  // Size of the decoded PCM in bytes (not the compressed file size).
  inline std::uint32_t getSize() const noexcept { return m_size; }

private:
  std::uint16_t *m_file{nullptr};
  std::uint32_t m_size{};
};

#if CONFIG_IDF_TARGET_ESP32
constexpr ::gpio_num_t PIN = ::GPIO_NUM_23;
#else
constexpr ::gpio_num_t PIN = ::GPIO_NUM_20;
#endif

template <bool Pulldown> struct OppositeEdge;

template <> struct OppositeEdge<true> {
  enum {
    pull_up = ::GPIO_PULLUP_DISABLE,
  };
  enum {
    pull_down = ::GPIO_PULLDOWN_ENABLE,

  };
  enum {
    intr_type = ::GPIO_INTR_POSEDGE,
  };
};
template <> struct OppositeEdge<false> {
  enum {
    pull_up = ::GPIO_PULLUP_ENABLE,
  };
  enum {
    pull_down = ::GPIO_PULLDOWN_DISABLE,

  };
  enum {
    intr_type = ::GPIO_INTR_NEGEDGE,
  };
};

} // namespace bh

#endif
