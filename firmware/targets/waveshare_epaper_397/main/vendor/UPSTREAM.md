# Screen driver provenance

Source: https://github.com/waveshareteam/ESP32-S3-ePaper-3.97
Commit: `9b12d40731a80213b927ee8a421cae4082952819`
Path: `ESP-IDF/01_E-Paper_Example/components/epaper_port/epaper_port.{c,h}`

The waveform initialization and grayscale bitplane mapping follow the official example.
Local integration removes GUI dependencies and the unused clear, fast and
one-shot display paths, bounds BUSY waiting to 15 seconds,
propagates SPI failures, corrects the GPIO pull-up enum and batches grayscale
bitplanes from a persistent PSRAM buffer instead of issuing one SPI transaction per byte.

Native framebuffer Y coordinates map to descending SSD1677 gate addresses.
Full-frame writes start at RAM row 479. Partial writes map `[Ystart, Yend)`
to `[479 - Ystart, 480 - Yend]`, start the Y pointer at the high address and
explicitly select X-increment/Y-decrement entry mode. X register endpoints
are inclusive pixel addresses. The public partial API accepts byte-aligned,
nonempty rectangles with exclusive end coordinates.

Addressing reference:
https://github.com/ZinggJM/GxEPD2/blob/master/src/gdem/GxEPD2_397_GDEM0397T81.cpp
(`_setPartialRamArea`).

After a partial waveform completes, the displayed region is written to both
previous RAM (`0x26`) and current RAM (`0x24`), with the RAM pointer reset before
each write. Keeping both planes equal prevents earlier content from returning
when a later update changes another rectangle. This follows `writeImageAgain`
in the same GxEPD2 panel driver.
