#include <cmath>
#include <cstdint>
#include <cstdio>

#include "Drumstick.hpp"

#include "Utils.hpp"
#include "bno085.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "driver/gpio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"

#include "hal/gpio_types.h"

namespace bh {

namespace {

// Preserve the existing detector's tuning independently of the position model.
// This is a legacy gyro scale, not the sphere radius in SphereConfig.
constexpr float HIT_SPEED_SCALE = 3.0f;
constexpr float HIT_THRESHOLD_ANGULAR = 6.0f;
constexpr float HIT_RELEASE_THRESHOLD_ANGULAR = 0.5f;

constexpr std::uint32_t MAX_ORIENTATION_AGE_US = 50000;
constexpr std::uint32_t SENSOR_READY_EVENT = 1u << 0;
constexpr std::uint32_t RESET_ORIENTATION_EVENT = 1u << 1;

void IRAM_ATTR sensor_data_ready(void *context) noexcept {
  ::BaseType_t task_woken = pdFALSE;
  xTaskNotifyFromISR(static_cast<::TaskHandle_t>(context), SENSOR_READY_EVENT,
                     eSetBits, &task_woken);
  if (task_woken) {
    portYIELD_FROM_ISR();
  }
}

enum class Condition : std::uint8_t {
  sensor_interrupt_registered = 0,
  has_orientation,
  hit_armed,
  reset_pending,
};

template <bool flag, Condition... Conditions>
inline static constexpr void set_condition(std::uint8_t &conditions) {
  // Create bit mask with 1 on every condition that will be changed
  constexpr auto bitMask =
      (0u | ... | (1u << static_cast<std::uint8_t>(Conditions)));

  if constexpr (flag) {
    conditions |= bitMask;
  } else {
    conditions &= ~bitMask;
  }
}

template <Condition C>
inline static constexpr bool get_condition(std::uint8_t conditions) {
  return conditions & (1u << static_cast<std::uint8_t>(C));
}

} // namespace

bool IRAM_ATTR bh::ButtonDebounce::accept() noexcept {
  const ::TickType_t now = ::xTaskGetTickCountFromISR();
  // Unsigned subtraction also handles tick-counter wraparound.
  if (has_press && ::TickType_t(now - last_press_tick) < bh::BUTTON_DEBOUNCE_TICKS) {
    return false;
  }
  last_press_tick = now;
  has_press = true;
  return true;
}

void IRAM_ATTR bh::Drumstick::reset_button_pressed(void *args) noexcept {
  if (args != nullptr && bh::reset_button_debounce.accept()) {
    ::BaseType_t task_woken = pdFALSE;
    xTaskNotifyFromISR(static_cast<::TaskHandle_t>(args), RESET_ORIENTATION_EVENT,
                       eSetBits, &task_woken);
    if (task_woken) {
      portYIELD_FROM_ISR();
    }
  }
}

bh::Drumstick::Drumstick() noexcept {
  ESP_ERROR_CHECK(::nvs_flash_init());
  ESP_ERROR_CHECK(::esp_netif_init());
  ESP_ERROR_CHECK(::esp_event_loop_create_default());

  {
    ::i2c_master_bus_config_t bus_config{};
    bus_config.i2c_port = ::I2C_NUM_0;
    bus_config.sda_io_num = ::GPIO_NUM_22;
    bus_config.scl_io_num = ::GPIO_NUM_23;
    bus_config.clk_source = ::I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    ESP_ERROR_CHECK(::i2c_new_master_bus(&bus_config, &m_bus));

    // Check the electrical bus and both address selections before opening SH-2.
    // A successful probe confirms an address ACK, not full sensor operation.
    ESP_LOGI("sticks", "I2C line levels before probe: SDA=%d SCL=%d",
             ::gpio_get_level(::GPIO_NUM_22), ::gpio_get_level(::GPIO_NUM_23));
    const ::esp_err_t probe_4a = ::i2c_master_probe(m_bus, 0x4A, 100);
    const ::esp_err_t probe_4b = ::i2c_master_probe(m_bus, 0x4B, 100);
    ESP_LOGI("sticks", "BNO085 address probes: 0x4A=%s, 0x4B=%s",
             ::esp_err_to_name(probe_4a), ::esp_err_to_name(probe_4b));
    ESP_LOGI("sticks", "I2C line levels after probe: SDA=%d SCL=%d",
             ::gpio_get_level(::GPIO_NUM_22), ::gpio_get_level(::GPIO_NUM_23));
    if (probe_4a == ESP_ERR_NOT_FOUND && probe_4b == ESP_OK) {
      ESP_LOGE("sticks", "Sensor ACKs at 0x4B, but this firmware uses 0x4A; "
                         "check the DI address pin.");
    }

    ::i2c_device_config_t device_config{};
    device_config.dev_addr_length = ::I2C_ADDR_BIT_LEN_7;
    device_config.device_address = 0x4A;
    device_config.scl_speed_hz = 100000;
    device_config.scl_wait_us = 20000;
    ESP_ERROR_CHECK(
        ::i2c_master_bus_add_device(m_bus, &device_config, &m_device));

    ESP_ERROR_CHECK(start_sensor());
  }

  ESP_ERROR_CHECK(::gpio_install_isr_service(ESP_INTR_FLAG_IRAM));
  // Wake the task immediately when the active-low data-ready signal asserts.
  ESP_ERROR_CHECK(::gpio_set_intr_type(::GPIO_NUM_2, ::GPIO_INTR_NEGEDGE));
  ESP_ERROR_CHECK(::gpio_isr_handler_add(::GPIO_NUM_2, sensor_data_ready,
                                       xTaskGetCurrentTaskHandle()));

  set_condition<true, Condition::sensor_interrupt_registered>(m_condition);

  {
    ::wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(::esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(::esp_wifi_set_mode(::WIFI_MODE_STA));
    ESP_ERROR_CHECK(::esp_wifi_start());

    ESP_ERROR_CHECK(::esp_now_init());
  }

  {
    ::gpio_config_t cfg;

    cfg.pin_bit_mask = 1ULL << bh::STATE_BUTTON_PIN;
    cfg.mode = ::GPIO_MODE_INPUT;
    cfg.pull_up_en = ::GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = ::GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = ::GPIO_INTR_POSEDGE;

    ESP_ERROR_CHECK(::gpio_config(&cfg));
  }

  ESP_ERROR_CHECK(::gpio_isr_handler_add(bh::STATE_BUTTON_PIN, reset_button_pressed,
                                       xTaskGetCurrentTaskHandle()));
}

::esp_err_t bh::Drumstick::start_sensor() noexcept {
  set_condition<false, Condition::has_orientation, Condition::hit_armed>(
      m_condition);
  m_last_hit_us = 0;
  m_top_hit_speed = 0.0f;
  m_tracking.invalidate();
  ESP_LOGI("sticks", "Point level and forward, then press/release reset to "
                     "calibrate chest-relative XYZ (meters)");
  ::esp_err_t result =
      ::bno085_init(nullptr, m_device, ::GPIO_NUM_2, ::GPIO_NUM_21, &m_sensor);
  if (result != ESP_OK) {
    m_sensor = nullptr;
    return result;
  }

  result = ::bno085_register_sensor_callback(m_sensor, on_sensor_data, this);
  if (result == ESP_OK) {
    result =
        ::bno085_enable_sensor(m_sensor, BNO085_SENSOR_ROTATION_VECTOR, 10000);
  }
  /*
   // Currently not needed
    if (result == ESP_OK) {
      result = bno085_enable_sensor(m_sensor, BNO085_SENSOR_LINEAR_ACCELERATION,
                                  10000);
    }
  */
  if (result == ESP_OK) {
    result = ::bno085_enable_sensor(m_sensor, BNO085_SENSOR_GYROSCOPE_CALIBRATED,
                                  10000);
  }
  // Reinitialization configures INT as a plain input, so restore its interrupt.
  if (result == ESP_OK &&
      get_condition<Condition::sensor_interrupt_registered>(m_condition)) {
    result = ::gpio_set_intr_type(::GPIO_NUM_2, ::GPIO_INTR_NEGEDGE);
    if (result == ESP_OK) {
      result = ::gpio_intr_enable(::GPIO_NUM_2);
    }
  }
  if (result != ESP_OK) {
    ::bno085_deinit(m_sensor);
    m_sensor = nullptr;
  }
  return result;
}

void bh::Drumstick::recover_sensor() noexcept {
  ESP_LOGW("sticks",
           "Sensor unreachable on I2C; resetting BNO085 and I2C. "
           "SDA=%d SCL=%d INT=%d",
           ::gpio_get_level(::GPIO_NUM_22), ::gpio_get_level(::GPIO_NUM_23),
           ::gpio_get_level(::GPIO_NUM_2));

  if (m_sensor) {
    // The driver holds RST low during deinit, releasing the sensor from the
    // bus.
    ::bno085_deinit(m_sensor);
    m_sensor = nullptr;
  }
  ::esp_err_t result = ::i2c_master_bus_reset(m_bus);
  if (result == ESP_OK) {
    result = start_sensor();
  }
  if (result != ESP_OK) {
    ESP_LOGE("sticks", "Sensor recovery failed: %s", ::esp_err_to_name(result));
  } else {
    ESP_LOGI("sticks",
             "Sensor restarted; waiting for fresh orientation reports");
  }
}

bh::Drumstick::~Drumstick() noexcept {
  ::gpio_isr_handler_remove(::GPIO_NUM_2);
  ::bno085_deinit(m_sensor);

  (void)::esp_now_unregister_recv_cb();
  (void)::esp_now_deinit();

  (void)::esp_wifi_stop();
  (void)::esp_wifi_deinit();

  ::gpio_isr_handler_remove(bh::STATE_BUTTON_PIN);
  ::gpio_uninstall_isr_service();
}

void bh::Drumstick::on_sensor_data(::bno085_handle_t,
                               const ::bno085_sensor_value_t *value,
                               void *context) noexcept {
  auto *stick = static_cast<bh::Drumstick *>(context);
  switch (value->sensor_id) {
  case BNO085_SENSOR_GYROSCOPE_CALIBRATED: {
    stick->detect_hit(*value);
    return;
  }
  case BNO085_SENSOR_LINEAR_ACCELERATION: {
    return;
  }
  case BNO085_SENSOR_ROTATION_VECTOR: {
    stick->update_position(*value);
    return;
  }
  default: {
    return;
  }
  }
}

void bh::Drumstick::update_position(const ::bno085_sensor_value_t &value) noexcept {

  const auto &q = value.data.quaternion;
  const bool reset = get_condition<Condition::reset_pending>(m_condition);
  if (!m_tracking.update({q.real, q.i, q.j, q.k}, reset)) {
    set_condition<false, Condition::has_orientation, Condition::hit_armed>(
        m_condition);
    m_top_hit_speed = 0.0f;
    return;
  }

  bh::Coordinates moved;
  ::bno085_quaternion_to_euler(q.i, q.j, q.k, q.real, &moved.roll, &moved.pitch,
                             &moved.yaw);

  // Keep the request pending until an orientation report actually arrives.
  if (reset) {
    m_offset = moved;
    m_top_hit_speed = 0.0f;
    set_condition<false, Condition::reset_pending>(m_condition);
    const auto p = m_tracking.position();
    ESP_LOGI("sticks",
             "XYZ calibrated: X=%.3f Y=%.3f Z=%.3f m "
             "(right, forward, up)",
             p.x, p.y, p.z);
  }

  portENTER_CRITICAL(&bh::rotation_mux);
  m_rotation = moved;
  portEXIT_CRITICAL(&bh::rotation_mux);
  portENTER_CRITICAL(&bh::position_mux);
  m_position = m_tracking.position();
  portEXIT_CRITICAL(&bh::position_mux);
  m_orientation_us = static_cast<std::uint32_t>(::esp_timer_get_time());
  set_condition<true, Condition::has_orientation>(m_condition);
}

void bh::Drumstick::detect_hit(const ::bno085_sensor_value_t &value) noexcept {
  const auto now_us = static_cast<std::uint32_t>(::esp_timer_get_time());
  if (get_condition<Condition::reset_pending>(m_condition) ||
      !get_condition<Condition::has_orientation>(m_condition) ||
      std::uint32_t(now_us - m_orientation_us) > MAX_ORIENTATION_AGE_US)
      [[unlikely]] {
    set_condition<false, Condition::hit_armed>(m_condition);
    m_top_hit_speed = 0.0f;
    return;
  }
  const auto &g = value.data.gyroscope_calibrated;
  float speed = HIT_SPEED_SCALE;

  portENTER_CRITICAL(&bh::rotation_mux);
  const bh::Coordinates packet_coords = m_rotation - m_offset;
  portEXIT_CRITICAL(&bh::rotation_mux);

  switch (bh::closest_direction(packet_coords.roll)) {
  case bh::Direction::PositiveReal: {
    speed *= g.y;
    break;
  }
  case bh::Direction::PositiveImag: {
    speed *= -g.z;
    break;
  }
  case bh::Direction::NegativeReal: {
    speed *= -g.y;
    break;
  }
  case bh::Direction::NegativeImag: {
    speed *= g.z;
    break;
  }
  }

  if (speed <= -HIT_THRESHOLD_ANGULAR) [[likely]] {
    set_condition<true, Condition::hit_armed>(m_condition);
    m_top_hit_speed = (-speed > m_top_hit_speed) ? -speed : m_top_hit_speed;
    return;
  }

  if (!(speed >= -HIT_RELEASE_THRESHOLD_ANGULAR &&
        get_condition<Condition::hit_armed>(m_condition))) {
    return;
  }
  set_condition<false, Condition::hit_armed>(m_condition);

  struct {
    bh::Coordinates coords{};
    float speed{};
  } packet{.coords = packet_coords, .speed = m_top_hit_speed};

  // std::printf("X: %.3f Y: %.3f Z: %.3f \n", g.x, g.y, g.z);
  std::printf("Hit! Roll: %.3f  Pitch: %.3f  Yaw: %.3f (rad), Speed: %.3f\n",
              packet.coords.roll, packet.coords.pitch, packet.coords.yaw,
              packet.speed);
  // Separate line preserves the existing angle-based serial matcher format.
  portENTER_CRITICAL(&bh::position_mux);
  const bh::Vector3 position = m_position;
  portEXIT_CRITICAL(&bh::position_mux);
  std::printf("HitXYZ! X: %.3f  Y: %.3f  Z: %.3f (m)\n", position.x, position.y,
              position.z);

  // Register ReceiverMAC as an ESP-NOW peer before enabling transmission.
  // const esp_err_t status = esp_now_send(
  //     ReceiverMAC.data(), reinterpret_cast<const std::uint8_t *>(&packet),
  //     sizeof(packet));
  // if (status != ESP_OK) {
  //   ESP_LOGW("sticks", "Hit send failed: %s", esp_err_to_name(status));
  // }

  m_top_hit_speed = std::abs(speed);
}

void bh::Drumstick::loop() noexcept {
  // TickType_t is 32 bits on this target. Unsigned subtraction handles
  // rollover.
  constexpr ::TickType_t connection_check_ticks = pdMS_TO_TICKS(1000);
  ::TickType_t last_connection_check_tick = xTaskGetTickCount();
  std::uint32_t consecutive_probe_failures{};
  std::uint32_t events{};

infinite_loop:

  // Merge events received while sleeping with any that arrived since waking.
  std::uint32_t pending_events{};
  (void)xTaskNotifyWait(0, UINT32_MAX, &pending_events, 0);
  events |= pending_events;
  if (events & RESET_ORIENTATION_EVENT) {
    m_tracking.invalidate();
    m_top_hit_speed = 0.0f;
    set_condition<true, Condition::reset_pending>(m_condition);
    set_condition<false, Condition::has_orientation, Condition::hit_armed>(
        m_condition);
  }
  events = 0;

  if (m_sensor) {
    ::bno085_service(m_sensor);
  }
  // Check reachability without treating a quiet sensor as a disconnect.
  const ::TickType_t now_tick = xTaskGetTickCount();
  const ::TickType_t ticks_since_check = now_tick - last_connection_check_tick;
  if (ticks_since_check >= connection_check_ticks) {
    last_connection_check_tick = now_tick;
    const ::esp_err_t connection = ::i2c_master_probe(m_bus, 0x4A, 100);
    if (connection == ESP_OK) {
      consecutive_probe_failures = 0;
      // A reconnected device can ACK while its driver session is still closed.
      if (!m_sensor) {
        recover_sensor();
      }
    } else if (connection == ESP_ERR_NOT_FOUND ||
               connection == ESP_ERR_TIMEOUT ||
               connection == ESP_ERR_INVALID_STATE) {
      ++consecutive_probe_failures;
      if (consecutive_probe_failures >= 3) {
        consecutive_probe_failures = 0;
        recover_sensor();
      }
    }
    last_connection_check_tick = xTaskGetTickCount();
  }
  // Sleep only while no packet is ready. An INT edge wakes us immediately;
  // the timeout permits a connection check even after a physical disconnect.
  if (!m_sensor || ::gpio_get_level(::GPIO_NUM_2) != 0) {
    const ::TickType_t elapsed_ticks =
        xTaskGetTickCount() - last_connection_check_tick;
    if (elapsed_ticks < connection_check_ticks) {
      (void)xTaskNotifyWait(0, UINT32_MAX, &events,
                            connection_check_ticks - elapsed_ticks);
    }
  }
  goto infinite_loop;
}

} // namespace bh
