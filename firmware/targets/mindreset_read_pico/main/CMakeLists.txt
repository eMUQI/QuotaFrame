idf_component_register(
    SRCS "app_main.cpp" "board.cpp" "demo_view.cpp" "display_ui.cpp" "fonts.cpp"
    INCLUDE_DIRS "."
    REQUIRES usage_core usage_ble usage_ota nvs_flash esp_driver_i2c
             esp_driver_usb_serial_jtag esp_timer esp_system
             read_pico read_pico_pmu epdiy e0470_epaper_waveform cst836u sc7a20h)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
