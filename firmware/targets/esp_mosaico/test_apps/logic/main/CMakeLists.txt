idf_component_register(
    SRCS
        "test_main.cpp"
        "test_local_clock.cpp"
        "test_orientation.cpp"
        "../../../main/local_clock.cpp"
        "../../../main/orientation.cpp"
    INCLUDE_DIRS "../../../main"
    REQUIRES unity usage_protocol usage_panel_state
    WHOLE_ARCHIVE
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
