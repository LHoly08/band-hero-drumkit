# band-hero-drumkit

Two independent ESP-IDF projects make up the drumkit:

- [`sticks/`](sticks/README.md): ESP32-C6 firmware for the BNO085-equipped sticks.
  It estimates chest-relative tip positions and currently reports hits over the
  USB console. The README covers wiring, calibration, and receiver MAC setup.
- `base/`: drum-base firmware in development. `main/main.cpp` currently mounts
  an SD card at `/sdcard`, appends a line to `test.txt`, and unmounts the card.
  Pad geometry, audio, and playing-mode code are being developed under
  `include/` and `main/Pads.cpp`.

## Build

Use an activated ESP-IDF shell, CMake 3.31 or newer, and a C++26-capable
toolchain. Build from the project directory you want to work on:

```sh
cd sticks
idf.py -DMAC=001122334455 build
```

The base supports `esp32` (the default for a new configuration) and `esp32s3`.
From `base/`, use a separate build directory for each target, for example:

```sh
idf.py -B build-esp32 -DIDF_TARGET=esp32 build
```

For an ESP32-S3 board, use `-B build-esp32s3 -DIDF_TARGET=esp32s3` instead.
CMake downloads the pinned u8g2 component and Lua sources during configuration.
Each project has its own configuration and build directory; there is no combined
top-level build. The base is not yet a complete wireless hit receiver or audio
player.

## C++ namespace convention

Qualify symbols defined in another file, even when they belong to the current
namespace: `bh::` for project symbols, `std::` for the standard library, and
`::` for APIs and types in the global namespace. Keep same-file names
unqualified when lookup permits; avoid `using namespace` directives.

Retain required class and enum scopes, such as `Play<S>::loop()` and
`State::Config`. Macros have no namespace and stay unqualified, including
`ESP_ERROR_CHECK`, `pdMS_TO_TICKS`, and `stdout`.
