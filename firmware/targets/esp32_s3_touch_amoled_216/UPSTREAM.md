# Waveshare AMOLED 2.16 panel initialization

The CO5300 register sequence in `main/display_ui.cpp` is adapted from the
[Waveshare AMOLED 2.16 BSP v2.0.1](https://components.espressif.com/components/waveshare/esp32_s3_touch_amoled_2_16/versions/2.0.1),
distributed under [Apache-2.0](https://components-file.espressif.com/components/waveshare/esp32_s3_touch_amoled_2_16/2.0.1/license.txt).
The local sequence initializes brightness to zero and defers Display On until
after the first full LVGL refresh. The Waveshare BSP remains the source of the
board pin definitions and touch initialization.
