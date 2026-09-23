set(FIXTURE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../protocol/examples")
set(PROTOCOL_FIXTURES
    "${FIXTURE_DIR}/usage-ok.jsonl"
    "${FIXTURE_DIR}/usage-invalid.jsonl"
    "${FIXTURE_DIR}/usage-invalid-numeric.jsonl"
    "${FIXTURE_DIR}/usage-invalid-orphan-reset.jsonl"
)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${PROTOCOL_FIXTURES})
file(READ "${FIXTURE_DIR}/usage-ok.jsonl" USAGE_OK_JSON)
file(READ "${FIXTURE_DIR}/usage-invalid.jsonl" USAGE_INVALID_PERCENT_JSON)
file(READ "${FIXTURE_DIR}/usage-invalid-numeric.jsonl" USAGE_INVALID_NUMERIC_JSON)
file(READ "${FIXTURE_DIR}/usage-invalid-orphan-reset.jsonl" USAGE_INVALID_ORPHAN_RESET_JSON)
string(STRIP "${USAGE_OK_JSON}" USAGE_OK_JSON)
string(STRIP "${USAGE_INVALID_PERCENT_JSON}" USAGE_INVALID_PERCENT_JSON)
string(STRIP "${USAGE_INVALID_NUMERIC_JSON}" USAGE_INVALID_NUMERIC_JSON)
string(STRIP "${USAGE_INVALID_ORPHAN_RESET_JSON}" USAGE_INVALID_ORPHAN_RESET_JSON)
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/fixture_data.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/fixture_data.hpp"
    @ONLY
)

idf_component_register(
    SRCS
        "test_main.cpp"
        "test_ble_identity.cpp"
        "test_ota_commands.cpp"
        "test_time_sync.cpp"
        "test_time_sync_ble.cpp"
        "test_usage_command.cpp"
        "test_usage_state.cpp"
    INCLUDE_DIRS "${CMAKE_CURRENT_BINARY_DIR}"
    REQUIRES
        unity
        esp_desktop_buddy
        esp_desktop_buddy_folder_push
        mbedtls
        usage_core
        usage_protocol
        usage_ble
        usage_ota
    WHOLE_ARCHIVE
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
