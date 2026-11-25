#include "Config.hpp"
#include "KitConfig.hpp"
#if BH_OLED
#include "Oled.hpp"
#endif
#include "Playing.hpp"
#include "Utils.hpp"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" void app_main() {
  ESP_ERROR_CHECK(::nvs_flash_init());
  ESP_ERROR_CHECK(::esp_netif_init());
  ESP_ERROR_CHECK(::esp_event_loop_create_default());

  bh::AtomicState state{bh::State::Config};
  // The kit outlives both modes. The web editor updates it before the selected play
  // mode takes a reference.
  bh::KitConfig kit;
  if (const char *error = kit.load()) {
    ESP_LOGW("Kit", "%s", error);
  }
#if BH_OLED
  bh::Oled oled{};
#endif

  const char *errorString = nullptr;
  auto play = [&]<bh::State S>
    requires bh::Playing<S>
  -> void {
      auto fail = [&](const char *message) {
        ESP_LOGE("Play", "%s", message ? message : "Configuration failed");
        state.store(bh::State::ConfigError, std::memory_order_relaxed);
      };

      if (!kit.hasFile) {
        if (const char *error = kit.load()) {
          fail(error);
          return;
        }
        if (!kit.hasFile) {
          fail("Missing config.lua");
          return;
        }
      }
      bool error = false;
#if BH_OLED
      oled.clearBuffer();
      oled.drawLoading();
      oled.sendBuffer();
#else
      std::puts("Loading...");
#endif
      bh::Play<S> play(state, kit, &error, errorString);
      if (error) {
        fail(errorString);
        return;
      }
#if BH_OLED
      oled.clearBuffer();
      oled.drawState<S>();
      oled.sendBuffer();
#else
      std::puts(bh::stateName(S));
#endif
      play.loop();
    };

infinite_loop:

  switch (state.load(std::memory_order_relaxed)) {
  case bh::State::Config: {
#if BH_OLED
    bh::Config config(state, kit, oled);
#else
    bh::Config config(state, kit);
#endif
    config.loop();

    break;
  }
  case bh::State::ConfigError: {
    ::vTaskDelay(pdMS_TO_TICKS(20));
    break;
  }
  case bh::State::PlayingFreestyle: {
    play.template operator()<bh::State::PlayingFreestyle>();

    break;
  }
  case bh::State::PlayingHub: {
    play.template operator()<bh::State::PlayingHub>();

    break;
  }
  case bh::State::PlayingSerial: {
    play.template operator()<bh::State::PlayingSerial>();

    break;
  }
  }

  goto infinite_loop;
}
