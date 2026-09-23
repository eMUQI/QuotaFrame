idf_component_register(
    SRCS
        "src/session.cpp"
        "src/esp_ota_backend.cpp"
        "src/folder_push_sink.cpp"
        "src/presentation.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        usage_json
        app_update
        esp_desktop_buddy_folder_push
        esp_timer
        espressif__cjson
        mbedtls
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
