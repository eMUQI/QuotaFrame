idf_component_register(
    SRCS
        "test_main.cpp"
        "test_panel_logic.cpp"
        "test_screens.cpp"
        "../../../main/canvas.cpp"
        "../../../main/panel_logic.cpp"
        "../../../main/screens.cpp"
    INCLUDE_DIRS "../../../main"
    REQUIRES unity usage_core
    WHOLE_ARCHIVE
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
