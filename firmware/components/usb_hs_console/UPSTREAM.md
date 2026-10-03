# Upstream source

- Repository: https://github.com/esp-mosaico/esp-mosaico-claw
- Path: `boards/esp_mosaico/components/usb_hs_console`
- Commit: `9ea326da1f2a017ba81f1d391862f977b355a03e`
- License: Apache-2.0 (see `LICENSE` and source SPDX headers).

This component initializes a TinyUSB CDC console on the Mosaico Type-C USB
OTG port. It implements the USB Serial/JTAG DTR/RTS reset protocol in software;
it does not expose hardware JTAG. Only the ESP-Mosaico target includes it.

## Local modifications

- Declare the ESP-IDF system, timer, ROM and SoC dependencies explicitly.
- Require ESP-IDF >=6.1 and constrain esp_tinyusb to the compatible 2.x API;
  the target lockfile records the resolved version.
- Include the ROM print declaration and correct obsolete Kconfig names in the
  public header; document initialization errors, serialized access and port
  re-enumeration. Clarify the stdio buffering comment.
- Continue application startup with a warning if USB console initialization
  fails, preserving the display and BLE recovery path.

The reset state machine and USB descriptor are retained from upstream.
Software download requires a running application; BOOT plus power-on remains
necessary for recovery when USB initialization or application startup fails.
