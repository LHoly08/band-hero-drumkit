# band-hero-drumkit

Two independent ESP-IDF projects make up the drumkit:

- [`sticks/`](sticks/README.md): ESP32-C6 firmware for BNO085-equipped sticks.
  It estimates chest-relative tip positions and sends XYZ/speed hit packets
  over ESP-NOW. Debug builds also emit serial hit records.
- [`base/`](base/README.md): ESP32-S3 receiver, Lua pad configuration, and I2S
  audio playback. The Config UI and SD mounting/startup flow remain unfinished;
  the base does not yet enter a playing mode from boot.
- `shared/HitPacket.hpp`: the common 16-byte wire format. Flash matching base
  and stick firmware; legacy angle packets have the same size but different
  semantics.

## Build

Use an activated ESP-IDF 6 environment, CMake 3.31 or newer, and a C++26-capable
toolchain. The base also requires the compiler's reflection support. Each
project has its own configuration and build directory; there is no combined
root build.

```sh
cd sticks
idf.py -DMAC=001122334455 build
```

The base explicitly targets ESP32-S3 (including PSRAM and core-1 audio):

```sh
cd base
idf.py build
```

CMake downloads the pinned u8g2 component and Lua sources during the base's
initial configuration. See each project's README for wiring, configuration,
and host-test commands. Use `-DDEBUG=ON` for sticks serial hit capture.

## C++ namespace convention

Qualify symbols defined in another file, even when they belong to the current
namespace: `bh::` for project symbols, `std::` for the standard library, and
`::` for APIs and types in the global namespace. Keep same-file names
unqualified when lookup permits; avoid `using namespace` directives.

Retain required class and enum scopes, such as `Play<S>::loop()` and
`State::Config`. Macros have no namespace and stay unqualified, including
`ESP_ERROR_CHECK`, `pdMS_TO_TICKS`, and `stdout`.
