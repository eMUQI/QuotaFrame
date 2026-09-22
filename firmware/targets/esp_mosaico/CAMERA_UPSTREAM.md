# Camera and gesture dependencies

The target uses registry components pinned by `dependencies.lock`:

| Component | Version | Role |
| --- | --- | --- |
| espressif/esp_video | 2.5.0 | DVP/V4L2 capture and buffer lifetime |
| espressif/esp_cam_sensor | 2.6.0 | OV3640 sensor configuration |
| espressif/esp-dl | 3.3.11 | S31 inference and preprocessing |
| espressif/hand_detect | 0.2.0 | ESPDet-Pico 224×224 hand detection |
| espressif/hand_gesture_recognition | 0.2.0 | MobileNetV2 128×128 classification; `ok` command |

The published model components explicitly select their `models/p4` resources
for `esp32s31`. Both models are embedded in the application so A/B update and
rollback keep model weights and code together. Only RGB888 conversions are
enabled in ESP-DL; camera UYVY conversion and geometric mapping live in
`main/gesture_image.cpp`. No managed component is modified.

Left-slot wiring and USB Serial/JTAG pad arbitration in
`main/gesture_camera.cpp` are adapted from `components/subboard_support/subboard.c`
in the esp-mosaico-claw source snapshot
`903fe11ec3ca4a8881afef104e53a8da040896ac` (Apache-2.0, Espressif Systems).
The pin assignments were read from the committed snapshot, not its working-tree
edits. This integration does not import the module manager, board manager, Lua,
or the esp-claw video wrapper. Preserve the copyright notice in the adapted file.

The camera uses its onboard oscillator and the existing BSP subboard I2C bus.
GPIO14 is D4 during capture; GPIO33 is D2 and cannot concurrently serve USB
Serial/JTAG. GPIO34 is the active-low illuminator and remains high. UART logging
is retained. Start/stop and frame ownership belong to the vision worker; the
application task exchanges context and bounded events only.

Camera mounting rotation and mirror defaults remain subject to physical
calibration. `CONFIG_MOSAICO_CAMERA_ROTATION` applies the mounting correction;
the inverse of the actual applied display rotation follows it. A context change
discards any unfinished gesture. The default correction is 180 degrees, with
horizontal mirroring enabled.

Package copyright and license sources are recorded in
[THIRD_PARTY_LICENSES.md](../../../THIRD_PARTY_LICENSES.md).

## Embedded model digests

SHA-256 of the selected published S31/P4 model files:

- `espdet_pico_224_224_hand.espdl` (576944 bytes): `a8d1857bab33bfd23bc72e1543a156ef33645b47b9ac130776aacb6a215439ea`
- `mobilenetv2_0_5_128_128_gesture.espdl` (935136 bytes): `fc4c4e0b4ba52e651dd802d5e0dc738f9eac899b107a9ce4638635e746be3c5d`
