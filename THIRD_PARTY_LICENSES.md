# Third-Party Licenses

This file records every top-level component in the four production `dependencies.lock` files, vendored source and generated font subsets and the Python packages that are shipped in, or materially used to build, the native Bridge release artifacts. Firmware versions below are locked. Development dependency ranges remain declared in `bridge/pyproject.toml`. The human-readable release version sets live in `bridge/release-constraints-windows.txt` and `bridge/release-constraints-macos.txt`, while native release builds actually install external packages from the platform-specific `bridge/release-lock-windows.txt` and `bridge/release-lock-macos.txt` files, which pin both package versions and the SHA-256 of the exact compatible wheels. The Python versions listed below are the release-lock values, not the broader development ranges.

For ESP Component Registry entries, `Registry license` is the API value for the exact locked version. When that value is `Custom`, `SPDX conclusion` is derived from the linked license text rather than presented as Registry metadata. The source and notice links are version-specific where available.

## `cube32esp/xpowerslib`

- Version: `0.3.3`
- Source: https://components.espressif.com/components/cube32esp/xpowerslib/versions/0.3.3
- Registry license / SPDX: `MIT`
- Copyright/notice: https://components.espressif.com/components/cube32esp/xpowerslib/versions/0.3.3/license

## `esp-mosaico-bsp`

- Version: `392860b1d1a123c3377947074b2af1f600e86c5d`
- Source: https://github.com/esp-mosaico/esp-mosaico-bsp/tree/392860b1d1a123c3377947074b2af1f600e86c5d/components/esp-mosaico-bsp
- SPDX: `Apache-2.0`
- Copyright/notice: https://github.com/esp-mosaico/esp-mosaico-bsp/blob/392860b1d1a123c3377947074b2af1f600e86c5d/LICENSE

## `esp_desktop_buddy`

- Version: `b6bac05db208717676e70180e5269d79f32b2d68`
- Source: https://github.com/espressif/esp-desktop-buddy/tree/b6bac05db208717676e70180e5269d79f32b2d68/components/esp_desktop_buddy
- SPDX: `Apache-2.0`
- Copyright/notice: https://github.com/espressif/esp-desktop-buddy/blob/b6bac05db208717676e70180e5269d79f32b2d68/LICENSE

## `esp_desktop_buddy_transport_ble`

- Version: `b6bac05db208717676e70180e5269d79f32b2d68`
- Source: https://github.com/espressif/esp-desktop-buddy/tree/b6bac05db208717676e70180e5269d79f32b2d68/components/esp_desktop_buddy_transport_ble
- SPDX: `Apache-2.0`
- Copyright/notice: https://github.com/espressif/esp-desktop-buddy/blob/b6bac05db208717676e70180e5269d79f32b2d68/LICENSE

## `espressif/button`

- Version: `4.2.0`
- Source: https://components.espressif.com/components/espressif/button/versions/4.2.0
- Registry license: `Custom`; SPDX conclusion: `Apache-2.0`
- Copyright/notice: https://components-file.espressif.com/components/espressif/button/4.2.0/license.txt

- ESP-Mosaico version: `4.2.1`; source: https://components.espressif.com/components/espressif/button/versions/4.2.1; copyright/notice: https://components.espressif.com/components/espressif/button/versions/4.2.1/license

## `espressif/cjson`

- Version: `1.7.19~2`
- Source: https://components.espressif.com/components/espressif/cjson/versions/1.7.19~2
- Registry license / SPDX: `MIT`
- Copyright/notice: https://components-file.espressif.com/components/espressif/cjson/1.7.19~2/license.txt

## `espressif/cmake_utilities`

- Version: `0.5.3`
- Source: https://components.espressif.com/components/espressif/cmake_utilities/versions/0.5.3
- Registry license / SPDX: `Apache-2.0`
- Copyright/notice: https://components-file.espressif.com/components/espressif/cmake_utilities/0.5.3/license.txt

## `espressif/esp_codec_dev`

- Version: `1.5.6`
- Source: https://components.espressif.com/components/espressif/esp_codec_dev/versions/1.5.6
- Registry license / SPDX: `Apache-2.0`
- Copyright/notice: https://components-file.espressif.com/components/espressif/esp_codec_dev/1.5.6/license.txt

- ESP-Mosaico version: `1.6.2`; source: https://components.espressif.com/components/espressif/esp_codec_dev/versions/1.6.2; copyright/notice: https://components.espressif.com/components/espressif/esp_codec_dev/versions/1.6.2/license

## `espressif/esp_io_expander`

- Version: `1.2.1`
- Source: https://components.espressif.com/components/espressif/esp_io_expander/versions/1.2.1
- Registry license / SPDX: `Apache-2.0`
- Copyright/notice: https://components-file.espressif.com/components/espressif/esp_io_expander/1.2.1/license.txt

## `espressif/esp_lcd_co5300`

- Version: `2.1.0`
- Source: https://components.espressif.com/components/espressif/esp_lcd_co5300/versions/2.1.0
- Registry license: `Custom`; SPDX conclusion: `Apache-2.0`
- Copyright/notice: https://components-file.espressif.com/components/espressif/esp_lcd_co5300/2.1.0/license.txt

- ESP-Mosaico version: `2.2.0`; source: https://components.espressif.com/components/espressif/esp_lcd_co5300/versions/2.2.0; copyright/notice: https://components.espressif.com/components/espressif/esp_lcd_co5300/versions/2.2.0/license

## `espressif/esp_lcd_panel_io_additions`

- Version: `1.0.1~1`
- Source: https://components.espressif.com/components/espressif/esp_lcd_panel_io_additions/versions/1.0.1~1
- Registry license / SPDX: `Apache-2.0`
- Copyright/notice: https://components-file.espressif.com/components/espressif/esp_lcd_panel_io_additions/1.0.1~1/license.txt

## `espressif/esp_lcd_touch`

- Version: `1.2.1`
- Source: https://components.espressif.com/components/espressif/esp_lcd_touch/versions/1.2.1
- Registry license / SPDX: `Apache-2.0`
- Copyright/notice: https://components-file.espressif.com/components/espressif/esp_lcd_touch/1.2.1/license.txt

## `espressif/esp_lv_decoder`

- Version: `0.4.3`
- Source: https://components.espressif.com/components/espressif/esp_lv_decoder/versions/0.4.3
- Registry license: `Custom`; SPDX conclusion: `Apache-2.0`
- Copyright/notice: https://components-file.espressif.com/components/espressif/esp_lv_decoder/0.4.3/license.txt

## `espressif/esp_lv_fs`

- Version: `1.0.1`
- Source: https://components.espressif.com/components/espressif/esp_lv_fs/versions/1.0.1
- Registry license / SPDX: `Apache-2.0`
- Copyright/notice: https://components-file.espressif.com/components/espressif/esp_lv_fs/1.0.1/license.txt

## `espressif/esp_lvgl_adapter`

- Version: `0.6.3`
- Source: https://components.espressif.com/components/espressif/esp_lvgl_adapter/versions/0.6.3
- Registry license: `Custom`; SPDX conclusion: `Apache-2.0`
- Copyright/notice: https://components-file.espressif.com/components/espressif/esp_lvgl_adapter/0.6.3/license.txt

- ESP-Mosaico version: `0.6.2`; source: https://components.espressif.com/components/espressif/esp_lvgl_adapter/versions/0.6.2; copyright/notice: https://components.espressif.com/components/espressif/esp_lvgl_adapter/versions/0.6.2/license

## `espressif/esp_mmap_assets`

- Version: `2.0.0`
- Source: https://components.espressif.com/components/espressif/esp_mmap_assets/versions/2.0.0
- Registry license / SPDX: `Apache-2.0`
- Copyright/notice: https://components-file.espressif.com/components/espressif/esp_mmap_assets/2.0.0/license.txt

- ESP-Mosaico version: `2.0.1`; source: https://components.espressif.com/components/espressif/esp_mmap_assets/versions/2.0.1; copyright/notice: https://components.espressif.com/components/espressif/esp_mmap_assets/versions/2.0.1/license

## `espressif/esp_new_jpeg`

- Version: `1.0.2`
- Source: https://components.espressif.com/components/espressif/esp_new_jpeg/versions/1.0.2
- Registry license: `Custom`; SPDX conclusion: `LicenseRef-Espressif-MIT`
- Copyright/notice: https://components-file.espressif.com/components/espressif/esp_new_jpeg/1.0.2/license.txt

## `espressif/freetype`

- Version: `2.14.2`
- Source: https://components.espressif.com/components/espressif/freetype/versions/2.14.2
- Registry license: `Custom`; SPDX conclusion: `FTL OR GPL-2.0-or-later`
- Copyright/notice: https://components-file.espressif.com/components/espressif/freetype/2.14.2/license.txt

- ESP-Mosaico version: `2.14.3~1`; source: https://components.espressif.com/components/espressif/freetype/versions/2.14.3~1; copyright/notice: https://components.espressif.com/components/espressif/freetype/versions/2.14.3~1/license

## `espressif/knob`

- Version: `1.1.0`
- Source: https://components.espressif.com/components/espressif/knob/versions/1.1.0
- Registry license: `Custom`; SPDX conclusion: `Apache-2.0`
- Copyright/notice: https://components-file.espressif.com/components/espressif/knob/1.1.0/license.txt

## `espressif/libpng`

- Version: `1.6.58`
- Source: https://components.espressif.com/components/espressif/libpng/versions/1.6.58
- Registry license: `Custom`; SPDX conclusion: `libpng-2.0`
- Copyright/notice: https://components-file.espressif.com/components/espressif/libpng/1.6.58/license.txt

- ESP-Mosaico version: `1.6.58~1`; source: https://components.espressif.com/components/espressif/libpng/versions/1.6.58~1; copyright/notice: https://components.espressif.com/components/espressif/libpng/versions/1.6.58~1/license

## `espressif/zlib`

- Version: `1.3.2`
- Source: https://components.espressif.com/components/espressif/zlib/versions/1.3.2
- Registry license: `Custom`; SPDX conclusion: `Zlib`
- Copyright/notice: https://components-file.espressif.com/components/espressif/zlib/1.3.2/license.txt

- ESP-Mosaico version: `1.3.2~1`; source: https://components.espressif.com/components/espressif/zlib/versions/1.3.2~1; copyright/notice: https://components.espressif.com/components/espressif/zlib/versions/1.3.2~1/license

## `espressif/usb`

- Version: `1.3.0`
- Source: https://components.espressif.com/components/espressif/usb/versions/1.3.0
- SPDX: `Apache-2.0`
- Copyright/notice: https://github.com/espressif/esp-usb/blob/ded385702ecae4d5c15c497149062ce577fa167a/host/usb/LICENSE

## `idf`

- Name: ESP-IDF
- Version: `6.1`
- Source: https://github.com/espressif/esp-idf/tree/v6.1
- SPDX: `Apache-2.0`
- Copyright/notice: https://github.com/espressif/esp-idf/blob/v6.1/LICENSE

## `lvgl/lvgl`

- Version: `9.3.0`
- Source: https://components.espressif.com/components/lvgl/lvgl/versions/9.3.0
- Registry license: `Custom`; SPDX conclusion: `MIT`
- Copyright/notice: https://components-file.espressif.com/components/lvgl/lvgl/9.3.0/license.txt

- ESP-Mosaico version: `9.5.0`; source: https://components.espressif.com/components/lvgl/lvgl/versions/9.5.0; copyright/notice: https://components.espressif.com/components/lvgl/lvgl/versions/9.5.0/license

## `m5stack/m5gfx`

- Version: `0.2.27`
- Source: https://components.espressif.com/components/m5stack/m5gfx/versions/0.2.27
- Registry license: `Custom`; SPDX conclusion: `MIT`
- Copyright/notice: https://components-file.espressif.com/components/m5stack/m5gfx/0.2.27/license.txt

## `m5stack/m5unified`

- Version: `0.2.20`
- Source: https://components.espressif.com/components/m5stack/m5unified/versions/0.2.20
- Registry license: `Custom`; SPDX conclusion: `MIT`
- Copyright/notice: https://components-file.espressif.com/components/m5stack/m5unified/0.2.20/license.txt

## Montserrat generated font subsets

- Upstream commit: `038b637da7b3fd956a4ed93ffc607c3d5e4ce172`
- Source: https://github.com/google/fonts/tree/038b637da7b3fd956a4ed93ffc607c3d5e4ce172/ofl/montserrat
- SPDX: `OFL-1.1`
- Generated subsets: Bold 120 px; Semibold 13 px and 20 px
- Copyright/notice: `firmware/targets/esp32_s3_touch_amoled_216/main/fonts/OFL.txt`

## Archivo generated font subsets

- Source: https://github.com/google/fonts/tree/main/ofl/archivo (`Archivo[wdth,wght].ttf`); the upstream commit used for generation was not recorded
- SPDX: `OFL-1.1`
- Generated subsets: weight 700; printable ASCII at 24–56 px, digits and `%:-. ` at 104–184 px
- Copyright/notice: `firmware/targets/waveshare_epaper_397/main/fonts/Archivo-OFL.txt`

## JetBrains Mono generated font subsets

- Source: https://github.com/google/fonts/tree/main/ofl/jetbrainsmono (`JetBrainsMono[wght].ttf`); the upstream commit used for generation was not recorded
- SPDX: `OFL-1.1`
- Generated subsets: weight 500; printable ASCII at 15–20 px
- Copyright/notice: `firmware/targets/waveshare_epaper_397/main/fonts/JetBrainsMono-OFL.txt`

## Waveshare ESP32-S3-ePaper-3.97 screen driver

- Upstream commit: `9b12d40731a80213b927ee8a421cae4082952819`
- Source: https://github.com/waveshareteam/ESP32-S3-ePaper-3.97/tree/9b12d40731a80213b927ee8a421cae4082952819/ESP-IDF/01_E-Paper_Example/components/epaper_port
- License: not declared. The repository has no license file and the source files carry no license header at this commit; redistribution terms are unconfirmed.
- Local copy and modifications: `firmware/targets/waveshare_epaper_397/main/vendor/` (see `UPSTREAM.md`)

## `waveshare/esp32_s3_touch_amoled_2_16`

- Version: `2.0.1`
- Source: https://components.espressif.com/components/waveshare/esp32_s3_touch_amoled_2_16/versions/2.0.1
- Registry license: `Custom`; SPDX conclusion: `Apache-2.0`
- Copyright/notice: https://components-file.espressif.com/components/waveshare/esp32_s3_touch_amoled_2_16/2.0.1/license.txt

## `waveshare/esp_lcd_touch_cst9217`

- Version: `2.0.0`
- Source: https://components.espressif.com/components/waveshare/esp_lcd_touch_cst9217/versions/2.0.0
- Registry license: `Custom`; SPDX conclusion: `Apache-2.0`
- Copyright/notice: https://components-file.espressif.com/components/waveshare/esp_lcd_touch_cst9217/2.0.0/license.txt

## `waveshare/qmi8658`

- Version: `2.0.0`
- Source: https://components.espressif.com/components/waveshare/qmi8658/versions/2.0.0
- Registry license: `Custom`; SPDX conclusion: `Apache-2.0`
- Copyright/notice: https://components-file.espressif.com/components/waveshare/qmi8658/2.0.0/license.txt

## `bleak`

- Release version: `3.0.2` (Windows and macOS)
- Development range: `>=1.0,<4`
- Source: https://github.com/hbldh/bleak
- SPDX: `MIT`
- Copyright/notice: https://github.com/hbldh/bleak/blob/develop/LICENSE

## `pystray`

- Release version: `0.19.5` (Windows only)
- Development range: `>=0.19`
- Source: https://github.com/moses-palmer/pystray
- SPDX: `LGPL-3.0-only`
- Copyright/notice: https://github.com/moses-palmer/pystray/blob/master/COPYING.LGPL

## `Pillow`

- Release version: `12.3.0` (Windows only)
- Development range: `>=10`
- Source: https://github.com/python-pillow/Pillow
- SPDX: `MIT-CMU`
- Copyright/notice: https://github.com/python-pillow/Pillow/blob/main/LICENSE

## `PyInstaller`

- Release build version: `6.22.0`
- Development range: `>=6.21`
- Source: https://github.com/pyinstaller/pyinstaller
- SPDX: `GPL-2.0-or-later WITH Bootloader-exception`
- Copyright/notice: https://github.com/pyinstaller/pyinstaller/blob/develop/COPYING.txt

## `pyobjc-framework-UserNotifications`

- Release version: `12.2.2` (macOS only)
- Development range: `>=10.3`
- Source: https://github.com/ronaldoussoren/pyobjc/tree/master/pyobjc-framework-UserNotifications
- SPDX: `MIT`
- Copyright/notice: https://github.com/ronaldoussoren/pyobjc/blob/master/LICENSE.txt

## `six`

- Release version: `1.17.0` (Windows only; through `pystray`)
- Source: https://github.com/benjaminp/six
- SPDX: `MIT`
- Copyright/notice: https://github.com/benjaminp/six/blob/main/LICENSE

## `typing_extensions`

- Release version: `4.16.0` (Windows only; through `bleak`)
- Source: https://github.com/python/typing_extensions
- SPDX: `PSF-2.0`
- Copyright/notice: https://github.com/python/typing_extensions/blob/main/LICENSE

## `pyobjc-core`

- Release version: `12.2.2` (macOS only)
- Source: https://github.com/ronaldoussoren/pyobjc/tree/master/pyobjc-core
- SPDX: `MIT`
- Copyright/notice: https://github.com/ronaldoussoren/pyobjc/blob/master/LICENSE.txt

## `pyobjc-framework-Cocoa`

- Release version: `12.2.2` (macOS only)
- Source: https://github.com/ronaldoussoren/pyobjc/tree/master/pyobjc-framework-Cocoa
- SPDX: `MIT`
- Copyright/notice: https://github.com/ronaldoussoren/pyobjc/blob/master/LICENSE.txt

## `pyobjc-framework-CoreBluetooth`

- Release version: `12.2.2` (macOS only; through `bleak`)
- Source: https://github.com/ronaldoussoren/pyobjc/tree/master/pyobjc-framework-CoreBluetooth
- SPDX: `MIT`
- Copyright/notice: https://github.com/ronaldoussoren/pyobjc/blob/master/LICENSE.txt

## `pyobjc-framework-libdispatch`

- Release version: `12.2.2` (macOS only; through `bleak`)
- Source: https://github.com/ronaldoussoren/pyobjc/tree/master/pyobjc-framework-libdispatch
- SPDX: `MIT`
- Copyright/notice: https://github.com/ronaldoussoren/pyobjc/blob/master/LICENSE.txt

## `winrt-runtime`

- Release version: `3.2.1` (Windows only; through `bleak`)
- Source: https://github.com/pywinrt/pywinrt/tree/main/packages/runtime
- SPDX: `MIT`
- Copyright/notice: https://github.com/pywinrt/pywinrt/blob/main/LICENSE

## `winrt-windows-devices-bluetooth`

- Release version: `3.2.1` (Windows only; through `bleak`)
- Source: https://github.com/pywinrt/pywinrt
- SPDX: `MIT`
- Copyright/notice: https://github.com/pywinrt/pywinrt/blob/main/LICENSE

## `winrt-windows-devices-bluetooth-advertisement`

- Release version: `3.2.1` (Windows only; through `bleak`)
- Source: https://github.com/pywinrt/pywinrt
- SPDX: `MIT`
- Copyright/notice: https://github.com/pywinrt/pywinrt/blob/main/LICENSE

## `winrt-windows-devices-bluetooth-genericattributeprofile`

- Release version: `3.2.1` (Windows only; through `bleak`)
- Source: https://github.com/pywinrt/pywinrt
- SPDX: `MIT`
- Copyright/notice: https://github.com/pywinrt/pywinrt/blob/main/LICENSE

## `winrt-windows-devices-enumeration`

- Release version: `3.2.1` (Windows only; through `bleak`)
- Source: https://github.com/pywinrt/pywinrt
- SPDX: `MIT`
- Copyright/notice: https://github.com/pywinrt/pywinrt/blob/main/LICENSE

## `winrt-windows-devices-radios`

- Release version: `3.2.1` (Windows only; through `bleak`)
- Source: https://github.com/pywinrt/pywinrt
- SPDX: `MIT`
- Copyright/notice: https://github.com/pywinrt/pywinrt/blob/main/LICENSE

## `winrt-windows-foundation`

- Release version: `3.2.1` (Windows only; through `bleak`)
- Source: https://github.com/pywinrt/pywinrt
- SPDX: `MIT`
- Copyright/notice: https://github.com/pywinrt/pywinrt/blob/main/LICENSE

## `winrt-windows-foundation-collections`

- Release version: `3.2.1` (Windows only; through `bleak`)
- Source: https://github.com/pywinrt/pywinrt
- SPDX: `MIT`
- Copyright/notice: https://github.com/pywinrt/pywinrt/blob/main/LICENSE

## `winrt-windows-storage-streams`

- Release version: `3.2.1` (Windows only; through `bleak`)
- Source: https://github.com/pywinrt/pywinrt
- SPDX: `MIT`
- Copyright/notice: https://github.com/pywinrt/pywinrt/blob/main/LICENSE

## `Python`

- Release build/runtime version: `3.13.14`
- Supported development/runtime range: `>=3.11`
- Source: https://github.com/python/cpython
- SPDX: `PSF-2.0`
- Copyright/notice: https://docs.python.org/3/license.html

## `altgraph`

- Release build version: `0.17.5` (Windows and macOS)
- Source: https://github.com/ronaldoussoren/altgraph
- SPDX: `MIT`
- Copyright/notice: https://github.com/ronaldoussoren/altgraph/blob/master/LICENSE

## `macholib`

- Release build version: `1.16.4` (macOS only)
- Source: https://github.com/ronaldoussoren/macholib
- SPDX: `MIT`
- Copyright/notice: https://github.com/ronaldoussoren/macholib/blob/master/LICENSE

## `packaging`

- Release build version: `26.2` (Windows and macOS)
- Source: https://github.com/pypa/packaging
- SPDX: `Apache-2.0 OR BSD-2-Clause`
- Copyright/notice: https://github.com/pypa/packaging/blob/main/LICENSE

## `pefile`

- Release build version: `2024.8.26` (Windows only)
- Source: https://github.com/erocarrera/pefile
- SPDX: `MIT`
- Copyright/notice: https://github.com/erocarrera/pefile/blob/master/LICENSE

## `pip`

- Release build version: `26.1.2` (Windows and macOS)
- Source: https://github.com/pypa/pip
- SPDX: `MIT`
- Copyright/notice: https://github.com/pypa/pip/blob/main/LICENSE.txt

## `pyinstaller-hooks-contrib`

- Release build version: `2026.6` (Windows and macOS)
- Source: https://github.com/pyinstaller/pyinstaller-hooks-contrib
- SPDX: `Apache-2.0 OR GPL-2.0-or-later`
- Copyright/notice: https://github.com/pyinstaller/pyinstaller-hooks-contrib/blob/master/LICENSE

## `pywin32-ctypes`

- Release build version: `0.2.3` (Windows only)
- Source: https://github.com/enthought/pywin32-ctypes
- SPDX: `BSD-3-Clause`
- Copyright/notice: https://github.com/enthought/pywin32-ctypes/blob/master/LICENSE.txt

## `setuptools`

- Release build version: `83.0.0` (Windows and macOS)
- Source: https://github.com/pypa/setuptools
- SPDX: `MIT`
- Copyright/notice: https://github.com/pypa/setuptools/blob/main/LICENSE

## `esp-web-tools`

- Web flasher version: `10.4.0`
- npm integrity: `sha512-3pwkeFFm5Fj7UQo8SJNYK5RXrtNCpq6X9QoI6bMT4GBZWgrJqjn0YvM9ihG74BtMoSFYXfmDtkehuxe50PTMPQ==`
- Source commit: https://github.com/esphome/esp-web-tools/commit/fab2ba48253d19fd29a15af57bec771d34978b62
- SPDX: `Apache-2.0`
- Copyright/notice: https://github.com/esphome/esp-web-tools/blob/fab2ba48253d19fd29a15af57bec771d34978b62/LICENSE

## `Win-CodexBar`

- Managed CLI version: `1.2.12`
- Source: https://github.com/nesszer/Win-CodexBar/tree/v1.2.12
- Download: https://github.com/nesszer/Win-CodexBar/releases/download/v1.2.12/codexbar.exe
- SHA-256: `a9d5d603705d168f7b587066d469deff9f2acb6780f022c88e55c4fc41ed7166`
- SPDX: `MIT`
- The CLI is fetched directly from the upstream release when missing. The Bridge does not install the upstream desktop application.
MIT License

Copyright (c) 2025 Peter Steinberger

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## `Inno Setup`

- Build tool version: `6.7.3`
- Source and license: https://github.com/jrsoftware/issrc/tree/is-6_7_3
- Package SHA-256: `9c73c3bae7ed48d44112a0f48e66742c00090bdb5bef71d9d3c056c66e97b732`
- The compiler is prepared in portable mode under the build directory. Its generated installer retains the upstream copyright notices.
- Chinese messages: `Files/Languages/Unofficial/ChineseSimplified.isl` at commit `4adf37ed7f3fd2bd11c6836ba056e3de170fbabf`, SHA-256 `7d544b9bb1d142cfa11f2e5d3cc8abe2e55f8e066c5124e3772675aa236e1278`.

## `espressif/bq27220`

- Version: `0.1.2`
- Source: https://components.espressif.com/components/espressif/bq27220/versions/0.1.2
- SPDX conclusion from distributed license: `Apache-2.0`
- Copyright/notice: https://components.espressif.com/components/espressif/bq27220/versions/0.1.2/license

## `espressif/dhara`

- Version: `1.0.0`
- Source: https://components.espressif.com/components/espressif/dhara/versions/1.0.0
- SPDX conclusion from distributed license: `ISC`
- Copyright/notice: https://components.espressif.com/components/espressif/dhara/versions/1.0.0/license

## `espressif/esp_lcd_touch_cst9220`

- Version: `0.1.1`
- Source: https://components.espressif.com/components/espressif/esp_lcd_touch_cst9220/versions/0.1.1
- SPDX conclusion from distributed license: `Apache-2.0`
- Copyright/notice: https://components.espressif.com/components/espressif/esp_lcd_touch_cst9220/versions/0.1.1/license

## `espressif/i2c_bus`

- Version: `1.5.2`
- Source: https://components.espressif.com/components/espressif/i2c_bus/versions/1.5.2
- SPDX conclusion from distributed license: `Apache-2.0`
- Copyright/notice: https://components.espressif.com/components/espressif/i2c_bus/versions/1.5.2/license

## `espressif/spi_nand_flash`

- Version: `1.4.4`
- Source: https://components.espressif.com/components/espressif/spi_nand_flash/versions/1.4.4
- SPDX conclusion from distributed license: `Apache-2.0`
- Copyright/notice: https://components.espressif.com/components/espressif/spi_nand_flash/versions/1.4.4/license

## `espressif2022/bmi270`

- Version: `1.1.0~2`
- Source: https://components.espressif.com/components/espressif2022/bmi270/versions/1.1.0~2
- SPDX conclusion from distributed license: `BSD-3-Clause`
- Copyright/notice: https://components.espressif.com/components/espressif2022/bmi270/versions/1.1.0~2/license

## `tangyumei3535/bmm150_sensorapi`

- Version: `1.0.0`
- Source: https://components.espressif.com/components/tangyumei3535/bmm150_sensorapi/versions/1.0.0
- SPDX conclusion from distributed license: `BSD-3-Clause`
- Copyright/notice: https://components.espressif.com/components/tangyumei3535/bmm150_sensorapi/versions/1.0.0/license
