# Drumstick firmware

ESP-IDF firmware for a Seeed Studio XIAO ESP32-C6, using the
`rinku404/bno085` component (pinned to 1.2.0) and ESP-NOW.
Requires CMake 3.31 or newer, ESP-IDF 6.0 or newer, and a compiler
supporting C++26.
The project adds the upstream component's missing I2C/GPIO driver dependencies
for ESP-IDF 6 without editing downloaded component files.

## Wiring

| BNO085 breakout | XIAO ESP32-C6 |
| --- | --- |
| 3.3 V power input | 3V3 |
| GND | GND |
| SDA | D4 / GPIO 22 |
| SCL | D5 / GPIO 23 |
| INT / H_INTN | D2 / GPIO 2 |
| RST / NRST | D3 / GPIO 21 |

Select I2C mode with PS0 and PS1 low if your breakout exposes those pins.
SDA and SCL need external pull-ups to 3.3 V; check whether the breakout already
includes them. SA0 low selects address `0x4A`; high selects `0x4B`.
The driver's README lists incorrect addresses (`0x28` and `0x29`);
this project uses the BNO085 datasheet addresses.

This driver requires both INT and RST. INT indicates that a packet is ready;
RST is used for hardware reset during initialization. Four-wire sensor operation
would require a different driver or an adaptation of this one.

## Build and flash

Activate your ESP-IDF environment first, then run:

```sh
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

The project uses the `esp32c6` target, 4 MB flash, and the USB Serial/JTAG
console. Normal builds use the standard `build` directory; no target argument
or separate build directory is needed.
ESP-IDF downloads the managed BNO085 component during configuration;
commit `main/idf_component.yml` and `dependencies.lock` to preserve its version.
The BNO085 driver is configured for one sensor instance to match this board.

### Debug delivery diagnostics

No ESP-IDF SDK files need editing. Enable this project's diagnostics with:

```sh
idf.py -DDEBUG=ON build
idf.py -p /dev/ttyACM0 flash monitor
```

The ESP-NOW send callback only increments lock-free counters. The main loop
prints `ESP-NOW send results: ... MAC-acked, ... failed` at most once every five
seconds when results are available. Counts cover completions since the previous
summary, across all receivers used during that interval. No hits are held for a
callback or retried. These are MAC-layer results, not confirmation that the base
played a sound. Immediate `esp_now_send()` submission errors keep their existing
warning and are separate from callback results. Info-level logging must be
enabled to see the summaries (the project uses it by default).

`DEBUG` defaults to `OFF` and is remembered by CMake. Disable it explicitly:

```sh
idf.py -DDEBUG=OFF build
```

The callback, counters, and periodic reporting are compiled out when disabled.
This project flag controls diagnostics independently of compiler optimization.
For stepping through code with a debugger, run `idf.py menuconfig`, select
**Compiler options → Optimization Level → Debug (-Og)**, save, then build.
That changes this project's `sdkconfig`, not the SDK source. Keep `DEBUG=ON`
if you also want the delivery summaries.

The receiver MAC and sphere radius are custom CMake arguments:

```sh
idf.py -DMAC=aabbccddeeff -DRADIUS=0.275 build
```

`MAC` is the receiver's Wi-Fi station MAC written as 12 hex digits without colons.
It defaults to `000000000000`, a placeholder; it does not change this board's MAC.
At runtime, send a complete command line over the USB serial console:

- `M aabbccddeeff` sets the receiver MAC (12 hex digits).
- `R 27.5` sets the sphere radius in centimeters for this boot and requests
  recalibration; hold the stick level and forward when sending it.

Use exactly one space after `M` or `R`, then Enter. Command letters and MAC
hex digits may be uppercase or lowercase. Radius accepts a positive decimal
number. Tabs, extra spaces, invalid characters, and lines longer than 31
characters are rejected. LF, CR, and CRLF are accepted as line endings.
The task waits for the full line across partial reads, then accepts another
command; invalid input is discarded through its line ending. In Arduino Serial
Monitor, select Newline, Carriage return, or Both NL & CR when sending.
The drumstick loop registers the new ESP-NOW peer, saves its six-byte address as
the `receiver_mac` blob in the `drumstick` NVS namespace, and switches hit sends
to that address. It logs success or failure. If registration or persistence fails,
the current receiver stays active. The constructor loads the saved address on
every boot (including deep-sleep wake); the build-time `MAC` is the fallback
when no valid blob exists. When the namespace or key is missing, the constructor
saves the build-time MAC to NVS as the initial address. Subsequent boots preserve
the stored address, including one configured over serial.
If NVS initialization reports no free pages or an incompatible stored format,
startup erases the NVS partition and retries initialization. This recovery loses
the saved receiver MAC and other NVS settings; the build-time MAC becomes the
fallback again. Other initialization errors are reported without erasing NVS.
`RADIUS` is the sphere radius in meters and defaults to `0.275` (27.5 cm).
Pass a number without a unit suffix, for example `-DRADIUS=0.27` for 27 cm.
Configuration rejects zero, negative, nonnumeric, and out-of-float-range values.
CMake remembers both values between builds. GPIOs, the sensor address, and Wi-Fi
initialization are in `main/Drumstick.cpp`; the reset button pin is in
`include/Utils.hpp`. The firmware does not explicitly select a Wi-Fi channel.
Configure both sender and receiver to use the same Wi-Fi channel for wireless
transmission.

## Current behavior

`main/main.cpp` initializes NVS and the network interface, constructs
`bh::Drumstick`, and runs its loop. The constructor initializes the event loop,
the sensor, Wi-Fi station mode, and ESP-NOW.
Each hit is sent over ESP-NOW as XYZ plus peak speed and also printed as two
consecutive USB-console lines compatible with `SticksTestAudio`.

The BNO085 is configured at 100 kHz I2C with a 20 ms clock-stretch allowance.
The driver requests rotation-vector and calibrated-gyroscope reports at 100 Hz.
The existing gyro detector arms on a downstroke and fires as it slows or reverses.
Its thresholds and roll-dependent axis selection remain in `main/Drumstick.cpp`.
`HIT_SPEED_SCALE` preserves its existing tuning and is independent of the sphere
radius. The active-low state/calibration button uses both GPIO edges and a 10 ms
stable-level debounce window on both press and release. A press shorter than
two seconds requests calibration. Releasing after a press
of at least two seconds returns from the drumstick loop. `app_main` destroys the
drumstick to shut down the sensor, I2C, and Wi-Fi, then enters deep sleep with
GPIO 0 low as the only enabled wake source. Both LEDs are held off and the BNO085
reset pin is held low during deep sleep. A new button press reboots the ESP into
`app_main`; the wake press is consumed before normal button handling starts.
Power-on and button wake both start the drumstick directly, without state classes.
Timing constants and the button pin are in `include/ButtonPress.hpp` and
`include/Utils.hpp`, respectively (currently GPIO 0).

External LEDs are active-high: `GREEN_LED_PIN` (GPIO 20) stays on until a valid
calibration completes, and turns on again when recalibrating.
`RED_LED_PIN` (GPIO 19) is reserved for low battery and currently stays off:
the XIAO ESP32-C6 needs an external battery voltage divider, and none is installed.
See [Seeed's battery measurement instructions](https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/#reading-battery-voltage).
The state button already uses A0/GPIO 0, so a future battery divider must use a
different free ADC pin. `SENSOR_RESET_PIN` is GPIO 21.

## Shared chest-origin position experiment

`include/SpherePosition.hpp` contains `SphereConfig`: the default radius is
**0.275 m (27.5 cm)**, the chest center is `{0, 0, 0}`, and `tip_axis` is sensor
**+X**. Build with `-DRADIUS=0.27` for 27 cm. Change `tip_axis` if a different
sensor-local axis points along the stick toward its tip, for example `{0, -1, 0}`
for -Y. Verify the mounting before judging the position estimate.

Both sticks use the same model, with no left/right assignment. The playing
coordinate system is **X right, Y forward, Z up**, in meters. The estimate is
`chest center + radius * calibrated stick direction`; it does not integrate
acceleration or measure hand translation. Parallel sticks still have the same
estimated position when their references agree.

To try it:

1. Build and flash the stick, then open its serial monitor.
2. Hold the stick level, pointing toward your intended forward direction.
   Press and release the existing state/calibration button in under two seconds.
   Calibration occurs on release for a button connected to ground.
   Hold the pose until `XYZ calibrated` appears.
3. Calibrate the second stick pointing in the same direction. Reset sets heading;
   gravity still defines up. It does not measure the chest location or set the
   tip to the sphere center. The forward tip should read about `(0, 0.275, 0)`.
4. Make repeated strokes at each intended drum. Each hit prints both lines:

   ```text
   Hit! Roll: 0.000  Pitch: 0.000  Yaw: 0.000 (rad), Speed: 6.000
   HitXYZ! X: 0.000  Y: 0.275  Z: 0.000 (m)
   ```

   These numbers illustrate a level forward pose; actual strike poses differ.
   Leftward poses should have negative X, rightward positive X, and downward
   poses negative Z. Twisting around the stick's length should not change XYZ.
5. Record 20-30 hits per intended drum and compare their XYZ spread before
   choosing drum centers and tolerances for the base. Test both hands.

Hits are suppressed until calibration. The main loop does not probe or
automatically recover a disconnected sensor; reboot after reconnecting it.
Near-vertical reset poses cannot define a heading; keep reset
pending by pointing level until calibration succeeds. Invalid orientation
reports, orientation callbacks older than 50 ms, or gyro/orientation report
timestamps more than 50 ms apart disarm the detector. Hit positions use the
latest valid orientation serviced by the firmware, not interpolated strike-time
poses. The hardware tests will determine whether the model and timing work well.

Build with `idf.py -DDEBUG=ON build` to enable the `Hit!`/`HitXYZ!` records
required by `SticksTestAudio`. Default (`DEBUG=OFF`) builds send hits only over
ESP-NOW. Capture the `Hit!` and `HitXYZ!` lines together with the serial monitor when
comparing strike positions and speeds. `SticksTestAudio` pairs the speed in
`Hit!` with the immediately following XYZ record. ESP-NOW carries both values
together in one packet; all base playing modes receive that same format.

## Capture a crash

After flashing, save the serial output to a file instead of trying to read it
while it scrolls:

```sh
idf.py -p /dev/ttyACM0 monitor 2>&1 | tee crash.log
```

Press Ctrl+] to exit the monitor after the crash. The complete output stays in
`crash.log`. The panic handler waits five seconds before rebooting so the dump
remains visible longer. This delay helps capture the error; it does not fix its cause.

`Drumstick::detect_hit()` sends the shared `bh::Packet` defined in
`../shared/HitPacket.hpp`: four little-endian IEEE-754 floats, X/Y/Z in meters
followed by the peak scaled gyro speed. Euler angles remain diagnostic fields
in the serial `Hit!` line only. The receiver validates the same 16-byte format
for every playing mode. Flash both base and stick when changing from the old
angle format: its equal byte length cannot identify old firmware automatically.

Hardware checks for the detector's direction and calibration roll are recorded
in [TESTING.md](TESTING.md). The current detector signs and axis selection are
preserved pending those measurements.

## Host checks

```sh
cmake -S tests -B /tmp/sticks-tests
cmake --build /tmp/sticks-tests
ctest --test-dir /tmp/sticks-tests --output-on-failure
```

## C++ namespace convention

Follow the [drumkit namespace convention](../README.md#c-namespace-convention):
qualify symbols from other files and keep same-file names unqualified where
C++ permits. Shared types in `Utils.hpp` and `SpherePosition.hpp` use `bh::`
when referenced from `Drumstick.hpp` or `Drumstick.cpp`.

## References

- [Seeed XIAO ESP32-C6 pinout](https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/)
- [BNO085 driver](https://components.espressif.com/components/rinku404/bno085/versions/1.2.0/readme)
- [BNO08x datasheet](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf)
- [ESP-NOW for ESP32-C6](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-reference/network/esp_now.html)
