# ESP32-S3-ePaper-3.97

Target: `waveshare_epaper_397`; application: `ws_epaper_397`.
ESP-IDF: v6.1. Flash: 16 MB; PSRAM: 8 MB.

```text
eim run "idf.py -C firmware/targets/waveshare_epaper_397 build" v6.1
eim run "idf.py -C firmware/targets/waveshare_epaper_397 -p <PORT> flash" v6.1
```

The device advertises as `QF-WS-S3-E397-` followed by the final two Bluetooth MAC
bytes. The prefix is unique in the Target Registry. The authenticated status
identifies this model as `waveshare_epaper_397`. Usage publication and RTC synchronization reuse the
existing authenticated BLE protocol. OTA images use project name `ws_epaper_397`
and retain the shared on-device confirmation and power gate.
A BLE OTA from 0.9.0 to 0.9.1 through `tools/run_ota_validation.py` passed on
2026-09-22 (153 s, physical confirmation, reboot and version check). The OTA page
used monochrome refreshes: a full refresh on entry and after 20 partial
updates, with partial refreshes for progress updates; power-loss recovery and rollback
remain unverified.

## Display and controls

The panel renders four gray levels at 800 x 480, or 480 x 800 in portrait.
Automatic orientation supports all four quarter turns after a 1.5-second
stable accelerometer reading; flat or diagonal readings retain the current view.
At boot in automatic mode, the first frame uses the measured orientation when
the accelerometer gives a decisive reading, and landscape otherwise.

Refresh modes by screen:

| Screen | Mode |
| --- | --- |
| Home, Settings, OTA | Black and white. Light gray is a fixed native-coordinate checker pattern. Monochrome full-refresh baseline on entry, then byte-aligned partial refreshes of the changed bounding rectangle. |
| Clock | Four-gray full refresh as the baseline, followed by monochrome RAM preparation. Time, temperature, humidity, battery and usage bars are refreshed through five fixed black-and-white partial windows. Any change outside these windows requests a new grayscale baseline. |
| Trend, pairing | Four-gray full refresh, then controller deep sleep. |

Partial regions are computed against the last frame processed by the refresh
worker, in native 800 x 480 coordinates. The driver maps the Y window to
descending controller addresses and, after BUSY completes, writes the displayed
region to both previous and current RAM. SSD1677 deep sleep does not retain RAM,
so the controller stays out of deep sleep while a partial-refresh baseline is
active. A full refresh renews the baseline after 20 partial updates, an
orientation change or a screen change. A pending full-refresh request is kept
when a later frame replaces the queued image before the worker takes it.
Automatic redraws are coalesced to the latest frame; unchanged frames are skipped. The default
update interval is 30 seconds, with 15/30/60/120-second settings. Page changes,
security prompts and physical refresh requests bypass this interval.

- Dial up: trend from landscape Home; down: clock.
- Dial press: return from clock/trend, or redraw Home using the latest received data.
- Dial hold 800 ms: open Display settings; hold again to exit.
- Settings: up/down changes focus; press cycles the selected value and saves it to NVS.
- BOOT: request a redraw outside OTA.
- OTA confirmation: press to confirm; hold to deny. Other navigation is blocked during OTA.
  After a successful transfer, the device restarts once the RESTARTING frame
  has finished refreshing, or after 20 seconds.
- PWR: PMU-managed 1-second power-on and 4-second power-off, following the vendor example.

A local redraw does not trigger a new provider fetch on the Bridge. The shared
protocol currently publishes usage from the host and has no device-to-host
fetch request. RTC time remains `--:--` until the stored calendar is valid.
The trend requires a mounted FAT SD card; absence is nonfatal. Samples are
stored every 30 minutes with up to 48 timestamped records and a replacement
backup. At boot, `trend.bak` is loaded when `trend.bin` is missing or contains
an invalid record. Missing intervals are not interpolated. An empty trend page remains
visible until navigation, a portrait rotation or the configured idle timeout.

TG28 uses the AXP2101 driver, as confirmed by the hardware owner. QMI8658 is
probed at 0x6A/0x6B; the tested board responds at 0x6B. I2C is SDA 41/SCL 42.
Screen GPIO: SCLK 11, MOSI 12, CS 10, DC 9, RESET 46, BUSY 3.
Dial GPIO: up 4, press 5, down 6; BOOT 0, all active low.

## USB diagnostics

Commands are accepted only while OTA is idle. `r` returns to live data.
`i` includes the stored rotation mode (0=AUTO, 1=LAND, 2=PORT).
`d` or `1` shows a synthetic Home; `2`-`9` show trend, critical warning, clock,
portrait Home, settings, offline, pairing and OTA design states respectively.
Repeated `6` commands cycle the settings focus for partial-refresh diagnostics.
These previews do not alter the live usage model or initiate an OTA transfer.
`p` shows a four-gray test pattern, `g` shows a four-gray background with an isolated
black-and-white partial window, `i` prints board state and acceleration, `s` exports the
packed 96000-byte frame. `tools/ws397/capture.py` keeps DTR/RTS deasserted to
avoid restarting the USB-JTAG device while collecting diagnostics.

## Partial-refresh isolation demos

Use the [diagnostic procedure](../../../tools/ws397/PARTIAL_DEMOS.md) to isolate native window addressing (`t`), frame differencing (`u`), rotation (`v`) and settings rendering (`w`). It covers serial and button controls, expected images and interpretation. The demos share the live display driver; exported PNGs show the prepared framebuffer, not the physical screen.

## Fonts and source

The glyph masks are generated by `tools/ws397/generate_fonts.py` using Pillow.
Inputs: Google Fonts `ofl/archivo/Archivo[wdth,wght].ttf` and
`ofl/jetbrainsmono/JetBrainsMono[wght].ttf`, placed in the input directory as
`Archivo.ttf` and `JetBrainsMono.ttf`. Archivo uses weight 700; JetBrains
Mono uses weight 500. Large numeric fonts are subsetted; text uses printable
ASCII. Masks are thresholded rather than dithered, preserving the four palette
values. Font licenses are in `main/fonts/`.

The screen driver provenance and local changes are recorded in
`main/vendor/UPSTREAM.md`.
Refresh measurements and optical acceptance are summarized in [ePaper validation](../../../docs/validation/epaper-refresh.md). See [verification](../../../docs/verification.md) for current functional and OTA acceptance.
