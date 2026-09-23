idf_component_register(
    SRCS
        "test_main.cpp"
        "test_ota_ui.cpp"
        "test_page_state.cpp"
        "test_panel_state.cpp"
        "test_render_schedule.cpp"
        "test_screensaver_state.cpp"
        "test_screensaver_usage.cpp"
        "test_usage_bar_animation.cpp"
    INCLUDE_DIRS "."
    REQUIRES unity usage_core usage_ota usage_protocol usage_panel_state
    WHOLE_ARCHIVE
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
