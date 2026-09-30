idf_component_register(
    SRCS "app_main.cpp" "beeper.cpp" "board.cpp" "canvas.cpp" "panel_logic.cpp"
         "screens.cpp" "st7305.cpp"
    INCLUDE_DIRS "."
    REQUIRES usage_core usage_ble usage_ota usage_rtc nvs_flash esp_driver_i2c esp_driver_spi
             esp_driver_gpio esp_driver_i2s esp_driver_sdmmc esp_lcd esp_adc fatfs esp_timer
             esp_system)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
