#ifndef BH_STICK
#define BH_STICK

#include <atomic>
#include <cstdint>

#include "bno085.h"
#include "driver/i2c_master.h"

#include "SerialMAC.hpp"
#include "Utils.hpp"
#include "freertos/projdefs.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "portmacro.h"

namespace bh {

class Drumstick {
public:
  Drumstick() noexcept;
  ~Drumstick() noexcept;
  Drumstick(const Drumstick &) = delete;
  Drumstick &operator=(const Drumstick &) = delete;
  void loop() noexcept;

private:
  bh::Coordinates m_offset{};
  bh::Coordinates m_rotation{};
  bh::SpherePosition m_tracking{};
  std::uint32_t m_orientation_us{};
  float m_top_hit_speed{};
  /*
   has_orientation - 0
   hit_armed - 1
   reset_pending - 2
  */
  std::uint8_t m_condition{};
  ::i2c_master_bus_handle_t m_bus{};
  ::i2c_master_dev_handle_t m_device{};
  ::bno085_handle_t m_sensor{};
  bh::SerialMAC::Address m_receiver_mac{bh::ReceiverMAC};
  // Serial task overwrites; main loop consumes and applies the latest MAC.
  ::QueueHandle_t m_peer_updates{};
  // Zero means no pending update; valid radii are positive meters.
  std::atomic<float> m_pending_radius{0.0f};
  ::TaskHandle_t m_serial_task{};
  ::SemaphoreHandle_t m_serial_stopped{};

  static void serial_input_task(void *context) noexcept;
  void update_peer(const bh::SerialMAC::Address &address) noexcept;

  ::esp_err_t start_sensor() noexcept;
  void request_calibration() noexcept;
  void update_position(const ::bno085_sensor_value_t &value) noexcept;
  void detect_hit(const ::bno085_sensor_value_t &value) noexcept;

  static void on_sensor_data(::bno085_handle_t,
                             const ::bno085_sensor_value_t *value,
                             void *context) noexcept;
  static void reset_button_pressed(void *args) noexcept;
};

} // namespace bh

#endif
