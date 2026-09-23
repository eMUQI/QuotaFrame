idf_component_register(
    SRCS
        "src/time_sync.cpp"
        "src/buddy_input_guard.c"
        "src/usage_command.cpp"
    INCLUDE_DIRS "include"
    REQUIRES usage_core esp_desktop_buddy usage_json
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=buddy_protocol_process_line" "-Wl,-u,__wrap_buddy_protocol_process_line")

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    idf_component_get_property(buddy_dir esp_desktop_buddy COMPONENT_DIR)
    foreach(pin IN ITEMS
        "private_include/buddy_internal.h=b98ec31a01a7614231adb1e978eda7ded229772e9e786dc3c8871dcd105612f8"
        "src/buddy_protocol.c=ddff5f7e6a9f2836539265aef45cdd65a220016368252f9f6b5e59b252cb1990"
        "src/buddy_linebuf.c=474ad70d494bdb5ebd9717abc3f8c041be478459563a4934236c9a374c5af2a9"
    )
        string(REPLACE "=" ";" pin_parts "${pin}")
        list(GET pin_parts 0 buddy_path)
        list(GET pin_parts 1 expected_hash)
        file(READ "${buddy_dir}/${buddy_path}" buddy_text)
        string(REPLACE "\r\n" "\n" buddy_text "${buddy_text}")
        string(SHA256 buddy_hash "${buddy_text}")
        if(NOT buddy_hash STREQUAL expected_hash)
            message(FATAL_ERROR "Buddy input guard requires reviewed pinned source: ${buddy_path}")
        endif()
    endforeach()
    target_include_directories(${COMPONENT_LIB} PRIVATE "${buddy_dir}/private_include")
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=buddy_linebuf_feed" "-Wl,-u,__wrap_buddy_linebuf_feed")
endif()
