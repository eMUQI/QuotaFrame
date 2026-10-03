# Upstream source

This component is a vendored source snapshot from:

- Repository: `https://github.com/esp-mosaico/esp-mosaico-bsp.git`
- Path: `components/esp-mosaico-bsp`
- Commit: `392860b1d1a123c3377947074b2af1f600e86c5d`
- License: Apache-2.0 (see `LICENSE` and the SPDX headers in the source files)

## Local modification

`idf_component.yml` declares `idf: ">=6.1"` instead of the upstream `">=6.2"`.

ESP-IDF v6.1 already exposes `esp32s31` as a preview target, and this component together with its whole dependency chain compiles and links against v6.1. The upstream constraint is enforced by the Component Manager version solver, which offers no way to relax it from the consuming project, so the snapshot is kept locally while the rest of the repository stays on the locked v6.1 toolchain.

The display integration also implements partial rendering with one TE wait per
LVGL refresh. `TE_SYNC` in the BSP configuration selects this integration;
the adapter uses `NONE` internally so it does not force full-screen rendering.
The single PSRAM draw buffer retains full-screen capacity, while LVGL submits
only invalidated areas. The adapter retains ownership of DMA completion and
flush-ready notifications. TE uses the rising edge of the CO5300 mode-1 output
(vertical blanking; [datasheet section 5.3](https://dl.espressif.com/AE/esp-iot-solution/CO5300_Datasheet_V0.00.pdf)).
Missing TE signals allow an unsynchronized transfer after a bounded 25 ms wait.
Large updates can span panel scans; TE alignment alone is not a tear-free guarantee.

Invalidated areas expand before rendering to four-pixel horizontal boundaries
and two-pixel vertical boundaries, clipped to the display dimensions. The CO5300
RASET command requires an even start row and an even row count (datasheet page
161); LVGL renders the expanded rows so the pixel buffer matches the transfer window.

The CO5300 initialization sequence sets brightness register 0x51 to zero instead of
0xFF and omits the Display On command with its 600 ms delay. `bsp_display_new()`
issues Display On after the orientation is set. The panel therefore stays dark until
the application raises the brightness, which it must do after the first frame has been
written; upstream shows undefined frame memory at full brightness during that delay.

`bsp_battery_init()` does not seal the BQ27220 again after `bq27220_create()`. The
driver seals and verifies on every path that returns a handle, and each seal waits
`CONFIG_BQ27220_SEAL_SETTLE_MS` unconditionally, so the upstream call only added
that wait. The driver default is 2000 ms; this target selects 200 ms in
`sdkconfig.defaults` and retains the sealed-state check after the wait.

When re-syncing, preserve these integration changes as well as the manifest
constraint and review the repository diff for other board-specific changes.
