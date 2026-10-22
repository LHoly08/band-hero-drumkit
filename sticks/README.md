# Drumstick firmware

ESP-IDF firmware for a Seeed Studio XIAO ESP32-C6, using the
`rinku404/bno085` component (pinned to 1.2.0) and ESP-NOW.
Requires CMake 3.31 or newer, ESP-IDF 5.5 or newer, and a compiler
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

The receiver MAC is the only custom CMake argument:

```sh
idf.py -DMAC=aabbccddeeff build
```

`MAC` is the receiver's Wi-Fi station MAC written as 12 hex digits without colons.
It defaults to `000000000000`, a placeholder; it does not change this board's MAC.
CMake remembers the MAC between builds. GPIOs, the sensor address, and Wi-Fi
initialization are in `main/Drumstick.cpp`; the reset button pin is in
`include/Utils.hpp`. The firmware does not explicitly select a Wi-Fi channel.
Before enabling wireless transmission, configure both sender and receiver to
use the same channel.

## Current behavior

`main/main.cpp` constructs `bh::Drumstick` and runs its loop. The constructor
initializes NVS, the event loop, the sensor, Wi-Fi station mode, and ESP-NOW.
Hit transmission is currently disabled; hits are printed to the USB console.

The BNO085 is configured at 100 kHz I2C with a 20 ms clock-stretch allowance.
The driver requests rotation-vector and calibrated-gyroscope reports at 100 Hz.
The existing gyro detector arms on a downstroke and fires as it slows or reverses.
Its thresholds and roll-dependent axis selection remain in `main/Drumstick.cpp`.
`HIT_SPEED_SCALE` preserves its existing tuning and is independent of the sphere
radius. The reset button uses a 200 ms debounce window, controlled by
`BUTTON_DEBOUNCE_MS` in `include/Utils.hpp`.

## Shared chest-origin position experiment

`include/SpherePosition.hpp` contains `SphereConfig`: the default radius is
**0.275 m (27.5 cm)**, the chest center is `{0, 0, 0}`, and `tip_axis` is sensor
**+X**. Change `radius` to `0.27f` for 27 cm. Change `tip_axis` if a different
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
   Press and release the existing reset button. With the current pull-up and
   rising-edge wiring, reset occurs on release for a button connected to ground.
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

Hits are suppressed until calibration, and again after sensor recovery until
you recalibrate. Near-vertical reset poses cannot define a heading; keep reset
pending by pointing level until calibration succeeds. Invalid orientation
reports or reports older than 50 ms disarm the detector. Hit positions use the
latest valid orientation serviced by the firmware, not interpolated strike-time
poses. The hardware tests will determine whether the model and timing work well.

Capture the `HitXYZ!` lines with the serial monitor when comparing strike
positions. A base config loader and wireless XYZ packets are
not part of this serial-only experiment.

## Capture a crash

After flashing, save the serial output to a file instead of trying to read it
while it scrolls:

```sh
idf.py -p /dev/ttyACM0 monitor 2>&1 | tee crash.log
```

Press Ctrl+] to exit the monitor after the crash. The complete output stays in
`crash.log`. The panic handler waits five seconds before rebooting so the dump
remains visible longer. This delay helps capture the error; it does not fix its cause.

`Drumstick::detect_hit()` contains a commented-out ESP-NOW send. Its existing
payload is a `Coordinates` snapshot (pitch, yaw, roll in radians) followed by a
float for the detector's peak scaled gyro speed; it does not include XYZ.
The receiver must use the same layout. Register `ReceiverMAC`
as a peer and configure the radio channel and antenna before enabling the send.

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
