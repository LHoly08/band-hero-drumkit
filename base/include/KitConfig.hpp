#pragma once

#include "PadConfig.hpp"
#include "Utils.hpp"
#include <lua.hpp>
#include <vector>

namespace bh {

inline constexpr char configPath[] = "/sdcard/config.lua";
inline constexpr char backupPath[] = "/sdcard/config.bak";
inline constexpr float defaultTolerance = 0.05f;
inline constexpr MAC defaultHubMac = [] {
  MAC mac{};
  for (unsigned i = 0; i < mac.size(); ++i) {
    mac[i] = BH_MAC >> (8 * (5 - i)) & 0xff;
  }
  return mac;
}();

struct KitConfig {
  float tolerance = defaultTolerance;
  MAC macAddress = defaultHubMac;
  std::vector<PadConfig> pads;
  bool hasFile = false;

  const char *load();
  const char *read(lua_State *lua);
};

bool parseHubMac(std::string_view text, MAC &mac);
bool prepareKitStorage();
} // namespace bh
