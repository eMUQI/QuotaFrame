# ESP32-S3-RLCD-4.2

Target: `waveshare_rlcd_42`; application: `ws_rlcd_42`.
ESP-IDF: v6.1. Flash: 16 MB; PSRAM: 8 MB (octal).

```text
eim run "idf.py -C firmware/targets/waveshare_rlcd_42 build" v6.1
eim run "idf.py -C firmware/targets/waveshare_rlcd_42 -p <PORT> flash" v6.1
```

Evidence level: **partial hardware**. The firmware and `test_apps/logic` compile with ESP-IDF
v6.1, and the logic tests pass on a host build. On one board, the landscape Home and detail
pages rendered live usage from a bonded Bridge, which reconnected with an encrypted link after
flashing. Portrait and flipped layouts, button debounce, sensors, audio, TF card, first-time
pairing and OTA have not been exercised on hardware.

The device advertises as `QF-WS-S3-R42-` followed by the final two Bluetooth MAC bytes and
reports `waveshare_rlcd_42` in the authenticated status. Usage publication, RTC
synchronization, screen toggle/page commands and OTA reuse the shared BLE components.

## Display

The 4.2-inch reflective LCD (ST7305) shows 400 x 300 black-and-white pixels without a backlight.
It has no refresh waveform and no ghosting, so every change is a full-frame SPI write of
15,000 bytes (about 12 ms at 10 MHz). Frames are composed every 250 ms and sent only when they
differ from the frame on the panel, so countdowns and the clock tick every second. The panel
scans at about 16 Hz along the landscape x axis; each write waits for the TE rising edge (GPIO6)
so it stays ahead of the scan instead of leaving a vertical seam.

At 120 PPI one pixel is about 0.21 mm and stays visible, so the screens follow the look of a
printed segment LCD instead of smoothing vector type:

- Figures (usage, clock, room readings, pairing code, OTA progress) are seven-segment digits.
  As on a printed segment LCD, every digit has a fixed cell and a `1` lights the right half of
  its cell. Percentages use a fixed field of a hundreds half digit and two right-aligned cells,
  with the percent sign a third of a digit after it.
- All other text uses a built-in 5 x 7 pixel font at 2x (labels, 14 px cap height, 2 px strokes)
  or 3x (countdowns on the detail and alert pages). Integer scaling keeps every stroke on the
  pixel grid.
- Bars are a 2 px outline holding up to twenty solid 5 % cells.
- No dither patterns are used: on this panel ordered dither shows as coarse dark streaks, not
  grey, and makes unlit segments or empty bar cells compete with the data.
- Primary rules are solid, secondary separators dotted; a 2 px bezel frame sits 4 px inside the
  panel edge. Chips (link status, focused setting, pace) are inverted with rounded corners.

The display uses the same SHORT / WEEK labels and used-percentage meaning as the other targets:

| Page | Content |
| --- | --- |
| Home | Both services stacked; SHORT and WEEK usage bars with reset countdowns and elapsed-time markers |
| Codex, Claude | One service: the SHORT window in 80 px figures with its RESET countdown, then SHORT and WEEK sections each with a usage bar, elapsed-time marker and elapsed time. OVER +N% marks usage at least 5 percentage points above the elapsed fraction of the window |
| Trend | Last 24 hours from the TF card, 48 half-hour bars per service |
| Clock | Time with seconds, date, SHTC3 temperature and humidity, compact usage bars |
| Settings | Auto cycle, alert mode, seconds, rotation |
| Alert | Full-screen inverted page once a short window reaches 95 %; NEAR LIMIT below 100 %, EXHAUSTED at 100 % |

Text is at least 2x (14 px, 2.9 mm cap height) and strokes at least 2 px in both orientations.
Layout follows one grid: content starts 14 px from the panel edge, the first line sits 10 px
below the header rule, rows and footers are separated by 10 px, and inverted chips grow around
their text so it stays on the same left edge as plain labels. Home uses 64 px figures level with
the top of the SHORT bar and the bottom of the WEEK bar; detail uses 80 px figures. On Home the countdown sits to the right of its window
label without a RESET prefix; the detail page labels it RESET. The header shows a chip only for
OFFLINE, PAIRING and UPDATING; LINKED is the normal state and is not shown.

Offline pages retain the last values at full contrast with their sample age and the OFFLINE
header chip, but show RESET -- rather than a live countdown. Missing windows also show --;
RESET WAIT is reserved for a known reset time that has passed. On Home, a service with neither
window collapses to NO DATA (offline) or WAITING FOR DATA (linked). Pace and time markers
assume fixed windows of five hours and seven days because usage.v1 carries only reset times. They are omitted offline, for missing windows, or when
the remaining time does not fit the assumed window.

Rotation is manual (the board has no accelerometer): LAND, PORT (landscape turned 90 degrees
clockwise) or FLIP. Portrait provides Home, Clock and Settings; the other pages exist in
landscape only.

## Controls

- BOOT (GPIO0): next page, HOME → CODEX → CLAUDE → TREND → CLOCK (portrait: HOME ↔ CLOCK).
- KEY (GPIO18): previous page.
- Hold KEY 800 ms: open Settings; hold again to leave.
- PWR switches the board's power circuit and is not readable by the firmware.
- Settings: KEY moves the focus, BOOT changes the value and saves it to NVS.
- Alert: KEY dismisses; it returns after usage drops below 93 % and reaches 95 % again.
- Pairing: KEY or BOOT requests a new code.
- OTA confirmation: KEY confirms, hold KEY denies. Navigation is blocked during OTA.

Auto cycle advances the ring every 30 s, 1 min or 5 min; any key press restarts the interval.
The Bridge screen-toggle command switches to and from Clock; page commands step the ring.

## Alerts

`ALERT 95%` selects OFF, PAGE (full-screen page) or +BEEP (page plus three 2 kHz tones
through the ES8311 codec and speaker amplifier). The tone plays once when an alert is raised.
Reconnecting restores an undismissed alert without repeating its tone. A provider's alert
re-arms after its warning level falls below 2 (usage below 93% with hysteresis).
The codec is configured for DAC-only slave operation at 16 kHz; its register values are derived
from Espressif `esp_codec_dev` 1.3.5 (Apache-2.0); fixed source links, notices and local adaptations
are recorded in [UPSTREAM.md](UPSTREAM.md).

## Peripherals

| Function | Connection |
| --- | --- |
| ST7305 (SPI3) | MOSI 12, SCLK 11, DC 5, CS 40, RESET 41, TE 6 |
| I2C | SDA 13, SCL 14: PCF85063 0x51, SHTC3 0x70, ES8311 0x18 |
| Keys | KEY 18, BOOT 0, active low |
| Battery | GPIO4 / ADC1 channel 3 through a 1:3 divider |
| TF card | SDMMC 1-bit: CLK 38, CMD 21, D0 39 |
| Speaker | I2S MCLK 16, BCLK 9, WS 45, DOUT 8; amplifier enable 46 |

The ST7305 initialization sequence and pixel layout come from the Waveshare
ESP32-S3-RLCD-4.2 factory program; see [driver provenance](UPSTREAM.md).
The battery percentage is a linear voltage proxy between
3.0 V and 4.12 V; the board has no fuel gauge or charger status, so the OTA battery gate is
not registered (the voltage cannot distinguish battery from USB power).

The trend requires a mounted FAT TF card; absence is nonfatal. Its record format matches the
ePaper 3.97 target: up to 48 timestamped records in `trend.bin`, with `trend.bak` used when the
primary file is missing, empty, truncated, oversized, unreadable or contains an invalid record.
Sampling starts only after a Bridge publication supplies a valid epoch. Until then, the trend
page shows `WAITING FOR TIME`, including when stored history is present; stored timestamps
are not used as the current time.

## Hardware verification checklist

- Panel orientation and pixel order (text reads correctly in LAND, PORT and FLIP).
- KEY / BOOT debounce and the 800 ms hold.
- SHTC3 readings against a reference; Waveshare's example subtracts 4 °C for board
  self-heating, which this firmware does not apply until measured.
- Battery percentage on battery and on USB power.
- ES8311 tone level and amplifier noise.
- TF card mount, trend persistence and backup recovery.
- Pairing, reconnect, usage rendering with a real Bridge, and OTA with physical confirmation.

## Glyphs and preview

The pixel font and the seven-segment digits are defined in `main/canvas.cpp`; there are no
generated font files or third-party font data.

`tools/rlcd42/render_preview.cpp` renders every screen on a host into PBM images using the
design's sample data; the build command is in the file header.
