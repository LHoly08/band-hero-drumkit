#include <lua.hpp>

#include <cstdio>
#include <cstring>

#include "Playing.hpp"
#include "Utils.hpp"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_netif.h"
#include "esp_psram.h"
#include "esp_vfs_fat.h"
#include "nvs_flash.h"
#include "sdmmc_cmd.h"

#include "driver/sdmmc_host.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" void app_main() {
  /*
  #if CONFIG_SPIRAM
    const std::size_t psram_size = ::esp_psram_get_size();
    std::printf("Total PSRAM: %zu bytes (%zu MiB)\n", psram_size,
                psram_size / (1024 * 1024));

    if (::esp_psram_is_initialized()) {
      constexpr char message[] = "Hello from PSRAM!";
      auto *psram_string = static_cast<char *>(
          ::heap_caps_malloc(sizeof(message), MALLOC_CAP_SPIRAM |
  MALLOC_CAP_8BIT)); if (psram_string) { std::memcpy(psram_string, message,
  sizeof(message)); std::printf("String stored in PSRAM: %s\n", psram_string);
        ::heap_caps_free(psram_string);
      } else {
        std::printf("PSRAM string allocation failed\n");
      }
    } else {
      std::printf("PSRAM is not initialized\n");
    }
  #else
    std::printf("PSRAM support is disabled in sdkconfig\n");
  #endif

    ::sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    ::sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    ::esp_vfs_fat_sdmmc_mount_config_t config{};
    config.format_if_mount_failed = false;
    config.max_files = 5;
    config.allocation_unit_size = 16 * 1024;

    ::sdmmc_card_t *card = nullptr;
    ::esp_err_t err =
        ::esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &config, &card);

    if (err != ESP_OK) {
      std::printf("SD mount failed: %s\n", ::esp_err_to_name(err));
      return;
    }

    ::sdmmc_card_print_info(stdout, card);

    // Append a line each time the board boots.
    std::FILE *file = std::fopen("/sdcard/test.txt", "a");
    if (file) {
      if (std::fputs("Hello from ESP32-CAM!\n", file) < 0)
        std::perror("Write failed");

      if (std::fclose(file) != 0)
        std::perror("Close failed");
    } else {
      std::perror("Open failed");
    }

    ESP_ERROR_CHECK(::esp_vfs_fat_sdcard_unmount("/sdcard", card));
  */
  ESP_ERROR_CHECK(::nvs_flash_init());
  ESP_ERROR_CHECK(::esp_netif_init());
  ESP_ERROR_CHECK(::esp_event_loop_create_default());

  constexpr std::uint64_t macAddressComptime = BH_MAC;

  bh::AtomicState state{bh::State::Config};

  bh::MAC macAddress{static_cast<std::uint8_t>(macAddressComptime >> (8 * 5)),
                     static_cast<std::uint8_t>(macAddressComptime >> (8 * 4)),
                     static_cast<std::uint8_t>(macAddressComptime >> (8 * 3)),
                     static_cast<std::uint8_t>(macAddressComptime >> (8 * 2)),
                     static_cast<std::uint8_t>(macAddressComptime >> (8 * 1)),
                     static_cast<std::uint8_t>(macAddressComptime >> (8 * 0))};

  auto play = [&]<bh::State S>
    requires bh::Playing<S>
  -> void {
      lua_State *L = luaL_newstate();

      if (bool error{false}; luaL_dofile(L, bh::configFile.data()) == LUA_OK)
          [[likely]] {

        if constexpr (S == bh::State::PlayingHub) {
          bh::Play<S> play({.state = state, .macAddress = macAddress}, L,
                           &error);

          if (error) {
            goto ret;
          }
          play.loop();

        } else {
          bh::Play<S> play({.state = state}, L, &error);

          if (error) {
            goto ret;
          }
          play.loop();
        }
      }

    ret:
      lua_close(L);
      return;
    };

infinite_loop:

  switch (state.load(std::memory_order_relaxed)) {
  case bh::State::Config: {
    break;
  }
  case bh::State::ConfigError: {
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
