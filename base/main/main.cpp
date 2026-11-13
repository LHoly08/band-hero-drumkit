#include <lua.hpp>

#include <cstdio>
#include <memory>

#include "Playing.hpp"
#include "Utils.hpp"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
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
      bh::registerPadTypes(lua.get());
      if (luaL_dofile(lua.get(), bh::configFile.data()) != LUA_OK) {
        fail(lua_tostring(lua.get(), -1));
        return;
      }
      bool error = false;
      const char *errorString = nullptr;
      if constexpr (S == bh::State::PlayingHub) {
        bh::Play<S> play({.state = state, .macAddress = macAddress},
                         lua.get(), &error, errorString);
        if (error) {
          fail(errorString);
          return;
        }
        play.loop();
      } else {
        bh::Play<S> play({.state = state}, lua.get(), &error, errorString);
        if (error) {
          fail(errorString);
          return;
        }
        play.loop();
      }
    };

infinite_loop:

  switch (state.load(std::memory_order_relaxed)) {
  case bh::State::Config: {
    ::vTaskDelay(pdMS_TO_TICKS(20));
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
