#include <cstring>

#include "esp_desktop_buddy/esp_desktop_buddy.h"
#include "esp_desktop_buddy/transport_port.h"
#include "fixture_data.hpp"
#include "unity.h"
#include "usage_protocol/usage_command.hpp"

using namespace usage_panel;

namespace {

struct Capture {
    unsigned accepted = 0;
    UsageUpdate update{};
};

esp_desktop_buddy_command_extension_result_t handle_usage(
    void* context,
    esp_desktop_buddy_t*,
    const esp_desktop_buddy_command_view_t* request)
{
    auto* capture = static_cast<Capture*>(context);
    const auto parsed = parse_usage_command(request);
    if (parsed.valid) {
        capture->accepted++;
        capture->update = parsed.update;
    }
    esp_desktop_buddy_command_extension_result_t result{};
    result.mode = ESP_DESKTOP_BUDDY_COMMAND_EXTENSION_ACK;
    result.reply.ok = parsed.valid;
    result.reply.n = parsed.sequence;
    result.reply.error = parsed.valid ? nullptr : "invalid_request";
    return result;
}

void feed_line(const char* line, Capture& capture, char* reply, size_t reply_size)
{
    static const esp_desktop_buddy_command_extension_entry_t entries[] = {
        {"usage", handle_usage},
    };
    esp_desktop_buddy_config_t config{};
    config.handlers.command_extension.ctx = &capture;
    config.handlers.command_extension.bindings = entries;
    config.handlers.command_extension.binding_count = 1;

    esp_desktop_buddy_t* buddy = nullptr;
    TEST_ASSERT_EQUAL(ESP_OK, esp_desktop_buddy_new(&config, &buddy));
    TEST_ASSERT_NOT_NULL(buddy);
    TEST_ASSERT_EQUAL(ESP_OK, esp_desktop_buddy_transport_port_attach(buddy));
    TEST_ASSERT_EQUAL(
        ESP_OK,
        esp_desktop_buddy_receive_bytes(
            buddy, reinterpret_cast<const uint8_t*>(line), strlen(line)));

    size_t reply_len = 0;
    TEST_ASSERT_EQUAL(
        ESP_OK,
        esp_desktop_buddy_transport_port_next_frame(
            buddy, reinterpret_cast<uint8_t*>(reply), reply_size - 1,
            &reply_len, pdMS_TO_TICKS(1000)));
    reply[reply_len] = '\0';

    esp_desktop_buddy_transport_port_detach(buddy);
    TEST_ASSERT_EQUAL(ESP_OK, esp_desktop_buddy_delete(buddy));
}

}  // namespace

TEST_CASE("valid usage command routes and acknowledges sequence", "[protocol]")
{
    Capture capture{};
    char reply[160]{};
    feed_line(kUsageOk, capture, reply, sizeof(reply));

    TEST_ASSERT_EQUAL_UINT32(1, capture.accepted);
    TEST_ASSERT_EQUAL(int(Provider::Codex), int(capture.update.provider));
    TEST_ASSERT_EQUAL_UINT8(36, capture.update.short_window.used_percent);
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"ack\":\"usage\""));
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"ok\":true"));
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"n\":42"));
}

TEST_CASE("invalid percentage is rejected without mutation", "[protocol]")
{
    Capture capture{};
    char reply[160]{};
    feed_line(kUsageInvalidPercent, capture, reply, sizeof(reply));

    TEST_ASSERT_EQUAL_UINT32(0, capture.accepted);
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"ok\":false"));
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"n\":45"));
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"error\":\"invalid_request\""));
}

TEST_CASE("JSON numeric fields are rejected instead of truncated", "[protocol]")
{
    Capture capture{};
    char reply[160]{};
    feed_line(kUsageInvalidNumeric, capture, reply, sizeof(reply));

    TEST_ASSERT_EQUAL_UINT32(0, capture.accepted);
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"ok\":false"));
}

TEST_CASE("reset without its usage percentage is rejected", "[protocol]")
{
    Capture capture{};
    char reply[160]{};
    feed_line(kUsageInvalidOrphanReset, capture, reply, sizeof(reply));

    TEST_ASSERT_EQUAL_UINT32(0, capture.accepted);
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"ok\":false"));
}


TEST_CASE("unregistered remote unpair is unsupported", "[protocol]")
{
    Capture capture{};
    char reply[160]{};
    feed_line("{\"cmd\":\"unpair\"}\n", capture, reply, sizeof(reply));
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"ack\":\"unpair\""));
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"ok\":false"));
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"error\":\"unsupported\""));
}
