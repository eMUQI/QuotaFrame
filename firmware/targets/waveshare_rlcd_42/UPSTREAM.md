# Peripheral driver provenance

## ST7305 display

- Source: [Waveshare factory display driver](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2/blob/eb1f63427d735a22b9c30e22fa63ebddae1834d3/02_Example/ESP-IDF/10_FactoryProgram/components/port_bsp/display_bsp.cpp).
- Reference commit: `eb1f63427d735a22b9c30e22fa63ebddae1834d3`. The original import revision was not recorded; the initialization sequence was checked against this revision.
- License: [Apache-2.0](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2/blob/eb1f63427d735a22b9c30e22fa63ebddae1834d3/LICENSE), Copyright 2026 Waveshare.
- Local implementation: [main/st7305.cpp](main/st7305.cpp).

The controller initialization sequence and pixel layout follow the factory program. The local
implementation uses a row-major one-bit canvas, converts it to the controller's 2 x 4 blocks,
skips unchanged frames, and synchronizes writes with TE. DMA completion owns buffer reuse;
a timeout skips submission without modifying the active buffer. Factory GUI and drawing APIs
are not included.

## ES8311 audio codec

- Source version: [Espressif esp_codec_dev 1.3.5](https://components.espressif.com/components/espressif/esp_codec_dev/versions/1.3.5).
- Source file: [device/es8311/es8311.c](https://github.com/espressif/esp-adf/blob/9b35bca1a6db3d989936f228d6e28f33089fa9e7/components/esp_codec_dev/device/es8311/es8311.c), the revision linked by the component registry for this version.
- License: [Apache-2.0](https://components.espressif.com/components/espressif/esp_codec_dev/versions/1.3.5/license), Copyright 2023 Espressif Systems (Shanghai) CO LTD.
- Local implementation: [main/beeper.cpp](main/beeper.cpp).

The register table is derived from `es8311_open`, `es8311_config_sample` and `es8311_start`.
The local implementation fixes playback to DAC-only slave operation at 16 kHz with 256 fs MCLK,
uses ESP-IDF I2C/I2S directly, and generates a short alert tone on a dedicated FreeRTOS task.
It does not include the upstream codec abstraction, microphone path or variable-rate API.

The repository includes the [Apache-2.0 license text](../../components/esp-mosaico-bsp/LICENSE).
