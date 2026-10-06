# Board component provenance

Source: https://github.com/MindReset/read_pico_firmware
Commit: `28cde682a4468a581c278761922724f57d976418`
Path: `components/{epdiy,e0470_epaper_waveform,read_pico,read_pico_pmu,cst836u,sc7a20h,fca9555,sy7636a,pwm_audio}`

| Component | Role | License |
| --- | --- | --- |
| `epdiy` | Trimmed fork of epdiy v2.0.0 for the ESP32-S3 LCD peripheral path | LGPL-3.0-or-later |
| `e0470_epaper_waveform` | E0470A01 waveform tables and boot-time trimmer | Apache-2.0 |
| `read_pico` | Board support: I2C, panel rails and scan timing, microSD, flash high-performance mode | Apache-2.0 |
| `read_pico_pmu` | CW32L010 PMU host: battery, RTC, power state | Apache-2.0 |
| `cst836u`, `sc7a20h`, `fca9555`, `sy7636a` | Touch, accelerometer, IO expander and panel PMIC drivers | Apache-2.0 |
| `pwm_audio` | Buzzer backend required by `read_pico` | Apache-2.0 |

Each directory keeps its upstream `LICENSE`. Local changes are limited to:

- Removal of `read_pico_pmu/docs/` (PMU datasheets and protocol references, 3.6 MB);
  `read_pico_pmu/README.md` still refers to that directory.
- Trailing whitespace cleanup in epdiy `src/board/epd_board.c`, `src/diff.S`,
  `src/output_common/lut.S` and `src/render.c` for the repository whitespace check.
  Driver behavior and waveform data are unchanged.

`epdiy` is statically linked under LGPL-3.0-or-later. This repository contains the
complete corresponding source and build configuration needed to relink the firmware
with a modified copy.

To update, replace each directory with the same path at the new upstream commit,
remove `read_pico_pmu/docs/` again and record the commit here.
