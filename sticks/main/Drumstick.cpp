#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "Drumstick.hpp"

#include "ButtonPress.hpp"
#include "HitPacket.hpp"
#include "SerialCommand.hpp"
#include "ReportTiming.hpp"
#include "Utils.hpp"
#include "bno085.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs.h"

#include "driver/gpio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"

#include "hal/gpio_types.h"

namespace bh {

namespace {

// Keep pending radius updates free of hidden locks on the target.
static_assert(std::atomic<float>::is_always_lock_free);

// Preserve the existing detector's tuning independently of the position model.
// This is a legacy gyro scale, not the sphere radius in SphereConfig.
constexpr float HIT_SPEED_SCALE = 3.0f;
constexpr float HIT_THRESHOLD_ANGULAR = 6.0f;
constexpr float HIT_RELEASE_THRESHOLD_ANGULAR = 0.5f;

constexpr std::uint32_t SENSOR_READY_EVENT = 1u << 0;
constexpr std::uint32_t BUTTON_CHANGED_EVENT = 1u << 1;
constexpr char NVS_NAMESPACE[] = "drumstick";
constexpr char NVS_PEER_KEY[] = "receiver_mac";

#if BH_DEBUG
// Static lifetime: the Wi-Fi callback never accesses a Drumstick being destroyed.
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
std::atomic<std::uint32_t> send_successes{};
std::atomic<std::uint32_t> send_failures{};
std::int64_t last_send_report_us{};
constexpr std::int64_t SEND_REPORT_INTERVAL_US = 5'000'000;

void on_send_result(const ::esp_now_send_info_t *,
                    ::esp_now_send_status_t status) noexcept {
  // Runs on the high-priority Wi-Fi task: no logging, waiting, or retrying here.
  auto &counter = status == ESP_NOW_SEND_SUCCESS ? send_successes : send_failures;
  counter.fetch_add(1, std::memory_order_relaxed);
}

void report_send_results() noexcept {
  const auto now_us = ::esp_timer_get_time();
  if (now_us - last_send_report_us < SEND_REPORT_INTERVAL_US) {
    return;
  }
  last_send_report_us = now_us;
  const auto successes = send_successes.exchange(0, std::memory_order_relaxed);
  const auto failures = send_failures.exchange(0, std::memory_order_relaxed);
  if (successes != 0 || failures != 0) {
    ESP_LOGI("sticks", "ESP-NOW send results: %lu MAC-acked, %lu failed",
             static_cast<unsigned long>(successes),
             static_cast<unsigned long>(failures));
  }
}
#endif

::esp_err_t save_receiver(const bh::SerialMAC::Address &address) noexcept {
  ::nvs_handle_t handle{};
  auto result = ::nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (result != ESP_OK) {
    return result;
  }
  result = ::nvs_set_blob(handle, NVS_PEER_KEY, address.data(), address.size());
  if (result == ESP_OK) {
    result = ::nvs_commit(handle);
  }
  ::nvs_close(handle);
  return result;
}

void IRAM_ATTR sensor_data_ready(void *context) noexcept {
  ::BaseType_t task_woken = pdFALSE;
  xTaskNotifyFromISR(static_cast<::TaskHandle_t>(context), SENSOR_READY_EVENT,
                     eSetBits, &task_woken);
  if (task_woken) {
    portYIELD_FROM_ISR();
  }
}

enum class Condition : std::uint8_t {
  has_orientation = 0,
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
    conditions &= static_cast<std::uint8_t>(~bitMask);
  }
}

template <Condition C>
inline static constexpr bool get_condition(std::uint8_t conditions) {
  return conditions & (1u << static_cast<std::uint8_t>(C));
}

} // namespace

void IRAM_ATTR bh::Drumstick::reset_button_pressed(void *args) noexcept {
  if (args != nullptr) {
    ::BaseType_t task_woken = pdFALSE;
    xTaskNotifyFromISR(static_cast<::TaskHandle_t>(args), BUTTON_CHANGED_EVENT,
                       eSetBits, &task_woken);
    if (task_woken) {
      portYIELD_FROM_ISR();
    }
  }
}

bh::Drumstick::Drumstick() noexcept {
  for (const auto pin :
       {bh::GREEN_LED_PIN, bh::RED_LED_PIN, bh::SENSOR_RESET_PIN}) {
    ESP_ERROR_CHECK(::gpio_hold_dis(pin));
  }
  ::gpio_config_t leds{};
  leds.pin_bit_mask = (1ULL << bh::GREEN_LED_PIN) | (1ULL << bh::RED_LED_PIN);
  leds.mode = ::GPIO_MODE_OUTPUT;
  ESP_ERROR_CHECK(::gpio_config(&leds));
  ESP_ERROR_CHECK(::gpio_set_level(bh::GREEN_LED_PIN, 1));
  // No battery voltage divider is installed, so low battery cannot be measured.
  ESP_ERROR_CHECK(::gpio_set_level(bh::RED_LED_PIN, 0));

  ESP_ERROR_CHECK(::esp_event_loop_create_default());

  ESP_ERROR_CHECK(::gpio_install_isr_service(ESP_INTR_FLAG_IRAM));

  {
    ::wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(::esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(::esp_wifi_set_mode(::WIFI_MODE_STA));
    ESP_ERROR_CHECK(::esp_wifi_start());

    ESP_ERROR_CHECK(::esp_now_init());
#if BH_DEBUG
    send_successes.store(0, std::memory_order_relaxed);
    send_failures.store(0, std::memory_order_relaxed);
    last_send_report_us = ::esp_timer_get_time();
    ESP_ERROR_CHECK(::esp_now_register_send_cb(on_send_result));
    ESP_LOGI("sticks", "ESP-NOW debug diagnostics enabled (5 s summaries)");
#endif

    ::nvs_handle_t handle{};
    auto result = ::nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (result == ESP_OK) {
      bh::SerialMAC::Address stored{};
      std::size_t size = stored.size();
      result = ::nvs_get_blob(handle, NVS_PEER_KEY, stored.data(), &size);
      if (result == ESP_OK && size == stored.size()) {
        m_receiver_mac = stored;
      } else if (result == ESP_OK) {
        result = ESP_ERR_NVS_INVALID_LENGTH;
      }
      ::nvs_close(handle);
    }
    if (result == ESP_ERR_NVS_NOT_FOUND) {
      const auto saved = save_receiver(m_receiver_mac);
      if (saved != ESP_OK) {
        ESP_LOGW("sticks", "Cannot save build-time receiver MAC: %s",
                 ::esp_err_to_name(saved));
      } else {
        ESP_LOGI("sticks", "Build-time receiver MAC saved to NVS: " MACSTR,
                 MAC2STR(m_receiver_mac.data()));
      }
    } else if (result != ESP_OK) {
      ESP_LOGW("sticks", "Cannot load receiver MAC; using build default: %s",
               ::esp_err_to_name(result));
    }

    ::esp_now_peer_info_t peer{};
    std::copy(m_receiver_mac.begin(), m_receiver_mac.end(), peer.peer_addr);
    peer.ifidx = ::WIFI_IF_STA;
    peer.channel = 0; // Use the current Wi-Fi channel.
    peer.encrypt = false;
    ESP_ERROR_CHECK(::esp_now_add_peer(&peer));
    ESP_LOGI("sticks", "ESP-NOW receiver peer registered: " MACSTR,
             MAC2STR(m_receiver_mac.data()));
  }

  {
    ::gpio_config_t cfg;

    cfg.pin_bit_mask = 1ULL << bh::STATE_BUTTON_PIN;
    cfg.mode = ::GPIO_MODE_INPUT;
    cfg.pull_up_en = ::GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = ::GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = ::GPIO_INTR_ANYEDGE;

    ESP_ERROR_CHECK(::gpio_config(&cfg));
  }

  // Consume the wake press so it cannot calibrate or send us back to sleep.
  if (::esp_sleep_get_wakeup_causes() & (1u << ESP_SLEEP_WAKEUP_GPIO)) {
    do {
      while (::gpio_get_level(bh::STATE_BUTTON_PIN) == 0) {
        ::vTaskDelay(pdMS_TO_TICKS(bh::BUTTON_DEBOUNCE_MS));
      }
      ::vTaskDelay(pdMS_TO_TICKS(bh::BUTTON_DEBOUNCE_MS));
    } while (::gpio_get_level(bh::STATE_BUTTON_PIN) == 0);
  }

  ESP_ERROR_CHECK(::gpio_isr_handler_add(
      bh::STATE_BUTTON_PIN, reset_button_pressed, xTaskGetCurrentTaskHandle()));

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

  // Wake the task immediately when the active-low data-ready signal asserts.
  ESP_ERROR_CHECK(::gpio_set_intr_type(::GPIO_NUM_2, ::GPIO_INTR_NEGEDGE));
  ESP_ERROR_CHECK(::gpio_isr_handler_add(::GPIO_NUM_2, sensor_data_ready,
                                         xTaskGetCurrentTaskHandle()));

  // Only the latest unread MAC matters; xQueueOverwrite requires one slot.
  m_peer_updates = xQueueCreate(1, sizeof(bh::SerialMAC::Address));
  ESP_ERROR_CHECK(m_peer_updates ? ESP_OK : ESP_ERR_NO_MEM);
  m_serial_stopped = xSemaphoreCreateBinary();
  ESP_ERROR_CHECK(m_serial_stopped ? ESP_OK : ESP_ERR_NO_MEM);
  // Use the configured serial console without blocking the sensor task.
  const int flags = ::fcntl(STDIN_FILENO, F_GETFL);
  ESP_ERROR_CHECK(
      flags >= 0 && ::fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) >= 0
          ? ESP_OK
          : ESP_FAIL);
  ESP_ERROR_CHECK(xTaskCreatePinnedToCore(serial_input_task, "serial_mac", 3072,
                                          this, 1, &m_serial_task, 0) == pdPASS
                      ? ESP_OK
                      : ESP_ERR_NO_MEM);
}

void bh::Drumstick::serial_input_task(void *context) noexcept {
  auto *stick = static_cast<bh::Drumstick *>(context);
  bh::SerialCommandReader reader;
  const auto poll_ticks = std::max<::TickType_t>(1, pdMS_TO_TICKS(10));
  ESP_LOGI("sticks", "Commands: M AAAAAAAAAAAA or R 27.5 (cm), then Enter");
  while (ulTaskNotifyTake(pdTRUE, 0) == 0) {
    char ch{};
    if (::read(STDIN_FILENO, &ch, 1) != 1) {
      if (ulTaskNotifyTake(pdTRUE, poll_ticks) != 0) {
        break;
      }
      continue;
    }
    const auto command = reader.push(ch);
    switch (command.kind) {
    case bh::SerialCommand::Kind::Receiver:
      // Peer registration and NVS writes belong to the main task.
      xQueueOverwrite(stick->m_peer_updates, &command.address);
      break;
    case bh::SerialCommand::Kind::Radius:
      stick->m_pending_radius.store(command.radius_meters,
                                    std::memory_order_relaxed);
      break;
    case bh::SerialCommand::Kind::Invalid:
      ESP_LOGW("sticks", "Invalid command; use M AAAAAAAAAAAA or R 27.5 "
                         "with one space, then Enter");
      break;
    case bh::SerialCommand::Kind::None:
      break;
    }
  }

  // Acknowledge that serial reads have stopped before the destructor deletes
  // us.
  xSemaphoreGive(stick->m_serial_stopped);
  ::vTaskSuspend(nullptr);
}

void bh::Drumstick::update_peer(
    const bh::SerialMAC::Address &address) noexcept {
  const bool changed = address != m_receiver_mac;
  if (changed) {
    ::esp_now_peer_info_t peer{};
    std::copy(address.begin(), address.end(), peer.peer_addr);
    peer.ifidx = ::WIFI_IF_STA;
    peer.channel = 0;
    peer.encrypt = false;
    const auto result = ::esp_now_add_peer(&peer);
    if (result != ESP_OK) {
      ESP_LOGE("sticks", "Cannot register receiver MAC: %s",
               ::esp_err_to_name(result));
      return;
    }
  }

  const auto result = save_receiver(address);
  if (result != ESP_OK) {
    if (changed) {
      (void)::esp_now_del_peer(address.data());
    }
    ESP_LOGE("sticks", "Cannot save receiver MAC: %s",
             ::esp_err_to_name(result));
    return;
  }
  if (changed) {
    const auto removed = ::esp_now_del_peer(m_receiver_mac.data());
    if (removed != ESP_OK) {
      ESP_LOGW("sticks", "Cannot remove previous receiver: %s",
               ::esp_err_to_name(removed));
    }
    m_receiver_mac = address;
  }
  ESP_LOGI("sticks", "Receiver MAC saved: " MACSTR,
           MAC2STR(m_receiver_mac.data()));
}

::esp_err_t bh::Drumstick::start_sensor() noexcept {
  ESP_ERROR_CHECK(::gpio_set_level(bh::GREEN_LED_PIN, 1));
  set_condition<false, Condition::has_orientation, Condition::hit_armed>(
      m_condition);
  m_top_hit_speed = 0.0f;
  m_tracking.invalidate();
  ESP_LOGI("sticks", "Point level and forward, then press/release reset to "
                     "calibrate chest-relative XYZ (meters)");
  ::esp_err_t result = ::bno085_init(nullptr, m_device, ::GPIO_NUM_2,
                                     bh::SENSOR_RESET_PIN, &m_sensor);
  if (result != ESP_OK) {
    m_sensor = nullptr;
    return result;
  }

  result = ::bno085_register_sensor_callback(m_sensor, on_sensor_data, this);
  if (result == ESP_OK) {
    result =
        ::bno085_enable_sensor(m_sensor, BNO085_SENSOR_ROTATION_VECTOR, 10000);
  }
  if (result == ESP_OK) {
    result = ::bno085_enable_sensor(m_sensor,
                                    BNO085_SENSOR_GYROSCOPE_CALIBRATED, 10000);
  }
  if (result != ESP_OK) {
    ::bno085_deinit(m_sensor);
    m_sensor = nullptr;
  }
  return result;
}

bh::Drumstick::~Drumstick() noexcept {
  if (m_serial_task) {
    xTaskNotifyGive(m_serial_task);
    (void)xSemaphoreTake(m_serial_stopped, portMAX_DELAY);
    ::vTaskDelete(m_serial_task);
  }
  if (m_serial_stopped) {
    ::vSemaphoreDelete(m_serial_stopped);
  }
  if (m_peer_updates) {
    ::vQueueDelete(m_peer_updates);
  }
  (void)::gpio_set_level(bh::GREEN_LED_PIN, 0);
  ::gpio_isr_handler_remove(::GPIO_NUM_2);
  ::bno085_deinit(m_sensor);
  (void)::i2c_master_bus_rm_device(m_device);
  (void)::i2c_del_master_bus(m_bus);

#if BH_DEBUG
  (void)::esp_now_unregister_send_cb();
#endif
  (void)::esp_now_deinit();

  (void)::esp_wifi_stop();
  (void)::esp_wifi_deinit();
  (void)::esp_event_loop_delete_default();

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
  case BNO085_SENSOR_ROTATION_VECTOR: {
    stick->update_position(*value);
    return;
  }
  default: {
    return;
  }
  }
}

void bh::Drumstick::update_position(
    const ::bno085_sensor_value_t &value) noexcept {

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
    ESP_ERROR_CHECK(::gpio_set_level(bh::GREEN_LED_PIN, 0));
    const auto p = m_tracking.position();
    ESP_LOGI("sticks",
             "XYZ calibrated: X=%.3f Y=%.3f Z=%.3f m "
             "(right, forward, up)",
             p.x, p.y, p.z);
  }

  // Sensor callbacks run synchronously on the loop task via bno085_service().
  m_rotation = moved;
  m_orientation_us = static_cast<std::uint32_t>(::esp_timer_get_time());
  m_orientation_report_us = value.timestamp_us;
  set_condition<true, Condition::has_orientation>(m_condition);
}

void bh::Drumstick::detect_hit(const ::bno085_sensor_value_t &value) noexcept {
  const auto now_us = static_cast<std::uint32_t>(::esp_timer_get_time());

  if (get_condition<Condition::reset_pending>(m_condition) ||
      !get_condition<Condition::has_orientation>(m_condition) ||
      !bh::orientationFresh(now_us, m_orientation_us, value.timestamp_us,
                            m_orientation_report_us))
      [[unlikely]] {
    set_condition<false, Condition::hit_armed>(m_condition);
    m_top_hit_speed = 0.0f;
    return;
  }
  const auto &g = value.data.gyroscope_calibrated;
  float speed = HIT_SPEED_SCALE;

  const bh::Coordinates relative_rotation = m_rotation - m_offset;

  switch (bh::closest_direction(relative_rotation.roll)) {
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

  if (!std::isfinite(speed)) {
    set_condition<false, Condition::hit_armed>(m_condition);
    m_top_hit_speed = 0.0f;
    return;
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

  const auto position = m_tracking.position();
  const bh::Packet packet{
      .coords = {position.x, position.y, position.z}, .speed = m_top_hit_speed};

  // One write keeps the two records adjacent for SticksTestAudio's HitReader.
#if BH_DEBUG
  std::printf("Hit! Roll: %.3f  Pitch: %.3f  Yaw: %.3f (rad), Speed: %.3f\n"
              "HitXYZ! X: %.3f  Y: %.3f  Z: %.3f (m)\n",
              relative_rotation.roll, relative_rotation.pitch,
              relative_rotation.yaw, packet.speed, packet.coords.x,
              packet.coords.y, packet.coords.z);
#endif

  const esp_err_t status = esp_now_send(
      m_receiver_mac.data(), reinterpret_cast<const std::uint8_t *>(&packet),
      sizeof(packet));
  if (status != ESP_OK) {
    ESP_LOGW("sticks", "Hit send failed: %s", esp_err_to_name(status));
  }

  m_top_hit_speed = 0.0f;
}

void bh::Drumstick::request_calibration() noexcept {
  ESP_ERROR_CHECK(::gpio_set_level(bh::GREEN_LED_PIN, 1));
  m_tracking.invalidate();
  m_top_hit_speed = 0.0f;
  set_condition<true, Condition::reset_pending>(m_condition);
  set_condition<false, Condition::has_orientation, Condition::hit_armed>(
      m_condition);
}

void bh::Drumstick::loop() noexcept {
  using Action = bh::ButtonPress::Action;

  bh::ButtonPress button;
  // Resample to confirm a stable press, even without sensor data.
  constexpr ::TickType_t button_poll_ticks =
      pdMS_TO_TICKS(bh::BUTTON_POLL_MS) > 0 ? pdMS_TO_TICKS(bh::BUTTON_POLL_MS)
                                            : 1;

  while (true) {

    bh::SerialMAC::Address address{};
    if (xQueueReceive(m_peer_updates, &address, 0) == pdTRUE) {
      update_peer(address);
    }

    // Consume the latest radius and clear it in one atomic operation.
    // The value is self-contained, so relaxed ordering is sufficient.
    const float radius_meters =
        m_pending_radius.exchange(0.0f, std::memory_order_relaxed);

    if (radius_meters > 0.0f && m_tracking.set_radius(radius_meters)) {
      // Recalibrate with the new radius and suppress hits until it succeeds.
      request_calibration();
      ESP_LOGI("sticks", "Radius updated: %.3f m (%.2f cm)", radius_meters,
               radius_meters * 100.0f);
    }

    // GPIO notifications wake the task on either edge.
    (void)xTaskNotifyWait(0, UINT32_MAX, nullptr, 0);
    switch (button.update(
        ::gpio_get_level(bh::STATE_BUTTON_PIN) == 0,
        static_cast<std::uint32_t>(::esp_timer_get_time() / 1000))) {
    case Action::Calibrate: {
      request_calibration();
      break;
    }
    case Action::Sleep: {
      return; // app_main destroys this object before entering deep sleep.
    }
    case Action::None: {
      break;
    }
    }

    if (m_sensor) {
      ::bno085_service(m_sensor);
    }
#if BH_DEBUG
    report_send_results();
#endif
    // Sleep only while no packet is ready. An INT edge wakes us immediately;
    // the timeout keeps button sampling active even after a sensor disconnect.
    if (!m_sensor || ::gpio_get_level(::GPIO_NUM_2) != 0) {
      (void)xTaskNotifyWait(0, UINT32_MAX, nullptr, button_poll_ticks);
    }
  }
}

} // namespace bh
