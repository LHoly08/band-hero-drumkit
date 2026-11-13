# Drum base firmware

ESP32-S3 receiver with Lua pad configuration and freestyle I2S audio playback.
The firmware starts in an unfinished configuration state; mode selection and
SD mounting must be implemented before it can play from boot.

## Build and flash

From `base/` in an activated ESP-IDF 6 environment, use CMake 3.31 or newer
and a C++26 toolchain supporting `<meta>` and `-freflection`:

```sh
idf.py -DMAC=001122334455 build
idf.py -p /dev/ttyACM0 flash monitor
```

Use your board's actual serial port. CMake forces the `esp32s3` target and
generates defaults for 8 MB flash, octal PSRAM at 80 MHz, and USB Serial/JTAG
console. Check those settings against your board with `idf.py menuconfig`.
Audio requires core 1, so single-core mode must be disabled.

`MAC` is the downstream hub's Wi-Fi station address: exactly 12 hexadecimal
digits without colons. Its default is the placeholder `000000000000`; the
base does not currently accept serial commands to change it. Configure each
stick's receiver separately to the base's station MAC and use the same Wi-Fi
channel for communicating devices.

Initial configuration downloads pinned u8g2 and Lua sources into the build
directory, plus the managed audio codec/effects dependencies. Keep
`main/idf_component.yml` and `dependencies.lock` with the project.

## Audio playback

`bh::AudioPlayer` sends mono signed 16-bit PCM to a Philips I2S DAC or
amplifier. Its FreeRTOS task runs on ESP32-S3 core 1 at priority 5. Mixing
and I2S writes run in that task; pad hits only enqueue playback requests.
The hardware must accept standard I2S with 16-bit slots. No I2C codec control
or amplifier enable pin is configured by this driver.

The wiring is hardcoded in `main/Audio.cpp`: GPIO16 to BCK, GPIO17 to LCK/LRCK,
and GPIO18 to DIN, with MCLK disabled (`GPIO_NUM_NC`).
GPIO5, GPIO6, GPIO7, and GPIO10 remain available for four ADC1 inputs, and
GPIO8 (SDA) / GPIO9 (SCL) remain available for I2C. The audio driver does not
configure these reserved analog/I2C pins. This allocation avoids the default
SDMMC pins and the octal PSRAM pins. Check that your board exposes these GPIOs;
see [ESP32-S3 GPIO capabilities](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/gpio.html).

Loaded PCM and I2S output use the compile-time constant
`bh::audioSampleRate` (24000 Hz) in `include/Utils.hpp`. MP3 files can keep their
original sample rate on the SD card, including 44100 and 48000 Hz. During loading,
the ESP downmixes stereo to mono and uses
[Espressif Audio Effects](https://github.com/espressif/esp-adf-libs/tree/master/esp_audio_effects)
to resample each
decoded frame to this shared rate before storing it in PSRAM. Files already at
the shared rate bypass resampling. Playback uses mono, 16-bit PCM; the mixer does
not resample. Source files are left unchanged.

Conversion runs once per load and adds loading time, with temporary decoder and
resampler memory released afterward. It does not retain a full source-rate PCM
copy. Optionally convert sounds beforehand to avoid on-device resampling:

```sh
ffmpeg -i original.mp3 -ar 24000 -ac 1 -c:a libmp3lame -b:a 96k converted.mp3
```

Trim unwanted silence and tails in the source sounds to save more memory.
Each second of decoded audio uses 48000 bytes of PSRAM (46.875 KiB).

For the PCM5102, `bclk` connects to BCK, `ws` to LCK/LRCK, and `dout` to DIN.
The same mono signal is sent to both DAC outputs. The PCM5102A supports
[three-wire I2S using its internal PLL](https://www.ti.com/product/PCM5102A),
so MCLK is disabled for that mode. Follow the
breakout's wiring instructions for its SCK, format, and mute straps.

Freestyle play starts the audio task and sends normal, alternate, and pedal
sounds to it. A pedal can receive its sound through
`bh::Pad<bh::PadType::Pedal, false>{pin, bh::mp3File{path}}`. Stick pads
already receive their normal and alternate audio through their constructors.
Hit speed currently does not change playback volume.

Up to 64 voices can overlap, including repeated hits on the same sound.
Samples are summed and saturated to the signed 16-bit range. There is also a
64-request queue. Overload logs a warning and drops new hits; existing sounds
continue. Mixing happens in 128-sample blocks with four DMA buffers.
The practical CPU and latency limits require measurement on the board.

`AudioPlayer::play()` borrows the file's PCM buffer. Keep the file alive and
do not move-assign or destroy it until `stop()` returns. The freestyle player
stops its audio worker before destroying its pads. Use `start()`, `play()`
and `stop()` from the same producer task, not from an ISR.

The application still boots into an unfinished Config state, and SD mounting
is not yet connected to the startup flow. These must be completed before the
drumkit can play from boot. Playing modes now populate pads from the Lua `hit`
array. `Drum`, `Cymbal`, and `Pedal` constants are registered before executing
`config.lua`. Pedal positions are integers 1–4 mapped to GPIO1–GPIO4; positions
must be unique. The limit is 30 total pads, including at most four pedals.

Stick pads require finite numeric `position.x/y/z`, a positive `radius`, and an
`audio` path. `tilt` is optional. Supply `alternate_radius` and `alternate_audio`
together; the alternate radius must be positive and no larger than the pad.
Malformed fields and capacity overflow report configuration errors. Only
freestyle decodes audio; serial/hub modes do not need PCM buffers. Setup lives
in `main/Playing.cpp` and remains shared and non-inlined across playing modes.

The configuration path is currently the relative `config.lua` string in
`include/Utils.hpp`; no example configuration is shipped. When implementing
SD startup, connect this path and each audio path to the mounted filesystem.
The `hit` table is a consecutive Lua array starting at index 1; loading stops
at the first missing entry. Lua standard libraries are not opened by the
current entry point. Configuration or audio setup failures enter `ConfigError`,
whose UI is also unfinished.

There is currently no host test suite in this repository. Validate pad loading
and audio playback on the board after the startup flow is connected.

## Wireless stick input

All playing modes receive the same 16-byte `bh::Packet` from
`../shared/HitPacket.hpp`: four little-endian IEEE-754 floats in the order
X, Y, Z, speed. Positions are chest-relative meters (X right, Y forward, Z up),
matching `SticksTestAudio`'s `HitXYZ!` input. Speed is the detector's peak scaled
angular value from its `Hit!` record; it is not meters per second.

The receiver checks that every incoming hit is a complete 16-byte packet. Freestyle queues the full `bh::Packet`;
serial/hub modes queue just its `bh::Vec3` coordinates. All modes use XYZ for pad
matching. Serial/hub output to the game remains the
existing note bitmask. Hit speed still does not change base playback volume.
Flash the matching stick firmware as well: legacy angle packets have the same
byte length and cannot be distinguished from XYZ packets by this format.

In serial/hub modes, pedal bits come first and stick-pad bits follow in their
configured order. Output is a four-byte note value sent only when it changes.
The current implementation evaluates these outputs only when a stick packet
arrives and ORs new hits into the previous value without clearing old bits;
repeated-hit and release handling remains unfinished.
