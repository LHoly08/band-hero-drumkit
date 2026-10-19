#ifndef BH_STICK
#define BH_STICK

#include <cstdint>

#include "bno085.h"
#include "driver/i2c_master.h"

#include "Utils.hpp"

namespace bh {

class Drumstick {
public:
  Drumstick() noexcept;
  ~Drumstick() noexcept;
  Drumstick(const Drumstick &) = delete;
  Drumstick &operator=(const Drumstick &) = delete;
  [[noreturn]]
  void loop() noexcept;

private:
  Coordinates m_offset{};
  Coordinates m_rotation{};
  Vector3 m_position{};
  SpherePosition m_tracking{};
  std::uint32_t m_orientation_us{};
  std::uint32_t m_last_hit_us{};
  float m_top_hit_speed{};
  /*
   sensor_interrupt_registered - 0
   has_orientation - 1
   hit_armed - 2
   reset_pending - 3
  */
  std::uint8_t m_condition{};
  i2c_master_bus_handle_t m_bus{};
  i2c_master_dev_handle_t m_device{};
  bno085_handle_t m_sensor{};

  esp_err_t start_sensor() noexcept;
  void recover_sensor() noexcept;
  void update_position(const bno085_sensor_value_t &value) noexcept;
  void detect_hit(const bno085_sensor_value_t &value) noexcept;
  void detect_hit_angular(const bno085_sensor_value_t &value) noexcept;

  static void on_sensor_data(bno085_handle_t,
                             const bno085_sensor_value_t *value,
                             void *context) noexcept;
  static void reset_button_pressed(void *args) noexcept;
};

} // namespace bh

#endif
