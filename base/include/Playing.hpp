#ifndef BH_PLAY
#define BH_PLAY

#include "Pads.hpp"
#include "Utils.hpp"

#include "esp_attr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "lua.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <inplace_vector>
#include <lua.hpp>
#include <meta>
#include <ranges>
#include <string_view>
#include <utility>
#include <variant>

namespace bh {

template <bh::State S>
concept Playing =
    S == bh::State::PlayingSerial || S == bh::State::PlayingFreestyle ||
    S == bh::State::PlayingHub;

namespace detail {

template <bh::State S>
  requires Playing<S>
struct NeededReferences {
  bh::AtomicState &state;
};

template <> struct NeededReferences<bh::State::PlayingHub> {
  bh::AtomicState &state;
  const bh::MAC &macAddress;
};

template <bh::State S>
  requires Playing<S>
struct RequiredPacketType {
  using Packet_t = bh::Vec3;
};

template <> struct RequiredPacketType<bh::State::PlayingFreestyle> {
  using Packet_t = bh::Packet;
};

struct Args {
  ::lua_State *luaFile;
  bool *error;
  const char *&errorString;
};

} // namespace detail

// Keep setup shared between playing modes.
[[gnu::noinline]]
void setup(void *context, bh::State state, ::lua_State *luaFile, bool *error,
           const char *&errorString);

template <bh::State S>
  requires Playing<S>
class Play {
public:
  Play(detail::NeededReferences<S> references, ::lua_State *luaFile, bool *error,
       const char *&errorString) noexcept;
  ~Play() noexcept;

  void loop() noexcept;

private:
  static Play<S> *instance;

  static void ReceivedCallback(const ::esp_now_recv_info_t *esp_now_info,
                               const std::uint8_t *data, int data_len) noexcept;

  friend void setup(void *context, bh::State state, ::lua_State *luaFile,
                    bool *error, const char *&errorString);

  detail::NeededReferences<S> m_references;
  std::uint32_t m_note{};

  using Packet_t = detail::RequiredPacketType<S>::Packet_t;
  bh::Queue<Packet_t, 5> m_queue;
  std::inplace_vector<bh::Pad<bh::PadType::Pedal, false>, 4> m_pedals;
  std::inplace_vector<bh::StickPlayable, 30> m_sticks;
  // Declared last: stops the worker before any pad PCM can be destroyed.
  bh::AudioPlayer m_audio;
};

template <bh::State S>
  requires Playing<S>
Play<S> *Play<S>::instance = nullptr;

// ----- Setup Definition -----

void setup(void *context, bh::State state, ::lua_State *luaFile, bool *error,
           const char *&errorString) {

  std::variant<Play<bh::State::PlayingFreestyle> *,
               Play<bh::State::PlayingHub> *, Play<bh::State::PlayingSerial> *>
      play;
  template for (constexpr auto State_ti : std::define_static_array(
                    std::meta::enumerators_of(^^bh::State) |
                    std::ranges::views::drop(2))) {
    constexpr bh::State State_t = [:State_ti:];
    if (state == State_t) {
      play = static_cast<Play<State_t> *>(context);
    }
  }

  {
    const auto result = ::gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (result != ESP_ERR_INVALID_STATE) {
      ESP_ERROR_CHECK(result);
    }
    {
      ::wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
      ESP_ERROR_CHECK(::esp_wifi_init(&cfg));
      ESP_ERROR_CHECK(::esp_wifi_set_mode(::WIFI_MODE_STA));
      ESP_ERROR_CHECK(::esp_wifi_start());

      ESP_ERROR_CHECK(::esp_now_init());
    }
  }
  {
    using ISR_CFG = bh::OppositeEdge<true>;

    ::gpio_config_t cfg;

    cfg.pin_bit_mask = 1ULL << bh::PIN;
    cfg.mode = ::GPIO_MODE_INPUT;
    cfg.pull_up_en = (gpio_pullup_t)ISR_CFG::pull_up;
    cfg.pull_down_en = (gpio_pulldown_t)ISR_CFG::pull_down;
    cfg.intr_type = (gpio_int_type_t)ISR_CFG::intr_type;

    ESP_ERROR_CHECK(::gpio_config(&cfg));
  }

  std::visit(
      [](auto *selected) {
        selected->instance = selected;
        ESP_ERROR_CHECK(::gpio_isr_handler_add(
            bh::PIN, modeButtonPressed, &selected->m_references.state));
        ESP_ERROR_CHECK(
            ::esp_now_register_recv_cb(selected->ReceivedCallback));
      },
      play);

  static constexpr auto PadTypes =
      std::define_static_array(std::meta::enumerators_of(^^bh::PadType));
  static constexpr auto PadTypes_v = PadTypes | std::ranges::views::take(2);

  template for (constexpr auto type : PadTypes) {
    lua_pushinteger(luaFile, std::to_underlying([:type:]));
    lua_setglobal(luaFile, std::meta::identifier_of(type).data());
  }

  lua_getglobal(luaFile, "hit");

  if (!lua_istable(luaFile, -1)) {
    *error = true;
    errorString = "Hit table not found";
    return;
  }

  std::uint32_t index{};

  auto getPosition = [&](bool *ret) -> bh::Vec3 {
    bh::Vec3 position{};

    lua_getfield(luaFile, -1, "x");
    if (lua_isnil(luaFile, -1)) {
      *error = true;
      *ret = true;
      return {};
    }
    position.x = static_cast<float>(lua_tonumber(luaFile, -1));
    lua_pop(luaFile, 1);

    lua_getfield(luaFile, -1, "y");
    if (lua_isnil(luaFile, -1)) {
      *error = true;
      *ret = true;
      return {};
    }
    position.y = static_cast<float>(lua_tonumber(luaFile, -1));
    lua_pop(luaFile, 1);

    lua_getfield(luaFile, -1, "z");
    if (lua_isnil(luaFile, -1)) {
      *error = true;
      *ret = true;
      return {};
    }
    position.z = static_cast<float>(lua_tonumber(luaFile, -1));
    lua_pop(luaFile, 1);

    return position;
  };

  auto error_s = [](const char *type,
                    std::string_view s) consteval -> const char * {
    std::string error = type;
    error.append(" for ");
    error.append(s.data());
    error.append(" not found");

    return std::define_static_string(error);
  };

  while (true) {
    lua_pushnumber(luaFile, ++index);
    lua_gettable(luaFile, -2);

    if (!lua_istable(luaFile, -1)) {
      break;
    }

    lua_getfield(luaFile, -1, "type");
    auto type = static_cast<bh::PadType>(lua_tointeger(luaFile, -1));
    lua_pop(luaFile, 1);

    switch (type) {
    case bh::PadType::Cymbal:
    case bh::PadType::Drum: {
      lua_getfield(luaFile, -1, "position");

      if (!lua_istable(luaFile, -1)) {
        *error = true;
        template for (constexpr auto Type : PadTypes_v) {
          if (type == [:Type:]) {
            errorString = error_s("Position", std::meta::identifier_of(Type));
          }
        }
        return;
      }
      bool ret{false};
      const bh::Vec3 position = getPosition(&ret);
      if (ret) {
        return;
      }
      lua_pop(luaFile, 1);

      lua_getfield(luaFile, -1, "radius");
      if (lua_isnil(luaFile, -1)) {
        *error = true;
        template for (constexpr auto Type : PadTypes_v) {
          if (type == [:Type:]) {
            errorString = error_s("Radius", std::meta::identifier_of(Type));
          }
        }
        return;
      }
      const float radius = static_cast<float>(lua_tonumber(luaFile, -1));
      lua_pop(luaFile, 1);

      lua_getfield(luaFile, -1, "audio");
      if (lua_isnil(luaFile, -1)) {
        *error = true;
        template for (constexpr auto Type : PadTypes_v) {
          if (type == [:Type:]) {
            errorString = error_s("Audio", std::meta::identifier_of(Type));
          }
        }
        return;
      }
      const char *filename = lua_tostring(luaFile, -1);
      mp3File audio{filename};
      lua_pop(luaFile, 1);

      float tilt = 0.0f;
      lua_getfield(luaFile, -1, "tilt");
      if (!lua_isnil(luaFile, -1)) {
        tilt = static_cast<float>(lua_tonumber(luaFile, -1));
      }
      lua_pop(luaFile, 1);

      {
        lua_getfield(luaFile, -1, "alternate_radius");
        if (lua_isnil(luaFile, -1)) {
          lua_pop(luaFile, 1);
          goto noAlternate;
        }
        const float alternate_radius =
            static_cast<float>(lua_tonumber(luaFile, -1));
        lua_pop(luaFile, 1);

        lua_getfield(luaFile, -1, "alternate_audio");
        if (lua_isnil(luaFile, -1)) {
          lua_pop(luaFile, 1);
          goto noAlternate;
        }
        const char *alternate_filename = lua_tostring(luaFile, -1);
        mp3File alternate_audio{alternate_filename};
        lua_pop(luaFile, 1);

        template for (constexpr auto Type : PadTypes_v) {
          constexpr bh::PadType Pad_t = [:Type:];
          if (type == Pad_t) {
            bh::Alternate<Pad_t, true> alternate{alternate_radius,
                                                 std::move(alternate_audio)};

            bh::Pad<Pad_t, true> temp{position, radius, tilt, std::move(audio),
                                      std::move(alternate)};

            std::visit(
                [&temp](auto *selected) {
                  selected->m_sticks.emplace_back(std::move(temp));
                },
                play);
          }
        }
      }
      break;

    noAlternate: {
      template for (constexpr auto Type : PadTypes_v) {
        constexpr bh::PadType Pad_t = [:Type:];
        if (type == Pad_t) {
          bh::Pad<Pad_t, false> temp{position, radius, tilt, std::move(audio)};

          std::visit(
              [&temp](auto *selected) {
                selected->m_sticks.emplace_back(std::move(temp));
              },
              play);
        }
      }
    } break;
    }
    case bh::PadType::Pedal: {
      static constexpr std::array<::gpio_num_t, 4> MAPPED_POSITIONS_PINS = {
          GPIO_NUM_1,
          GPIO_NUM_2,
          GPIO_NUM_3,
          GPIO_NUM_4,
      };

      lua_getfield(luaFile, -1, "position");

      if (lua_isnil(luaFile, -1)) {
        *error = true;
        errorString = "Position for Pedal not found";
        return;
      }
      std::int32_t position =
          static_cast<std::int32_t>(lua_tointeger(luaFile, -1));
      if (position < 0 ||
          static_cast<std::size_t>(position) >= MAPPED_POSITIONS_PINS.size()) {
        *error = true;
        errorString = "Position for Pedal must be between 1 and 4";
        return;
      }
      lua_pop(luaFile, 1);

      lua_getfield(luaFile, -1, "audio");
      if (lua_isnil(luaFile, -1)) {
        *error = true;
        errorString = "Audio for Pedal not found";
        return;
      }
      const char *filename = lua_tostring(luaFile, -1);
      mp3File audio{filename};
      lua_pop(luaFile, 1);

      std::visit(
          [position, &audio](auto *selected) {
            selected->m_pedals.emplace_back(MAPPED_POSITIONS_PINS[position],
                                           std::move(audio));
          },
          play);
      break;
    }
    }
    lua_pop(luaFile, 1);
    if (std::visit([](auto *selected) {
          return selected->m_sticks.size() + selected->m_pedals.size() == 30;
        }, play)) {
      break;
    }
  }
}

// ----- End of Setup Definition -----

// ----- Definitions -----

template <bh::State S>
  requires Playing<S>
Play<S>::Play(detail::NeededReferences<S> references, ::lua_State *luaFile, bool *error,
              const char *&errorString) noexcept
    : m_references{references} {
  assert(error != nullptr);

  if constexpr (S == bh::State::PlayingFreestyle) {
    const auto result = m_audio.start();

    if (result != ESP_OK) {
      ESP_LOGE("Play", "Cannot start audio: %s", ::esp_err_to_name(result));
      return;
    }
  }

  setup(this, S, luaFile, error, errorString);
}

template <bh::State S>
  requires Playing<S>
Play<S>::~Play() noexcept {
  if (instance == this) {
    ::esp_now_unregister_recv_cb();
    ::esp_now_deinit();
    ::esp_wifi_stop();
    ::esp_wifi_deinit();
    ::gpio_isr_handler_remove(bh::PIN);
    instance = nullptr;
  }
  if constexpr (S == bh::State::PlayingFreestyle) {
    m_audio.stop();
  }
}

template <bh::State S>
  requires Playing<S>
void Play<S>::ReceivedCallback(const ::esp_now_recv_info_t *esp_now_info,
                               const std::uint8_t *data,
                               int data_len) noexcept {
  if (instance == nullptr || esp_now_info == nullptr ||
      esp_now_info->src_addr == nullptr || data == nullptr ||
      data_len != sizeof(bh::Packet)) {
    return;
  }

  Packet_t packet;
  std::memcpy(&packet, data, sizeof(packet));

  instance->m_queue.push(packet);
}

template <bh::State S>
  requires Playing<S>
void Play<S>::loop() noexcept {

  while (m_references.state.load(std::memory_order_relaxed) == S) {

    if constexpr (S == bh::State::PlayingFreestyle) {
      for (const auto &pedal : m_pedals) {
        pedal.play(m_audio);
      }
    }

    if (Packet_t packet{}; m_queue.pop(&packet)) [[likely]] {

      if constexpr (S != bh::State::PlayingFreestyle) {

        const std::uint8_t pedalsSize = m_pedals.size();

        std::uint32_t val = m_note;

        for (std::uint8_t i{}; i < pedalsSize; ++i) {
          val |= ((1u << i) * m_pedals[i].pressed());
        }

        for (std::uint8_t i{}; i < m_sticks.size(); ++i) {
          const bool isInside = std::visit(
              [&packet](const auto &pad) -> bool {
                return pad.inside(packet, nullptr);
              },
              m_sticks[i]);
          if (isInside) {
            val |= (1u << (i + pedalsSize));
            break;
          }
        }

        if (val != m_note) {
          if constexpr (S == bh::State::PlayingSerial) {

            std::fwrite(&val, 1, sizeof(val), stdout);
            std::fflush(stdout);

          } else if constexpr (S == bh::State::PlayingHub) {

            ::esp_err_t status = ::esp_now_send(
                m_references.macAddress.data(),
                reinterpret_cast<const std::uint8_t *>(&val), sizeof(val));

            for (unsigned char i{}; i < 4 && status != ESP_OK; ++i) {

              status = ::esp_now_send(
                  m_references.macAddress.data(),
                  reinterpret_cast<const std::uint8_t *>(&val), sizeof(val));
            }
          }
          m_note = val;
        }
      } else { // ----- Play Freestyle -----

        for (const auto &pad : m_sticks) {
          const bool isInside = std::visit(
              [this, &packet](const auto &pad) -> bool {
                float radialSquared{};
                const bool hit = pad.inside(packet.coords, &radialSquared);

                if (hit) {
                  pad.play(m_audio, radialSquared, packet.speed);
                }
                return hit;
              },
              pad);
          if (isInside) {
            break;
          }
        }
      }
    }
  }
}

// ----- End of Definitions -----

} // namespace bh

#endif
