idf_component_register(
    SRCS "app_main.cpp" "board.cpp" "display_ui.cpp" "fonts.cpp"
         "vendor/epaper_port.c"
    INCLUDE_DIRS "." "vendor"
    REQUIRES usage_core usage_ble usage_ota usage_rtc nvs_flash esp_driver_i2c
             esp_driver_spi esp_driver_gpio esp_driver_usb_serial_jtag
             esp_driver_sdmmc fatfs esp_timer esp_system
             cube32esp__xpowerslib waveshare__qmi8658)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
