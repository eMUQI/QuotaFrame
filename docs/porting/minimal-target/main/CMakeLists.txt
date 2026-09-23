idf_component_register(
    SRCS "app_main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        usage_core
        usage_protocol
        usage_ble
        usage_ota
)
