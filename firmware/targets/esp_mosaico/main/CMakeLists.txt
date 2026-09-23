idf_component_register(
    SRCS
        "app_main.cpp"
        "display_ui.cpp"
        "local_clock.cpp"
        "orientation.cpp"
        "power_monitor.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        esp-mosaico-bsp
        usage_core
        usage_panel_state
        usage_fonts
        usage_ble
        usage_ota
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
