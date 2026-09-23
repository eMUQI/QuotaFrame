idf_component_register(
    SRCS
        "src/rtc_clock.cpp"
        "src/rtc_clock_codec.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c usage_protocol
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
