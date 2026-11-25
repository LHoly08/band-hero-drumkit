# Drum base firmware

ESP32-S3 receiver with Lua pad configuration and freestyle I2S audio playback.
The firmware starts in configuration mode and mounts the XIAO ESP32S3 Sense
microSD slot at `/sdcard` at startup to load the kit. The editor refreshes it from the card on load,
and a successful save updates the in-memory kit used by all play modes.
The configuration-mode HTTP server runs on core 1 on dual-core builds.

## Build and flash

From `base/` in an activated ESP-IDF 6 environment, use CMake 3.31 or newer
and a C++26 toolchain supporting `<meta>` and `-freflection`:

```sh
idf.py -DMAC=001122334455 build
idf.py -p /dev/ttyACM0 flash monitor
```

For a classic ESP32 with a 4 MB flash chip and OLED disabled, build and flash
from the same activated environment:

```sh
idf.py -B build-esp32-nooled -DIDF_TARGET=esp32 -DOLED=false build
idf.py -B build-esp32-nooled -p /dev/ttyUSB0 flash
```

For the AI-Thinker ESP32-CAM onboard microSD socket, add `-DESP32_CAM_SD=ON`
to the classic build command. It selects 1-bit SDMMC on GPIO14 (CLK), GPIO15
(CMD), and GPIO2 (D0). Use `-DESP32_CAM_SD=OFF` to return to external SPI.

The classic build uses UART0 and internal RAM for decoded samples by default.
The total kit audio must fit in available RAM. Its SD pins are GPIO14 (SCK),
GPIO19 (MISO), GPIO13 (MOSI), and GPIO33 (CS). Pedal inputs 1–4 use GPIO25,
GPIO26, GPIO27, and GPIO32. I2S BCK, WS, and DOUT use GPIO16, GPIO17, and
GPIO18. The mode button uses GPIO0.

Use your board's actual serial port. The default target is `esp32s3`.
`sdkconfig.defaults` selects 8 MB flash, the large single-app partition, octal
PSRAM at 80 MHz, and the USB Serial/JTAG console. Check those settings against
your board with `idf.py menuconfig`.
Audio requires core 1, so single-core mode must be disabled.

`MAC` is the downstream hub's Wi-Fi station address: exactly 12 hexadecimal
digits without colons. It defaults to the placeholder `000000000000`. The Hub MAC
setting in the web editor overrides this build-time default for the kit. Configure each
stick's receiver separately to the base's station MAC and use the same Wi-Fi
channel for communicating devices.

Initial configuration downloads pinned u8g2 and Lua sources into the build
directory, plus the managed cJSON and audio codec/effects dependencies. Keep
`main/idf_component.yml` and `dependencies.lock` with the project.

## Audio playback

`bh::AudioPlayer` sends mono signed 16-bit PCM to a Philips I2S DAC or
amplifier. Its FreeRTOS task runs on ESP32-S3 core 1 at priority 5. Mixing
and I2S writes run in that task; pad hits only enqueue playback requests.
The hardware must accept standard I2S with 16-bit slots. No I2C codec control
or amplifier enable pin is configured by this driver.

The wiring is hardcoded in `main/Audio.cpp`: GPIO16 to BCK, GPIO17 to LCK/LRCK,
and GPIO18 to DIN, with MCLK disabled (`GPIO_NUM_NC`).
The Sense expansion board’s microSD slot uses SPI on D8/GPIO7 (SCK),
D9/GPIO8 (MISO), D10/GPIO9 (MOSI), and GPIO21 (CS). With `-DOLED=true`,
wire the OLED to D4/GPIO5 (SDA) and D5/GPIO6 (SCL). D1/GPIO2 is the
second pedal input. See the [XIAO ESP32S3 Sense pinout](https://wiki.seeedstudio.com/xiao_esp32s3_pin_multiplexing/).

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

Playing modes populate pads from the Lua `hit`
array. `Drum`, `Cymbal`, and `Pedal` constants are registered before executing
`config.lua`. Pedal positions are integers 1–4 mapped to GPIO1–GPIO4; positions
must be unique. The limit is 30 total pads, including at most four pedals.

Stick pads require finite numeric `position.x/y/z`, a positive `radius`, and an
`audio` path. `tilt` is optional. Supply `alternate_radius` and `alternate_audio`
together; the alternate radius must be positive and no larger than the pad.
Malformed fields and capacity overflow report configuration errors. Only
freestyle decodes audio; serial/hub modes do not need PCM buffers. Setup lives
in `main/Playing.cpp` and remains shared and non-inlined across playing modes.

In configuration mode, connect to the base access point and open the IP shown
on the base. The editor reads the SD card through `GET /api/kit`. A mounted
card with no configuration opens as an empty kit. Saving changes with
`PUT /api/kit` creates or replaces `config.lua` with the current settings and
ordered pads. Saving rewrites the evaluated kit as Lua data, including
separate globals such as:

```lua
tolerance = 0.05
macAddress = "00:11:22:33:44:55"
hit = {}
```

Tolerance is the maximum distance on either side of a stick pad's plane.
The editor displays centimeters and sends float32-rounded meters in the kit JSON.
The Lua global uses meters. Older files use 0.05 m and the build-time hub MAC for omitted globals.
Use **Upload MP3** on a pad or its second sound to select a file from your
computer. The base stages the upload on the card, checks its MPEG Layer III
frames and runs the playback decoder before publishing it under `/sdcard/sounds/`.
Uploads are limited to 2 MiB compressed and 4 MiB of decoded mono PCM at 24 kHz.
Existing files with the same name are preserved. **Save kit** commits the pad
assignment. Local HTML preview checks the MP3 frames, decodes the audio in the browser,
and stores the file in browser storage. Open the editor served by the drum base
to upload to its SD card.

Place `config.lua` at the root of the card and use paths under `/sdcard` for audio files.
The `hit` table is a consecutive Lua array starting at index 1; loading stops
at the first missing entry. Lua standard libraries are not opened by the
current entry point. Configuration or audio setup failures enter `ConfigError`,
whose UI is also unfinished.

There is currently no host test suite in this repository. Validate pad loading
and audio playback on the board after flashing.

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
