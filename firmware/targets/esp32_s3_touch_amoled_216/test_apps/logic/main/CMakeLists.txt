idf_component_register(
    SRCS
        "test_main.cpp"
        "test_display_transport.cpp"
        "test_orientation.cpp"
        "test_rtc_clock.cpp"
        "../../../main/orientation.cpp"
    INCLUDE_DIRS "../../../main"
    REQUIRES unity usage_core usage_ota usage_protocol usage_rtc usage_panel_state esp_driver_i2c lvgl__lvgl
    WHOLE_ARCHIVE
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
