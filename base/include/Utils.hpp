#ifndef BH_UTIL
#define BH_UTIL

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <utility>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace bh {

enum class State : std::uint32_t {
  Config = 0,
  ConfigError,
  PlayingSerial,
  PlayingHub,
  PlayingFreestyle,
};

using AtomicState = std::atomic<State>;
// ESP-IDF's PSRAM workaround can use software-backed atomics on Xtensa.
// State access must be atomic, but need not always be lock-free.

using MAC = std::array<std::uint8_t, 6>;

constexpr std::string_view configFile = "config.lua";

struct Vec3 {
  float x{};
  float y{};
  float z{};

  Vec3 operator-(const Vec3 &other) const noexcept {
    return Vec3{
        .x = x - other.x,
        .y = y - other.y,
        .z = z - other.z,
    };
  }
  float dot(const Vec3 &other) const noexcept {
    return x * other.x + y * other.y + z * other.z;
  }
};

struct Packet {
  Vec3 coords{};
  float speed{};
};

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
  mp3File(std::string_view file) noexcept;
  ~mp3File() noexcept;
  mp3File(const mp3File &) = delete;
  mp3File &operator=(const mp3File &) = delete;
  mp3File(mp3File &&other) noexcept
      : m_file{std::exchange(other.m_file, nullptr)},
        m_size{std::exchange(other.m_size, 0)} {}
  mp3File &operator=(mp3File &&other) noexcept {
    if (this != &other) {
      m_file = std::exchange(other.m_file, nullptr);
      m_size = std::exchange(other.m_size, 0);
    }
    return *this;
  }

  inline std::uint16_t *getFile() const noexcept { return m_file; }
  inline std::uint32_t getSize() const noexcept { return m_size; }

private:
  std::uint16_t *m_file{nullptr};
  std::uint32_t m_size{};
};

constexpr ::gpio_num_t PIN = ::GPIO_NUM_20;

constexpr std::uint32_t BUTTON_DEBOUNCE_MS = 200;
constexpr ::TickType_t BUTTON_DEBOUNCE_TICKS =
    (BUTTON_DEBOUNCE_MS * configTICK_RATE_HZ + 999) / 1000;
static_assert(sizeof(::TickType_t) == sizeof(std::uint32_t));

struct ButtonDebounce {
  ::TickType_t last_press_tick{0};
  bool has_press{false};

  bool IRAM_ATTR accept() noexcept {
    const ::TickType_t now = ::xTaskGetTickCountFromISR();
    // Unsigned subtraction also handles tick-counter wraparound.
    if (has_press &&
        ::TickType_t(now - last_press_tick) < BUTTON_DEBOUNCE_TICKS) {
      return false;
    }
    last_press_tick = now;
    has_press = true;
    return true;
  }
};

// Accessed only by the serialized GPIO ISRs; persist across mode changes.
inline DRAM_ATTR ButtonDebounce mode_button_debounce{};

// Keep the IRAM handler out of class templates so Xtensa places its literal
// pool alongside the handler instead of in a separate COMDAT section.
void modeButtonPressed(void *args) noexcept;

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
