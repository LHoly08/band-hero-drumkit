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
  Coordinates m_position{};

  i2c_master_bus_handle_t m_bus{};
  i2c_master_dev_handle_t m_device{};
  bno085_handle_t m_sensor{};

  static void on_sensor_data(bno085_handle_t,
                             const bno085_sensor_value_t *value,
                             void *context) noexcept;
};

} // namespace bh

#endif
