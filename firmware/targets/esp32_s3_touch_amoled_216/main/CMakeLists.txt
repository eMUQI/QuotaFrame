idf_component_register(
    SRCS
        "app_main.cpp"
        "display_ui.cpp"
        "memory_telemetry.cpp"
        "performance_probe.cpp"
        "orientation.cpp"
        "power_monitor.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        esp_hw_support
        esp_driver_i2c
        heap
        esp_lvgl_adapter
        usage_core
        usage_panel_state
        usage_fonts
        usage_ble
        usage_ota
        usage_rtc
        cube32esp__xpowerslib
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_sources(${lvgl_lib} PRIVATE "lvgl_psram_pool.c")
target_include_directories(${lvgl_lib} PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
target_compile_definitions(${lvgl_lib} PRIVATE
    LV_MEM_POOL_INCLUDE=\"lvgl_psram_pool.h\"
    LV_MEM_POOL_ALLOC=lvgl_psram_pool_alloc
)
