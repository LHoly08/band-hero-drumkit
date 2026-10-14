# Drumstick firmware

ESP-IDF firmware for a Seeed Studio XIAO ESP32-C6, using the
`rinku404/bno085` component (pinned to 1.2.0) and ESP-NOW.
Requires ESP-IDF 5.5 or newer and a compiler supporting C++26.
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
It defaults to `ffffffffffff` (broadcast); it does not change this board's MAC.
The receiver must use channel 1. CMake remembers the MAC between builds.
GPIOs and the sensor address are constants in `main/Drumstick.cpp`;
the ESP-NOW channel is a constant in `main/main.cpp`.

## Current behavior

Startup initializes NVS, Wi-Fi station mode, and ESP-NOW, and registers the
configured receiver as an unencrypted peer. The local and receiver MAC addresses
are printed to the USB console. The XIAO RF switch is configured for its onboard
antenna (GPIO 3 low, GPIO 14 low). No access-point connection is needed.

The BNO085 is configured at 100 kHz I2C with a 20 ms clock-stretch allowance.
The driver requests rotation-vector reports at 100 Hz; `Drumstick::loop()`
services the sensor, and its callback updates roll, pitch, and yaw in radians.
At most 10 times per second, the callback prints the angles as decimal floats to the
USB serial console, for example `Roll: 0.123  Pitch: -0.456  Yaw: 1.234 (rad)`.

## Capture a crash

After flashing, save the serial output to a file instead of trying to read it
while it scrolls:

```sh
idf.py -p /dev/ttyACM0 monitor 2>&1 | tee crash.log
```

Press Ctrl+] to exit the monitor after the crash. The complete output stays in
`crash.log`. The panic handler waits five seconds before rebooting so the dump
remains visible longer. This delay helps capture the error; it does not fix its cause.

ESP-NOW is ready for use, but the firmware does not yet transmit sensor packets.
Define a packet format shared with the base firmware before adding sends.

## References

- [Seeed XIAO ESP32-C6 pinout](https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/)
- [BNO085 driver](https://components.espressif.com/components/rinku404/bno085/versions/1.2.0/readme)
- [BNO08x datasheet](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf)
- [ESP-NOW for ESP32-C6](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-reference/network/esp_now.html)
