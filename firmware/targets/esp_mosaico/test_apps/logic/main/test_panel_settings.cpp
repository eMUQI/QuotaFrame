#include "panel_settings.hpp"
#include "nvs.h"
#include "nvs_flash.h"
#include "unity.h"

#include <initializer_list>

using namespace usage_panel;
using namespace usage_panel::mosaico;

TEST_CASE("panel preferences persist together and reject invalid updates", "[settings]")
{
    TEST_ASSERT_EQUAL(ESP_OK, nvs_flash_init());
    for (bool camera : {false, true}) {
        const PanelSettings expected{{camera ? uint8_t{100} : uint8_t{1},
                                      camera ? 86400U : 0U}, camera};
        TEST_ASSERT_EQUAL(ESP_OK, save_panel_settings(expected));
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, save_panel_settings({{0, 60}, !camera}));
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, save_panel_settings({{101, 60}, !camera}));
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, save_panel_settings({{50, 86401}, !camera}));
        TEST_ASSERT_EQUAL(ESP_OK, nvs_flash_deinit());
        TEST_ASSERT_EQUAL(ESP_ERR_NVS_NOT_INITIALIZED, save_panel_settings({{50, 60}, !camera}));
        TEST_ASSERT_EQUAL(ESP_OK, nvs_flash_init());
        const auto actual = load_panel_settings({50, 300});
        TEST_ASSERT_EQUAL(expected.display.brightness, actual.display.brightness);
        TEST_ASSERT_EQUAL(expected.display.clock_timeout_seconds, actual.display.clock_timeout_seconds);
        TEST_ASSERT_EQUAL(expected.camera_enabled, actual.camera_enabled);
    }
}

TEST_CASE("missing or invalid panel records use defaults with capture off", "[settings]")
{
    TEST_ASSERT_EQUAL(ESP_OK, nvs_flash_init());
    nvs_handle_t handle;
    TEST_ASSERT_EQUAL(ESP_OK, nvs_open("mosaico", NVS_READWRITE, &handle));
    // Invalid brightness and timeout must reject the entire record, including capture enablement.
    for (uint32_t packed : {uint32_t{0x80}, uint32_t{(86401U << 8) | 0x80 | 50}}) {
        TEST_ASSERT_EQUAL(ESP_OK, nvs_set_u32(handle, "settings", packed));
        TEST_ASSERT_EQUAL(ESP_OK, nvs_commit(handle));
        const auto actual = load_panel_settings({40, 600});
        TEST_ASSERT_EQUAL(40, actual.display.brightness);
        TEST_ASSERT_EQUAL(600, actual.display.clock_timeout_seconds);
        TEST_ASSERT_FALSE(actual.camera_enabled);
    }
    TEST_ASSERT_EQUAL(ESP_OK, nvs_erase_key(handle, "settings"));
    TEST_ASSERT_EQUAL(ESP_OK, nvs_commit(handle));
    nvs_close(handle);
    const auto actual = load_panel_settings({30, 60});
    TEST_ASSERT_EQUAL(30, actual.display.brightness);
    TEST_ASSERT_EQUAL(60, actual.display.clock_timeout_seconds);
    TEST_ASSERT_FALSE(actual.camera_enabled);
}

TEST_CASE("display keys from earlier firmware apply until a panel record is saved", "[settings]")
{
    TEST_ASSERT_EQUAL(ESP_OK, nvs_flash_init());
    nvs_handle_t handle;
    TEST_ASSERT_EQUAL(ESP_OK, nvs_open("mosaico", NVS_READWRITE, &handle));
    const esp_err_t erased = nvs_erase_key(handle, "settings");
    TEST_ASSERT_TRUE(erased == ESP_OK || erased == ESP_ERR_NVS_NOT_FOUND);
    TEST_ASSERT_EQUAL(ESP_OK, nvs_commit(handle));
    nvs_close(handle);
    TEST_ASSERT_EQUAL(ESP_OK, save_display_settings({70, 600}));

    auto actual = load_panel_settings({40, 300});
    TEST_ASSERT_EQUAL(70, actual.display.brightness);
    TEST_ASSERT_EQUAL(600, actual.display.clock_timeout_seconds);
    TEST_ASSERT_FALSE(actual.camera_enabled);

    TEST_ASSERT_EQUAL(ESP_OK, save_panel_settings({{20, 60}, true}));
    actual = load_panel_settings({40, 300});
    TEST_ASSERT_EQUAL(20, actual.display.brightness);
    TEST_ASSERT_EQUAL(60, actual.display.clock_timeout_seconds);
    TEST_ASSERT_TRUE(actual.camera_enabled);

    TEST_ASSERT_EQUAL(ESP_OK, nvs_open("display", NVS_READWRITE, &handle));
    TEST_ASSERT_EQUAL(ESP_OK, nvs_erase_all(handle));
    TEST_ASSERT_EQUAL(ESP_OK, nvs_commit(handle));
    nvs_close(handle);
}
