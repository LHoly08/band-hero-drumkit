# Hardware checks to revisit

## Finding 3: downstroke direction

Status: needs hardware verification; keep the detector's current signs.

The user reports that downstrokes consistently triggered hits during testing.
The review's simulated orientation/gyro inputs suggested the opposite direction
under its assumed sensor mounting (+X toward the tip, world Z up, zero roll).
That simulation does not establish that the actual mounted sensor is reversed.

- Record which sensor axis points toward the tip and the physical mounting.
- Calibrate level and forward, then perform separate downstrokes and upstrokes.
- Temporarily capture raw gyro Y/Z, raw roll, calibration roll, and hit events.
- Check which motion arms the detector and whether the hit happens while slowing
  at the bottom of the downstroke or at the top of an upstroke.
- Repeat with both sticks before deciding whether any sign change is needed.

## Finding 4: gyro axis selection after roll calibration

Status: needs hardware verification; no axis-selection change in this fix.

The detector selects gyro Y or Z using `m_rotation.roll - m_offset.roll`.
Calibration changes this relative roll, but does not rotate the sensor-frame
gyro measurements. Calibrating at 90 degrees of physical roll therefore selects
the same axis as calibrating at zero, even though the components of a vertical
stroke may have moved from Y to Z. Calibrating at 180 degrees may reverse signs.

- Keep the tip level and pointed forward throughout each calibration.
- Calibrate at approximately 0, 90, 180, and -90 degrees of sensor roll around
  the stick's length; repeat the same downstrokes after each calibration.
- Record raw roll, saved offset, selected gyro axis, peak speed, and hit count.
- Compare this with calibrating once and then changing roll between strokes.
- If calibration roll changes detection, select axes using a consistent physical
  frame or transform gyro measurements using the orientation. Validate the
  mounting and sign convention before choosing the correction.
