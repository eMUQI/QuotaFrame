idf_component_register(
    SRCS
        "display_settings.cpp"
        "page_state.cpp"
        "panel_state.cpp"
        "render_schedule.cpp"
        "screensaver_state.cpp"
        "screensaver_usage.cpp"
        "usage_bar_animation.cpp"
    INCLUDE_DIRS "include"
    REQUIRES usage_core usage_ota usage_protocol nvs_flash lvgl__lvgl
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
