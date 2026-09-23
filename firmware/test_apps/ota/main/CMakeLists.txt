idf_component_register(
    SRCS
        "test_main.cpp"
        "test_ota_session.cpp"
        "test_ota_presentation.cpp"
    REQUIRES unity usage_ota
    WHOLE_ARCHIVE
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
