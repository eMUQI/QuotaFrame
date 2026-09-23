idf_component_register(
    SRCS
        "app_main.cpp"
        "display_ui.cpp"
        "local_clock.cpp"
        "orientation.cpp"
        "power_monitor.cpp"
        "camera_setting.cpp"
        "gesture_input.cpp"
        "gesture_tracker.cpp"
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

if(CONFIG_MOSAICO_GESTURE_INPUT)
    target_sources(${COMPONENT_LIB} PRIVATE "gesture_camera.cpp" "gesture_image.cpp")
endif()
