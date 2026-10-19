# Serial hit matcher

Standalone desktop C++20 program using the existing Band Hero `serialib` sources.
It prints `drum`, `cymbal`, `kick`, or `unmatched` for each valid hit. It does not
play audio or change the ESP firmware.

From the `sticks` directory:

```sh
cmake -S test -B test/build
cmake --build test/build
./test/build/stick_hit_test /dev/ttyACM0
```

Close `idf.py monitor` before opening the same port. The default baud rate is
115200; optionally pass a baud rate after the port name. Stop with Ctrl+C.
If Band Hero is elsewhere, configure with
`-DBAND_HERO_DIR=/absolute/path/to/band-hero`.

Edit `drum`, `cymbal`, and `kick` in `main.cpp` to set your three target
positions. Field order is **X, Y, Z**, in meters: X right, Y forward, Z up.
The sample targets lie on the 27.5 cm sphere: drum forward, cymbal left, kick
right. These are placeholder names/positions, not measured drum locations or a
physical kick pedal. Replace them with positions from your own `HitXYZ!` logs.
`tolerance` is an inclusive **3D distance** from a target, currently `0.05f`
(5 cm). If targets overlap, the nearest target wins; ties use target order.

The parser accepts the firmware's `HitXYZ!` format, with optional `(m)` suffix
and log prefixes. X, Y, and Z can appear in any order, once each:

```text
HitXYZ! X: -0.275 Y: 0.000 Z: 0.000
HitXYZ! X: -0.275  Y: 0.000  Z: 0.000 (m)
```

Both print `cymbal`. Old `Hit!` angle lines, non-hit logs, and malformed hits
are ignored, so each firmware hit produces only one result. Serial lines
are preserved across read timeouts; lines longer than 4096 bytes are discarded.
For a hardware-free check, run `./test/build/stick_hit_test --stdin` and paste
hit lines, then finish with Ctrl+D.

## Sphere model checks

After the CMake build above, run `ctest --test-dir test/build --output-on-failure`.
The `sphere_position_test` checks the actual firmware math without hardware:
axis signs, mounting-axis changes, heading resets, preserved elevation, roll
invariance, invalid inputs, and fixed-radius geometry. These checks do not
validate real-world tracking accuracy or physical sensor mounting.
