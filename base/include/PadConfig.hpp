#ifndef BH_PAD_CONFIG
#define BH_PAD_CONFIG

#include "HitPacket.hpp"
#include <lua.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <meta>
#include <string>

namespace bh {

enum class PadType : std::uint8_t { Drum = 0, Cymbal, Pedal };

struct PadConfig {
  PadType type{};
  bh::Vec3 position{};
  float radius{};
  float tilt{};
  std::size_t pedalIndex{};
  std::string audio;
  bool hasAlternate{};
  float alternateRadius{};
  std::string alternateAudio;
};

inline void registerPadTypes(::lua_State *lua) {
  template for (constexpr auto Pad_t : std::define_static_array(
                    std::meta::enumerators_of(^^PadType))) {
    constexpr auto padValue = [:Pad_t:];
    constexpr auto padName =
        std::define_static_string(std::meta::identifier_of(Pad_t));
    ::lua_pushinteger(lua, static_cast<::lua_Integer>(padValue));
    ::lua_setglobal(lua, padName);
  }
}

// Always restore the caller's Lua stack, including on malformed fields.
inline bool readPadConfig(::lua_State *lua, int table, PadConfig &out,
                          const char *&error) {
  const int top = ::lua_gettop(lua);
  struct StackGuard {
    ::lua_State *lua;
    int top;
    ~StackGuard() { ::lua_settop(lua, top); }
  } guard{lua, top};
  out = {};
  error = nullptr;
  table = ::lua_absindex(lua, table);
  auto fail = [&](const char *message) {
    error = message;
    return false;
  };
  if (!lua_istable(lua, table)) {
    return fail("Pad entry must be a table");
  }
  auto number = [&](int source, const char *key, float &value) {
    ::lua_getfield(lua, source, key);
    const bool numeric = ::lua_type(lua, -1) == LUA_TNUMBER;
    value = numeric ? static_cast<float>(lua_tonumber(lua, -1)) : 0.0f;
    lua_pop(lua, 1);
    return numeric && std::isfinite(value);
  };
  auto path = [&](const char *key, std::string &value) {
    ::lua_getfield(lua, table, key);
    const bool string = ::lua_type(lua, -1) == LUA_TSTRING;
    std::size_t length = 0;
    const char *text = string ? ::lua_tolstring(lua, -1, &length) : nullptr;
    // Copy the path while the Lua string is on the stack. The kit outlives the Lua
    // state.
    value = text ? std::string{text, length} : std::string{};
    lua_pop(lua, 1);
    return !value.empty() && value.find('\0') == std::string::npos;
  };
  auto present = [&](const char *key) {
    ::lua_getfield(lua, table, key);
    const bool exists = !lua_isnil(lua, -1);
    lua_pop(lua, 1);
    return exists;
  };
  ::lua_getfield(lua, table, "type");
  int integer = 0;
  const auto type = ::lua_tointegerx(lua, -1, &integer);
  const bool numericType = ::lua_type(lua, -1) == LUA_TNUMBER;
  lua_pop(lua, 1);
  if (!numericType || !integer || type < 0 || type > 2) {
    return fail("Pad type must be Drum, Cymbal or Pedal");
  }
  out.type = static_cast<PadType>(type);
  if (!path("audio", out.audio)) {
    return fail("Audio must be a nonempty file path");
  }
  if (out.type == PadType::Pedal) {
    float position;
    if (!number(table, "position", position) || position < 1 || position > 4 ||
        std::trunc(position) != position) {
      return fail("Position for Pedal must be an integer between 1 and 4");
    }
    out.pedalIndex = static_cast<std::size_t>(position) - 1;
    return true;
  }
  ::lua_getfield(lua, table, "position");
  const int position = ::lua_gettop(lua);
  if (!lua_istable(lua, position) || !number(position, "x", out.position.x) ||
      !number(position, "y", out.position.y) ||
      !number(position, "z", out.position.z)) {
    return fail("Position must contain finite numeric x, y and z");
  }
  lua_pop(lua, 1);
  if (!number(table, "radius", out.radius) || out.radius <= 0 ||
      !std::isfinite(out.radius * out.radius)) {
    return fail("Radius must be positive with a finite square");
  }
  if (present("tilt") && !number(table, "tilt", out.tilt)) {
    return fail("Tilt must be a finite number");
  }
  const bool alternateRadius = present("alternate_radius");
  const bool alternateAudio = present("alternate_audio");
  if (alternateRadius != alternateAudio) {
    return fail("Alternate radius and audio must be supplied together");
  }
  out.hasAlternate = alternateRadius;
  if (out.hasAlternate &&
      (!number(table, "alternate_radius", out.alternateRadius) ||
       out.alternateRadius <= 0 || out.alternateRadius > out.radius ||
       !path("alternate_audio", out.alternateAudio))) {
    return fail("Alternate requires a radius within the pad and an audio path");
  }
  return true;
}

} // namespace bh
#endif
