# Read Pico

Target: `mindreset_read_pico`; application: `mr_read_pico`.
ESP-IDF: v6.1. Flash: 16 MB; PSRAM: 8 MB. Panel: E0470A01, 684 x 1216, 16 gray levels.

```text
eim run "idf.py -C firmware/targets/mindreset_read_pico build" v6.1
eim run "idf.py -C firmware/targets/mindreset_read_pico -p <PORT> flash" v6.1
```

The device advertises as `QF-MR-PICO-` followed by the final two Bluetooth MAC
bytes, and its authenticated status identifies the model as `mindreset_read_pico`.
Usage publication, time synchronization and OTA reuse the shared BLE protocol.
Release assets use the stem `mindreset-read-pico`; write the `-full-` image at
offset 0 for a first installation.

Board support comes from the vendor firmware; see
[components/UPSTREAM.md](components/UPSTREAM.md). The 120 MHz flash and PSRAM
timing in `sdkconfig.defaults` is required by the panel render path and depends
on the flash fitted to this board.

## Display

The views are Home with both services, the
24-hour trend and alarm states; the idle clock; a landscape Home; pairing; OTA
progress and confirmation; Display settings; and the offline state. The view
rotates in quarter turns. Automatic rotation applies after a 1.5-second stable
accelerometer reading; flat or diagonal readings keep the current view.

Each service leads with its short window and shows the week window beside it; the
week window leads when no short window is reported. The alarm level follows the
fuller of the two windows: from 80% the bar is hatched, and from 95% the service
area takes a gray wash with a NEAR LIMIT or EXHAUSTED label.

Eight of the sixteen gray levels are used. The panel renders light grays close to
paper, so bar tracks, rules and washes use darker levels than a screen design
suggests; the test card under [USB diagnostics](#usb-diagnostics) compares
candidates. Refresh modes:

| Mode | Use |
| --- | --- |
| GC16, full panel | First frame, screen or rotation change, link online/offline change, alarm level change, a shrinking usage bar, KEY2, and after 13 consecutive DU or GL16 updates |
| DU, changed pixels | Any update whose changed pixels all turn to ink or paper: values, countdowns, sample ages, the clock, growing bars, Display settings and OTA progress |
| GL16, full panel | Updates that change a gray area, such as text on an alarm wash or a new stored trend sample |

The trend retains its gray 80–100% band and grid. Its curves and horizontal axis
stay fixed between stored samples, so normal usage and clock updates do not move
the gray chart. Periodic GC16 refreshes still clear ghosting.

Refreshes run on the application task and block it for the duration of the
waveform. An unchanged frame is not refreshed. The high-voltage rails power down
8 seconds after the last refresh. Usage is redrawn at the configured interval
(15/30/60/120 seconds, default 30); input, link, alarm and minute changes redraw
immediately.

## Controls

The three capacitive keys below the display share the touch sensor.

- KEY1 / KEY3: switch between Home and Clock.
- KEY2: full GC16 refresh. Hold 800 ms: open or close Display settings.
- Display settings: tap a value to select and save it to NVS; tap DONE to exit.
- Clock: a tap returns to Home. A clock opened by the idle timeout also returns
  when the device is picked up.
- OTA confirmation: tap CONFIRM or DENY, or press KEY2 to confirm and hold it to
  deny. Other navigation is blocked during OTA and pairing.

A local redraw does not trigger a provider fetch on the Bridge. Power-key events
are acknowledged and ignored; power-off uses the PMU's forced long press.

## Clock, power and trend

The PMU RTC stores UTC seconds. A Bridge time sync sets it and stores the UTC
offset in NVS; the time shows `--:--` until both are available. Battery level
and charge state come from the PMU and feed the OTA power gate. Invalid PMU status
readings do not authorize an update.

Trend history uses a mounted FAT microSD card; absence is nonfatal, and a card
inserted while the device is running is mounted when it is detected. Samples are
stored every 30 minutes as up to 48 timestamped records in `trend.bin`, with
`trend.bak` as the replacement backup. Gaps longer than one hour are not connected.
The upper service blocks use the latest Bridge publication; the trend uses only
stored short-window samples and updates when a new half-hour record is saved.
Its `AS OF HH:MM` label gives the snapshot time in the device's local time zone,
and each legend value shows its age. The axis ends at that snapshot, not at NOW.
Without a valid local clock, the snapshot label shows its age instead.
Offline history keeps its original timestamps. Records older than 24 hours are
excluded using the current clock, with Bridge time as a fallback. Missing short
windows are not replaced with week values. Without an SD card or usable history,
the chart shows NO TREND DATA; current usage remains available above it.

## Orientation calibration

`kUprightXSign` and `kLandscapeYSign` in `main/board.cpp` map the accelerometer's
device frame to the four views. The `i` serial command logs the current sample
in mg together with the resolved orientation.

## USB diagnostics

Commands are accepted while OTA is idle. `1`-`9` show synthetic views: Home, alarms, clock, landscape Home, pairing, OTA progress,
settings, offline and OTA confirmation. `t` shows the gray-level test card: four
candidates each for small text, bar tracks, rules, large digits and alarm washes,
refreshed with GC16. `r` returns to live data and `i` prints board state. The previews do not alter the live usage model or start an OTA.

`tools/read_pico/preview/run.sh` renders the same nine views in both rotations
and the test card on the host with the firmware drawing code and checks the touch targets.

## Fonts

Glyph masks are generated by `tools/ws397/generate_fonts.py --target read_pico`
from the Google Fonts variable files described in the
[ePaper 3.97 notes](../waveshare_epaper_397/README.md#fonts-and-source): Archivo
weight 700 and JetBrains Mono weights 500 and 700, thresholded to one bit. Sizes above
56 px carry digits and `%:-. ` only. Font licenses are in `main/fonts/`.
