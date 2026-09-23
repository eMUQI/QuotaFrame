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

When re-syncing, preserve these integration changes as well as the manifest
constraint and review the repository diff for other board-specific changes.
