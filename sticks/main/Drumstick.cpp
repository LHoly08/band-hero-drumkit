#include <cstdint>
#include <cstdio>

#include "Drumstick.hpp"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "driver/gpio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

namespace bh {

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
    position->pitch = 0;
    position->yaw = 0;
    position->roll = 0;
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

    i2c_device_config_t device_config{};
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = 0x4A;
    device_config.scl_speed_hz = 100000;
    device_config.scl_wait_us = 20000;
    ESP_ERROR_CHECK(
        i2c_master_bus_add_device(m_bus, &device_config, &m_device));

    ESP_ERROR_CHECK(
        bno085_init(nullptr, m_device, GPIO_NUM_2, GPIO_NUM_21, &m_sensor));
    ESP_ERROR_CHECK(
        bno085_register_sensor_callback(m_sensor, on_sensor_data, &m_position));
    ESP_ERROR_CHECK(
        bno085_enable_sensor(m_sensor, BNO085_SENSOR_ROTATION_VECTOR, 10000));
  }

  ESP_ERROR_CHECK(gpio_install_isr_service(ESP_INTR_FLAG_IRAM));

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

Drumstick::~Drumstick() noexcept {
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
  if (value->sensor_id != BNO085_SENSOR_ROTATION_VECTOR) [[unlikely]] {
    return;
  }
  Coordinates *stickPosition = static_cast<Coordinates *>(context);

  const auto &q = value->data.quaternion;

  Coordinates moved;
  bno085_quaternion_to_euler(q.i, q.j, q.k, q.real, &moved.roll, &moved.pitch,
                             &moved.yaw);

  stickPosition->pitch += moved.pitch;
  stickPosition->yaw += moved.yaw;
  stickPosition->roll += moved.roll;
}

void Drumstick::loop() noexcept {

infinite_loop:

  if (m_sensor) {
    bno085_service(m_sensor);
  }
  vTaskDelay(1);
  std::printf("Roll: %.3f  Pitch: %.3f  Yaw: %.3f (rad)\n", m_position.roll,
              m_position.pitch, m_position.yaw);
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
