#include <array>
#include <cstring>

#include "unity.h"
#include "usage_json.h"
#include "usage_ble/ble_identity.hpp"
#include "usage_ble/ble_service.hpp"
#include "usage_ota/session.hpp"

using namespace usage_panel;

namespace {

class FakeBackend final : public OtaBackend {
public:
    size_t capacity() const override { return 4096; }
    bool begin(size_t) override { return true; }
    bool write(const uint8_t*, size_t) override { return true; }
    bool finish(const std::array<uint8_t, 32>&, const char*) override
    {
        return true;
    }
    void abort() override {}
};

}  // namespace

TEST_CASE("M5 BLE identity advertises exact OTA status fields", "[ble_identity]")
{
    char advertising[24]{};
    TEST_ASSERT_TRUE(make_advertising_name(
        "QF-M5-", 0xA1B2, advertising, sizeof(advertising)));
    TEST_ASSERT_EQUAL_STRING("QF-M5-A1B2", advertising);

    char status[1024]{};
    OtaSnapshot ota{};
    TEST_ASSERT_TRUE(make_status_json(
        "M5 Usage Panel", true, "codex", "0.5.0", "m5sticks3",
        ota, false, status, sizeof(status)));
    TEST_ASSERT_EQUAL_STRING(
        "{\"name\":\"M5 Usage Panel\",\"sec\":true,\"protocol\":1,"
        "\"page\":\"codex\",\"caps\":[\"usage.v1\",\"ota.folder.v1\"],\"firmware_project\":\"quotaframe\","
        "\"fw\":\"0.5.0\",\"target\":\"m5sticks3\",\"boot_valid\":false,"
        "\"ota\":{\"phase\":\"idle\",\"off\":\"0\","
        "\"size\":\"0\",\"err\":\"\"}}",
        status);

    char short_output[8]{};
    TEST_ASSERT_FALSE(make_advertising_name(
        "QF-M5-", 0xA1B2, short_output, sizeof(short_output)));
    TEST_ASSERT_FALSE(make_status_json(
        "M5 Usage Panel", true, "codex", "0.5.0", "m5sticks3",
        ota, false, short_output, sizeof(short_output)));
}

TEST_CASE("Waveshare BLE identity uses the exact OTA target", "[ble_identity]")
{
    char advertising[24]{};
    TEST_ASSERT_TRUE(make_advertising_name(
        "QF-WS-S3-A216-", 0xA1B2, advertising, sizeof(advertising)));
    TEST_ASSERT_EQUAL_STRING("QF-WS-S3-A216-A1B2", advertising);

    char status[1024]{};
    OtaSnapshot ota{};
    TEST_ASSERT_TRUE(make_status_json(
        "Waveshare Usage Panel", true, "claude", "0.5.0",
        "waveshare_amoled_216", ota, true, status, sizeof(status), true));
    TEST_ASSERT_NOT_NULL(strstr(
        status, "\"target\":\"waveshare_amoled_216\""));
    TEST_ASSERT_NOT_NULL(strstr(
        status, "\"caps\":[\"usage.v1\",\"ota.folder.v1\",\"time.sync.v1\",\"screen.toggle.v1\"]"));
}

TEST_CASE("BLE status reports active OTA progress and error", "[ble_identity]")
{
    char status[1024]{};
    OtaSnapshot ota{};
    ota.phase = OtaPhase::Receiving;
    ota.error = OtaError::LinkLost;
    ota.offset = 129600;
    ota.size = 777216;

    TEST_ASSERT_TRUE(make_status_json(
        "M5 Usage Panel", true, "overview", "0.5.0", "m5sticks3",
        ota, false, status, sizeof(status)));

    TEST_ASSERT_NOT_NULL(strstr(status, "\"phase\":\"receiving\""));
    TEST_ASSERT_NOT_NULL(strstr(status, "\"off\":\"129600\""));
    TEST_ASSERT_NOT_NULL(strstr(status, "\"size\":\"777216\""));
    TEST_ASSERT_NOT_NULL(strstr(status, "\"err\":\"link_lost\""));
}

TEST_CASE("BLE status exposes low_power as the OTA error word", "[ble_identity]")
{
    char status[1024]{};
    OtaSnapshot ota{};
    ota.error = OtaError::LowPower;

    TEST_ASSERT_TRUE(make_status_json(
        "M5 Usage Panel", true, "codex", "0.5.0", "m5sticks3",
        ota, false, status, sizeof(status)));
    TEST_ASSERT_NOT_NULL(strstr(status, "\"err\":\"low_power\""));
}

TEST_CASE("BLE status omits page when UI location is absent", "[ble_identity]")
{
    char status[1024]{};
    OtaSnapshot ota{};
    TEST_ASSERT_TRUE(make_status_json(
        "M5 Usage Panel", false, nullptr, "0.5.0", "m5sticks3",
        ota, false, status, sizeof(status)));
    TEST_ASSERT_NULL(strstr(status, "\"page\""));
    TEST_ASSERT_NOT_NULL(strstr(status, "\"sec\":false"));
}

TEST_CASE("BLE status rejects unsafe public JSON strings", "[ble_identity]")
{
    char status[1024]{};
    OtaSnapshot ota{};
    TEST_ASSERT_TRUE(make_status_json(
        "M5 \"Usage\" Panel", true, "codex", "0.5.0", "m5sticks3",
        ota, false, status, sizeof(status)));
    TEST_ASSERT_FALSE(make_status_json(
        "M5 Usage Panel", true, "co\\dex", "0.5.0", "m5sticks3",
        ota, false, status, sizeof(status)));
    TEST_ASSERT_FALSE(make_status_json(
        "M5 Usage Panel", true, "codex", "0.5.0", "bad\"target",
        ota, false, status, sizeof(status)));

    const char control_name[] = {'M', '5', '\n', '\0'};
    TEST_ASSERT_FALSE(make_status_json(
        control_name, true, "codex", "0.5.0", "m5sticks3",
        ota, false, status, sizeof(status)));
}

TEST_CASE("BLE service rejects overlong configured identity", "[ble_identity]")
{
    FakeBackend backend;
    OtaSession ota(backend);
    AppEventQueue queue;
    BleService service;
    BleServiceConfig config{
        .advertising_prefix = "12345678901234567890",
        .status_name = "M5 Usage Panel",
        .target = "m5sticks3",
        .enable_time_sync = false,
        .ota = &ota,
        .get_ui_location = nullptr,
        .ui_context = nullptr,
    };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, service.start(queue, config));

    const char long_status_name[] =
        "123456789012345678901234567890123456789012345678901234567890123456789";
    config.advertising_prefix = "QF-M5-";
    config.status_name = long_status_name;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, service.start(queue, config));

    config.status_name = "Invalid\nName";
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, service.start(queue, config));
}

TEST_CASE("strict JSON rejects ambiguous and lossy inputs", "[ble_identity]")
{
    const char* invalid[] = {
        "{\"cmd\":\"status\",\"cmd\":\"unpair\"}",
        "{\"nested\":{\"a\":1,\"\\u0061\":2}}",
        "{\"size\":-1}", "{\"size\":1.0}", "{\"size\":1e0}",
        "{\"size\":4294967296}", "{\"size\":01}",
        "{\"name\":\"\\u0000\"}", "{\"name\":\"\xff\"}", "{}trailing"
    };
    for (const char* text : invalid) {
        cJSON* root = usage_json_parse(text, strlen(text));
        TEST_ASSERT_NULL(root);
        cJSON_Delete(root);
    }
    const char* text = "{\"cmd\":\"file\",\"size\":4294967295}";
    cJSON* root = usage_json_parse(text, strlen(text));
    TEST_ASSERT_NOT_NULL(root);
    cJSON_Delete(root);
}

TEST_CASE("public names and versions preserve strict bounds", "[ble_identity]")
{
    TEST_ASSERT_TRUE(usage_name_valid("书桌 \\\"额度屏"));
    TEST_ASSERT_FALSE(usage_name_valid(" name"));
    TEST_ASSERT_FALSE(usage_name_valid("name\n"));
    TEST_ASSERT_FALSE(usage_name_valid("\xed\xa0\x80"));
    TEST_ASSERT_TRUE(usage_version_valid("1.2.3-rc.1+build"));
    for (const char* text : {"v1.2.3", "01.2.3", "1.2.3-01", "1.2.3+", "1.2"})
        TEST_ASSERT_FALSE(usage_version_valid(text));
}


TEST_CASE("BLE advertising prefixes use the bounded public namespace", "[ble_identity]")
{
    char output[24]{};
    TEST_ASSERT_TRUE(make_advertising_name("QF-123456789012", 0x1234, output, sizeof(output)));
    TEST_ASSERT_FALSE(make_advertising_name("QF-1234567890123", 0x1234, output, sizeof(output)));
    TEST_ASSERT_FALSE(make_advertising_name("Other-", 0x1234, output, sizeof(output)));
    TEST_ASSERT_FALSE(make_advertising_name("QF- ", 0x1234, output, sizeof(output)));
}
