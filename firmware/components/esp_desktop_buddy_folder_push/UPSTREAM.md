# Upstream source

This component is a vendored source snapshot from:

- Repository: `https://github.com/espressif/esp-desktop-buddy.git`
- Path: `components/esp_desktop_buddy_folder_push`
- Commit: `b6bac05db208717676e70180e5269d79f32b2d68`
- License: Apache-2.0 (see the SPDX headers in the source files)

The upstream component manifest uses a sibling `path` dependency on `../esp_desktop_buddy`. ESP-IDF Component Manager does not include that sibling when it downloads a Git subdirectory component, so the pinned source is kept locally while `esp_desktop_buddy` itself remains a managed Git dependency.

The transfer-size Kconfig range permits up to 7 MiB plus 512 manifest bytes for
the Mosaico gesture firmware. The default remains 4 MiB plus 512 bytes; each
target selects its limit, and the OTA backend independently checks slot capacity.
