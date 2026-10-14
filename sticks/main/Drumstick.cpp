#include <cstdint>
#include <cstdio>

#include "Drumstick.hpp"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "driver/gpio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"

namespace bh {

namespace {
void IRAM_ATTR sensor_data_ready(void *context) noexcept {
  BaseType_t task_woken = pdFALSE;
  vTaskNotifyGiveFromISR(static_cast<TaskHandle_t>(context), &task_woken);
  if (task_woken) {
    portYIELD_FROM_ISR();
  }
}
} // namespace

bool IRAM_ATTR ButtonDebounce::accept() noexcept {
  const TickType_t now = xTaskGetTickCountFromISR();
  // Unsigned subtraction also handles tick-counter wraparound.
  if (has_press && TickType_t(now - last_press_tick) < BUTTON_DEBOUNCE_TICKS) {
    return false;
  }
  last_press_tick = now;
  has_press = true;
  return true;
}

void IRAM_ATTR reset_button_pressed(void *args) noexcept {
  if (args != nullptr && reset_button_debounce.accept()) {
    Coordinates *position = static_cast<Coordinates *>(args);
    portENTER_CRITICAL_ISR(&position_mux);
    position->pitch = 0;
    position->yaw = 0;
    position->roll = 0;
    portEXIT_CRITICAL_ISR(&position_mux);
  }
}

Drumstick::Drumstick() noexcept {
  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  {
    i2c_master_bus_config_t bus_config{};
    bus_config.i2c_port = I2C_NUM_0;
    bus_config.sda_io_num = GPIO_NUM_22;
    bus_config.scl_io_num = GPIO_NUM_23;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &m_bus));

    // Check the electrical bus and both address selections before opening SH-2.
    // A successful probe confirms an address ACK, not full sensor operation.
    ESP_LOGI("sticks", "I2C line levels before probe: SDA=%d SCL=%d",
             gpio_get_level(GPIO_NUM_22), gpio_get_level(GPIO_NUM_23));
    const esp_err_t probe_4a = i2c_master_probe(m_bus, 0x4A, 100);
    const esp_err_t probe_4b = i2c_master_probe(m_bus, 0x4B, 100);
    ESP_LOGI("sticks", "BNO085 address probes: 0x4A=%s, 0x4B=%s",
             esp_err_to_name(probe_4a), esp_err_to_name(probe_4b));
    ESP_LOGI("sticks", "I2C line levels after probe: SDA=%d SCL=%d",
             gpio_get_level(GPIO_NUM_22), gpio_get_level(GPIO_NUM_23));
    if (probe_4a == ESP_ERR_NOT_FOUND && probe_4b == ESP_OK) {
      ESP_LOGE("sticks", "Sensor ACKs at 0x4B, but this firmware uses 0x4A; "
                         "check the DI address pin.");
    }

    i2c_device_config_t device_config{};
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = 0x4A;
    device_config.scl_speed_hz = 100000;
    device_config.scl_wait_us = 20000;
    ESP_ERROR_CHECK(
        i2c_master_bus_add_device(m_bus, &device_config, &m_device));

    ESP_ERROR_CHECK(start_sensor());
  }

  ESP_ERROR_CHECK(gpio_install_isr_service(ESP_INTR_FLAG_IRAM));
  // Wake the task immediately when the active-low data-ready signal asserts.
  ESP_ERROR_CHECK(gpio_set_intr_type(GPIO_NUM_2, GPIO_INTR_NEGEDGE));
  ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_NUM_2, sensor_data_ready,
                                      xTaskGetCurrentTaskHandle()));
  m_sensor_interrupt_registered = true;

  {
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_now_init());
  }

  {
    gpio_config_t cfg;

    cfg.pin_bit_mask = 1ULL << STATE_BUTTON_PIN;
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
    cfg.intr_type = GPIO_INTR_POSEDGE;

    ESP_ERROR_CHECK(gpio_config(&cfg));
  }

  ESP_ERROR_CHECK(gpio_isr_handler_add(STATE_BUTTON_PIN, reset_button_pressed,
                                       &m_position));
}

esp_err_t Drumstick::start_sensor() noexcept {
  esp_err_t result =
      bno085_init(nullptr, m_device, GPIO_NUM_2, GPIO_NUM_21, &m_sensor);
  if (result != ESP_OK) {
    m_sensor = nullptr;
    return result;
  }

  result = bno085_register_sensor_callback(m_sensor, on_sensor_data, this);
  if (result == ESP_OK) {
    result =
        bno085_enable_sensor(m_sensor, BNO085_SENSOR_ROTATION_VECTOR, 10000);
  }
  // Reinitialization configures INT as a plain input, so restore its interrupt.
  if (result == ESP_OK && m_sensor_interrupt_registered) {
    result = gpio_set_intr_type(GPIO_NUM_2, GPIO_INTR_NEGEDGE);
    if (result == ESP_OK) {
      result = gpio_intr_enable(GPIO_NUM_2);
    }
  }
  if (result != ESP_OK) {
    bno085_deinit(m_sensor);
    m_sensor = nullptr;
  }
  return result;
}

void Drumstick::recover_sensor() noexcept {
  ESP_LOGW("sticks",
           "Sensor unreachable on I2C; resetting BNO085 and I2C. "
           "SDA=%d SCL=%d INT=%d",
           gpio_get_level(GPIO_NUM_22), gpio_get_level(GPIO_NUM_23),
           gpio_get_level(GPIO_NUM_2));

  if (m_sensor) {
    // The driver holds RST low during deinit, releasing the sensor from the
    // bus.
    bno085_deinit(m_sensor);
    m_sensor = nullptr;
  }
  esp_err_t result = i2c_master_bus_reset(m_bus);
  if (result == ESP_OK) {
    result = start_sensor();
  }
  if (result != ESP_OK) {
    ESP_LOGE("sticks", "Sensor recovery failed: %s", esp_err_to_name(result));
  } else {
    ESP_LOGI("sticks",
             "Sensor restarted; waiting for fresh orientation reports");
  }
}

Drumstick::~Drumstick() noexcept {
  gpio_isr_handler_remove(GPIO_NUM_2);
  bno085_deinit(m_sensor);

  (void)esp_now_unregister_recv_cb();
  (void)esp_now_deinit();

  (void)esp_wifi_stop();
  (void)esp_wifi_deinit();

  gpio_isr_handler_remove(STATE_BUTTON_PIN);
  gpio_uninstall_isr_service();
}

void Drumstick::on_sensor_data(bno085_handle_t,
                               const bno085_sensor_value_t *value,
                               void *context) noexcept {
  auto *stick = static_cast<Drumstick *>(context);
  if (value->sensor_id != BNO085_SENSOR_ROTATION_VECTOR) [[unlikely]] {
    return;
  }
  Coordinates *stickPosition = &stick->m_position;

  const auto &q = value->data.quaternion;

  Coordinates moved;
  bno085_quaternion_to_euler(q.i, q.j, q.k, q.real, &moved.roll, &moved.pitch,
                             &moved.yaw);

  portENTER_CRITICAL(&position_mux);
  *stickPosition = moved;
  portEXIT_CRITICAL(&position_mux);
}

void Drumstick::loop() noexcept {
  // TickType_t is 32 bits on this target. Unsigned subtraction handles rollover.
  constexpr TickType_t connection_check_ticks = pdMS_TO_TICKS(1000);
  TickType_t last_connection_check_tick = xTaskGetTickCount();
  std::uint32_t consecutive_probe_failures{};

infinite_loop:

  if (m_sensor) {
    bno085_service(m_sensor);
  }
  // Check reachability without treating a quiet sensor as a disconnect.
  const TickType_t now_tick = xTaskGetTickCount();
  const TickType_t ticks_since_check = now_tick - last_connection_check_tick;
  if (ticks_since_check >= connection_check_ticks) {
    last_connection_check_tick = now_tick;
    const esp_err_t connection = i2c_master_probe(m_bus, 0x4A, 100);
    if (connection == ESP_OK) {
      consecutive_probe_failures = 0;
      // A reconnected device can ACK while its driver session is still closed.
      if (!m_sensor) {
        recover_sensor();
      }
    } else if (connection == ESP_ERR_NOT_FOUND || connection == ESP_ERR_TIMEOUT ||
               connection == ESP_ERR_INVALID_STATE) {
      ++consecutive_probe_failures;
      if (consecutive_probe_failures >= 3) {
        consecutive_probe_failures = 0;
        recover_sensor();
      }
    }
    last_connection_check_tick = xTaskGetTickCount();
  }
  // Copy shared coordinates under the lock, then print outside it.
  portENTER_CRITICAL(&position_mux);
  const Coordinates snapshot = m_position;
  portEXIT_CRITICAL(&position_mux);
  std::printf("Roll: %.3f  Pitch: %.3f  Yaw: %.3f (rad)\n", snapshot.roll,
              snapshot.pitch, snapshot.yaw);
  // Sleep only while no packet is ready. An INT edge wakes us immediately;
  // the timeout permits a connection check even after a physical disconnect.
  if (!m_sensor || gpio_get_level(GPIO_NUM_2) != 0) {
    const TickType_t elapsed_ticks =
        xTaskGetTickCount() - last_connection_check_tick;
    if (elapsed_ticks < connection_check_ticks) {
      (void)ulTaskNotifyTake(pdTRUE, connection_check_ticks - elapsed_ticks);
    }
  }
  /*
    esp_err_t status =
        esp_now_send(ReceiverMAC.data(),
                     reinterpret_cast<const std::uint8_t *>(&val), sizeof(val));

    for (unsigned char i{}; i < 4 && status != ESP_OK; ++i) {

      status =
          esp_now_send(m_references.macAddress.data(),
                       reinterpret_cast<const std::uint8_t *>(&val),
    sizeof(val));
    }
  */
  goto infinite_loop;
}

} // namespace bh
