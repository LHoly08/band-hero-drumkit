#include <lua.hpp>

#include <cstdio>
#include <memory>
#include <meta>
#include <string>

#include "Config.hpp"
#include "Oled.hpp"
#include "Playing.hpp"
#include "Utils.hpp"
#include "driver/sdmmc_host.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_vfs_fat.h"
#include "lauxlib.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" void app_main() {
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

  bh::Oled oled{};

  const char *errorString = nullptr;
  constexpr char sdMountPoint[] = "/sdcard";
  constexpr auto configPath = [sdMountPoint]() consteval -> auto {
    std::string path = sdMountPoint;
    path.append("/config.lua");
    return std::define_static_string(path);
  }();
  sdmmc_card_t *sdCard = nullptr;

  auto play = [&]<bh::State S>
    requires bh::Playing<S>
  -> void {
      std::unique_ptr<::lua_State, decltype(&::lua_close)> lua{
          ::luaL_newstate(), &::lua_close};

      auto fail = [&](const char *message) {
        ESP_LOGE("Play", "%s", message ? message : "Configuration failed");
        state.store(bh::State::ConfigError, std::memory_order_relaxed);
      };

      if (!lua) {
        fail("Cannot allocate Lua state");
        return;
      }
      if (!sdCard) {
        const sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
        slot.width = 1;
        slot.d0 = GPIO_NUM_11; // GPIO2 is reserved for a pedal.
        esp_vfs_fat_mount_config_t mountConfig{};
        mountConfig.max_files = 4;
        const auto result = esp_vfs_fat_sdmmc_mount(sdMountPoint, &host, &slot,
                                                    &mountConfig, &sdCard);

        if (result != ESP_OK) {
          ESP_LOGE("Play", "Cannot mount %s: %s", sdMountPoint,
                   esp_err_to_name(result));
          state.store(bh::State::ConfigError, std::memory_order_relaxed);
          return;
        }
      }
      bh::registerPadTypes(lua.get());
      if (luaL_dofile(lua.get(), configPath) != LUA_OK) {
        fail(lua_tostring(lua.get(), -1));
        return;
      }
      bool error = false;
      oled.drawLoading();

      if constexpr (S == bh::State::PlayingHub) {
        bh::Play<S> play({.state = state, .macAddress = macAddress}, lua.get(),
                         &error, errorString);
        if (error) {
          fail(errorString);
          return;
        }
        oled.drawState<S>();
        play.loop();
      } else {
        bh::Play<S> play({.state = state}, lua.get(), &error, errorString);
        if (error) {
          fail(errorString);
          return;
        }
        oled.drawState<S>();
        play.loop();
      }
    };

infinite_loop:

  switch (state.load(std::memory_order_relaxed)) {
  case bh::State::Config: {
    bh::Config config(state, macAddress, oled);
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
