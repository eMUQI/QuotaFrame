cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../../../components/usage_json"
    "${CMAKE_CURRENT_LIST_DIR}/../../../../components/usage_core"
    "${CMAKE_CURRENT_LIST_DIR}/../../../../components/usage_protocol"
    "${CMAKE_CURRENT_LIST_DIR}/../../../../components/usage_ota"
    "${CMAKE_CURRENT_LIST_DIR}/../../../../components/usage_panel_state"
    "${CMAKE_CURRENT_LIST_DIR}/../../../../components/usage_rtc"
    "${CMAKE_CURRENT_LIST_DIR}/../../../../components/esp_desktop_buddy_folder_push"
)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(amoled_usage_panel_logic_tests)
