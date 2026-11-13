#include "ButtonPress.hpp"
#include "Drumstick.hpp"
#include "Utils.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sleep.h"
#include "nvs_flash.h"

extern "C" void app_main() {
  auto nvs_result = ::nvs_flash_init();
  if (nvs_result == ESP_ERR_NVS_NO_FREE_PAGES ||
      nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW("sticks", "Reinitializing NVS after %s; saved receiver MAC is lost",
             ::esp_err_to_name(nvs_result));
    ESP_ERROR_CHECK(::nvs_flash_erase());
    nvs_result = ::nvs_flash_init();
  }
  ESP_ERROR_CHECK(nvs_result);
  ESP_ERROR_CHECK(::esp_netif_init());

  {
    bh::Drumstick drumstick;
    drumstick.loop();
  }

  // The loop returns only for a long press; resources have now been released.
  // Wait for a settled release before arming low-level button wakeup.
  do {
    while (::gpio_get_level(bh::STATE_BUTTON_PIN) == 0) {
      ::vTaskDelay(pdMS_TO_TICKS(bh::BUTTON_DEBOUNCE_MS));
    }
    ::vTaskDelay(pdMS_TO_TICKS(bh::BUTTON_DEBOUNCE_MS));
  } while (::gpio_get_level(bh::STATE_BUTTON_PIN) == 0);

  for (const auto pin :
       {bh::GREEN_LED_PIN, bh::RED_LED_PIN, bh::SENSOR_RESET_PIN}) {
    ESP_ERROR_CHECK(::gpio_set_level(pin, 0));
    ESP_ERROR_CHECK(::gpio_hold_en(pin));
  }
  ESP_ERROR_CHECK(::esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL));
  ESP_ERROR_CHECK(::esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown(
      1ULL << bh::STATE_BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW));
  ::esp_deep_sleep_start();
}
