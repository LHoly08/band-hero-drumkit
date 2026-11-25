#include "KitConfig.hpp"
#if BH_ESP32_CAM_SD
#include "driver/sdmmc_host.h"
#else
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#endif
#include "esp_vfs_fat.h"
#include <cerrno>
#include <cmath>
#include <memory>
#include <sys/stat.h>

namespace {

::esp_err_t mountSdCard(const char *mountPoint, ::sdmmc_card_t **card) {
  ::esp_vfs_fat_mount_config_t mountConfig{};
  mountConfig.max_files = 4;
#if BH_ESP32_CAM_SD
  const ::sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  ::sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  // Classic ESP32 slot 1: CLK 14, CMD 15, D0 2.
  slot.width = 1;
  return ::esp_vfs_fat_sdmmc_mount(mountPoint, &host, &slot, &mountConfig, card);
#else
  const ::sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  const auto spiHost = static_cast<::spi_host_device_t>(host.slot);
  ::spi_bus_config_t bus{};
#if CONFIG_IDF_TARGET_ESP32
  bus.sclk_io_num = ::GPIO_NUM_14;
  bus.miso_io_num = ::GPIO_NUM_19;
  bus.mosi_io_num = ::GPIO_NUM_13;
  constexpr auto chipSelect = ::GPIO_NUM_33;
#elif CONFIG_IDF_TARGET_ESP32S3
  // XIAO ESP32S3 Sense expansion board, onboard microSD.
  constexpr auto chipSelect = ::GPIO_NUM_21;
  bus.sclk_io_num = ::GPIO_NUM_7; // D8
  bus.miso_io_num = ::GPIO_NUM_8; // D9
  bus.mosi_io_num = ::GPIO_NUM_9; // D10
#else
#error "No SD pin mapping defined for this ESP-IDF target"
#endif
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.data4_io_num = bus.data5_io_num = -1;
  bus.data6_io_num = bus.data7_io_num = -1;
  auto result = ::spi_bus_initialize(spiHost, &bus, SDSPI_DEFAULT_DMA);
  if (result != ESP_OK) {
    return result;
  }

  ::sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot.host_id = spiHost;
  slot.gpio_cs = chipSelect;
  result =
      ::esp_vfs_fat_sdspi_mount(mountPoint, &host, &slot, &mountConfig, card);
  if (result != ESP_OK) {
    ESP_ERROR_CHECK_WITHOUT_ABORT(::spi_bus_free(spiHost));
  }
  return result;
#endif
}

} // namespace

namespace bh {

bool prepareKitStorage() {
  static ::sdmmc_card_t *card = nullptr;
  if (!card) {
    const auto result = mountSdCard("/sdcard", &card);
    if (result != ESP_OK) {
      ESP_LOGE("Kit", "microSD mount failed (%s)", esp_err_to_name(result));
      return false;
    }
    ESP_LOGI("Kit", "microSD mounted at /sdcard");
  }

  // FAT cannot rename over an existing file. Recover an interrupted
  // replacement.
  struct stat info{};
  if (stat(configPath, &info) == 0) {
    return true;
  }
  if (errno != ENOENT) {
    return false;
  }
  if (stat(backupPath, &info) == 0) {
    return std::rename(backupPath, configPath) == 0;
  }
  return errno == ENOENT;
}

bool parseHubMac(std::string_view text, MAC &mac) {
  if (text.size() != 17 && text.size() != 12) {
    return false;
  }
  auto hex = [](char c) -> int {
    if (c >= '0' && c <= '9') {
      return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
      return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f') {
      return c - 'a' + 10;
    }
    return -1;
  };
  for (unsigned i = 0; i < mac.size(); ++i) {
    unsigned pos = i * (text.size() == 17 ? 3 : 2);
    if (pos && text.size() == 17 && text[pos - 1] != ':') {
      return false;
    }
    int hi = hex(text[pos]), lo = hex(text[pos + 1]);
    if (hi < 0 || lo < 0) {
      return false;
    }
    mac[i] = (hi << 4) | lo;
  }
  return true;
}

const char *KitConfig::load() {
  *this = {};
  if (!prepareKitStorage()) {
    return "Cannot access SD card";
  }
  struct stat info{};
  if (stat(configPath, &info) != 0) {
    // A missing file opens an empty editor. hasFile stays false until a kit is
    // loaded or saved.
    return errno == ENOENT ? nullptr : "Cannot access config.lua";
  }
  std::unique_ptr<lua_State, decltype(&lua_close)> lua(luaL_newstate(),
                                                       lua_close);
  if (!lua) {
    return "Cannot allocate Lua state";
  }
  registerPadTypes(lua.get());
  if (luaL_dofile(lua.get(), configPath) != LUA_OK) {
    ESP_LOGE("Kit", "%s", lua_tostring(lua.get(), -1));
    return "Cannot load config.lua";
  }
  return read(lua.get());
}

const char *KitConfig::read(lua_State *lua) {
  // Start with the build-time MAC and default tolerance. Publish the new kit only
  // after every pad validates.
  KitConfig next;
  lua_getglobal(lua, "tolerance");
  if (!lua_isnil(lua, -1)) {
    next.tolerance = lua_tonumber(lua, -1);
    if (lua_type(lua, -1) != LUA_TNUMBER || !std::isfinite(next.tolerance) ||
        next.tolerance < 0) {
      lua_pop(lua, 1);
      return "Tolerance must be a finite nonnegative distance";
    }
  }
  lua_pop(lua, 1);
  lua_getglobal(lua, "macAddress");
  bool valid = lua_isnil(lua, -1);
  if (lua_type(lua, -1) == LUA_TSTRING) {
    size_t size;
    const char *value = lua_tolstring(lua, -1, &size);
    valid = parseHubMac({value, size}, next.macAddress);
  }
  lua_pop(lua, 1);
  if (!valid) {
    return "Invalid hub MAC address";
  }
  lua_getglobal(lua, "hit");
  if (!lua_istable(lua, -1)) {
    return "Missing Lua hit table";
  }
  unsigned usedPedals = 0;
  for (int i = 1;; ++i) {
    lua_rawgeti(lua, -1, i);
    if (lua_isnil(lua, -1)) {
      break;
    }
    if (i > 30) {
      return "Kit exceeds 30 pads";
    }
    auto &pad = next.pads.emplace_back();
    const char *error = nullptr;
    if (!readPadConfig(lua, -1, pad, error)) {
      return error;
    }
    if (pad.type == PadType::Pedal) {
      unsigned mask = 1u << pad.pedalIndex;
      if (usedPedals & mask) {
        return "Pedal positions must be unique";
      }
      usedPedals |= mask;
    }
    lua_pop(lua, 1);
  }
  next.hasFile = true;
  *this = std::move(next);
  return nullptr;
}
} // namespace bh
