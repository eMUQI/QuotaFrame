idf_component_register(
    SRCS
        "src/app_events.cpp"
        "src/ble_identity.cpp"
        "src/ble_service.cpp"
        "src/ota_commands.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES bt app_update esp_timer
    REQUIRES
        usage_json
        usage_core
        usage_protocol
        usage_ota
        esp_desktop_buddy
        esp_desktop_buddy_folder_push
        esp_desktop_buddy_transport_ble
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
