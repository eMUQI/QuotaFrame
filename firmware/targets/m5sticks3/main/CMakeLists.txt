idf_component_register(
    SRCS
        "app_main.cpp"
        "m5gfx_spi_compat.c"
        "battery_view.cpp"
        "button_controller.cpp"
        "display_ui.cpp"
        "page_state.cpp"
        "power_reading.cpp"
        "render_schedule.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        usage_core
        usage_ble
        usage_ota
    PRIV_REQUIRES esp_driver_spi
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=spi_bus_initialize" "-Wl,-u,__wrap_spi_bus_initialize")
