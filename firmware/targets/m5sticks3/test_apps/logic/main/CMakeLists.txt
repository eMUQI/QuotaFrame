idf_component_register(
    SRCS
        "test_main.cpp"
        "test_battery_view.cpp"
        "test_ota_ui.cpp"
        "test_page_state.cpp"
        "test_power_reading.cpp"
        "test_render_schedule.cpp"
        "../../../main/battery_view.cpp"
        "../../../main/button_controller.cpp"
        "../../../main/page_state.cpp"
        "../../../main/power_reading.cpp"
        "../../../main/render_schedule.cpp"
    INCLUDE_DIRS "../../../main"
    REQUIRES unity usage_core usage_ota
    WHOLE_ARCHIVE
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
