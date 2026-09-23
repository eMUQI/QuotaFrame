idf_component_register(
    SRCS
        "test_main.cpp"
        "test_local_clock.cpp"
        "test_orientation.cpp"
        "test_gestures.cpp"
        "test_panel_settings.cpp"
        "../../../main/panel_settings.cpp"
        "../../../main/local_clock.cpp"
        "../../../main/orientation.cpp"
        "../../../main/gesture_tracker.cpp"
        "../../../main/gesture_image.cpp"
    INCLUDE_DIRS "../../../main"
    REQUIRES unity usage_protocol usage_panel_state nvs_flash
    WHOLE_ARCHIVE
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
