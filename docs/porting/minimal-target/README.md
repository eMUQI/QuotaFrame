# Minimal Target Skeleton

This directory is a copyable structural example for a new hardware port. It is not a maintained production target and is intentionally not registered in the Target Registry or CI matrix.

Copy it to:

```text
firmware/targets/<target>/
```

Then customize the copied project before attempting registration.

## Files to customize

- `CMakeLists.txt`: change the ESP-IDF project name and remove shared components you do not use.
- `sdkconfig.defaults`: set the actual chip/flash/PSRAM/rollback requirements for the board.
- `partitions.csv`: size application partitions for the real flash capacity and built image.
- `main/CMakeLists.txt`: add the target's board/UI source files and component dependencies.
- `main/app_main.cpp`: replace the placeholders with real board initialization, display/UI, input, BLE configuration, and OTA integration.
- `main/idf_component.yml`: add one after choosing the board/BSP/component-manager dependencies.

Follow the [bring-up sequence and acceptance checklist](../../PORTING.md) before registration. This skeleton does not implement a complete BLE device; supply the board's BSP, display, input and application logic.
