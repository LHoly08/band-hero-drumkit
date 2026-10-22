#ifndef BH_UTILS
#define BH_UTILS

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

#include "SpherePosition.hpp"

#include "driver/gpio.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace bh {

static constexpr std::uint64_t receiver_mac_value = BH_MAC;

constexpr std::array<std::uint8_t, 6> ReceiverMAC{
    static_cast<std::uint8_t>(receiver_mac_value >> 40),
    static_cast<std::uint8_t>(receiver_mac_value >> 32),
    static_cast<std::uint8_t>(receiver_mac_value >> 24),
    static_cast<std::uint8_t>(receiver_mac_value >> 16),
    static_cast<std::uint8_t>(receiver_mac_value >> 8),
    static_cast<std::uint8_t>(receiver_mac_value),
};

constexpr ::gpio_num_t STATE_BUTTON_PIN = ::GPIO_NUM_20;

constexpr std::uint32_t BUTTON_DEBOUNCE_MS = 200;
constexpr ::TickType_t BUTTON_DEBOUNCE_TICKS =
    (BUTTON_DEBOUNCE_MS * configTICK_RATE_HZ + 999) / 1000;
static_assert(sizeof(::TickType_t) == sizeof(std::uint32_t));

struct ButtonDebounce {
  ::TickType_t last_press_tick{0};
  bool has_press{false};

  bool accept() noexcept;
};

// Accessed only by the serialized GPIO ISRs; persist across mode changes.
inline DRAM_ATTR ButtonDebounce reset_button_debounce{};

struct Coordinates {
  float pitch{};
  float yaw{};
  float roll{};

  Coordinates operator-(const Coordinates &other) const {
    return {.pitch = this->pitch - other.pitch,
            .yaw = this->yaw - other.yaw,
            .roll = this->roll - other.roll};
  }
};

// Protect complete coordinate updates shared by the task and button ISR.
inline DRAM_ATTR ::portMUX_TYPE rotation_mux = portMUX_INITIALIZER_UNLOCKED;
inline DRAM_ATTR ::portMUX_TYPE position_mux = portMUX_INITIALIZER_UNLOCKED;

enum class Direction : std::uint8_t {
  PositiveReal = 0,
  PositiveImag,
  NegativeReal,
  NegativeImag,
};

inline Direction closest_direction(float radians) noexcept {
  constexpr float ONE_OVER_HALF_PI = 1 / (std::numbers::pi_v<float> * 0.5);
  constexpr float TWO_PI = std::numbers::pi_v<float> * 2;

  const float angle = std::remainder(radians, TWO_PI);
  const std::int32_t quadrant =
      static_cast<std::int32_t>(std::round(angle * ONE_OVER_HALF_PI));

  return static_cast<Direction>(quadrant & 0b11);
}

} // namespace bh

#endif
