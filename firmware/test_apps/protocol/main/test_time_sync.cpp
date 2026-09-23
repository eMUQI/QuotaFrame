#include <cstring>

#include "esp_desktop_buddy/esp_desktop_buddy.h"
#include "esp_desktop_buddy/transport_port.h"
#include "unity.h"
#include "usage_protocol/time_sync.hpp"

using namespace usage_panel;

namespace {

struct Capture {
    unsigned accepted = 0;
    TimeSyncCommandParseResult parsed{};
};

esp_desktop_buddy_command_extension_result_t handle_time_sync(
    void* context,
    esp_desktop_buddy_t*,
    const esp_desktop_buddy_command_view_t* request)
{
    auto* capture = static_cast<Capture*>(context);
    const auto parsed = parse_time_sync_command(request);
    if (parsed.valid) {
        capture->accepted++;
        capture->parsed = parsed;
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
        {"time_sync", handle_time_sync},
    };
    esp_desktop_buddy_config_t config{};
    config.handlers.command_extension.ctx = &capture;
    config.handlers.command_extension.bindings = entries;
    config.handlers.command_extension.binding_count = 1;

    esp_desktop_buddy_t* buddy = nullptr;
    TEST_ASSERT_EQUAL(ESP_OK, esp_desktop_buddy_new(&config, &buddy));
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

bool accepts(const char* line)
{
    Capture capture{};
    char reply[160]{};
    feed_line(line, capture, reply, sizeof(reply));
    return capture.accepted == 1;
}

}  // namespace

TEST_CASE("valid leap-day time sync preserves calendar and offset", "[protocol]")
{
    Capture capture{};
    char reply[160]{};
    feed_line(
        "{\"cmd\":\"time_sync\",\"v\":\"1\",\"seq\":\"42\","
        "\"year\":\"2028\",\"month\":\"2\",\"day\":\"29\","
        "\"weekday\":\"2\",\"hour\":\"23\",\"minute\":\"58\","
        "\"second\":\"59\",\"utc_offset_min\":\"-480\"}\n",
        capture, reply, sizeof(reply));

    TEST_ASSERT_EQUAL_UINT32(1, capture.accepted);
    TEST_ASSERT_EQUAL_UINT32(42, capture.parsed.sequence);
    TEST_ASSERT_EQUAL_UINT16(2028, capture.parsed.calendar.year);
    TEST_ASSERT_EQUAL_UINT8(2, capture.parsed.calendar.month);
    TEST_ASSERT_EQUAL_UINT8(29, capture.parsed.calendar.day);
    TEST_ASSERT_EQUAL_UINT8(2, capture.parsed.calendar.weekday);
    TEST_ASSERT_EQUAL_UINT8(23, capture.parsed.calendar.hour);
    TEST_ASSERT_EQUAL_UINT8(58, capture.parsed.calendar.minute);
    TEST_ASSERT_EQUAL_UINT8(59, capture.parsed.calendar.second);
    TEST_ASSERT_EQUAL_INT16(-480, capture.parsed.calendar.utc_offset_minutes);
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"ack\":\"time_sync\""));
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"ok\":true"));
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"n\":42"));
}

TEST_CASE("time sync rejects invalid Gregorian and bounded fields", "[protocol]")
{
    TEST_ASSERT_FALSE(accepts(
        "{\"cmd\":\"time_sync\",\"v\":\"1\",\"seq\":\"1\","
        "\"year\":\"2027\",\"month\":\"2\",\"day\":\"29\","
        "\"weekday\":\"1\",\"hour\":\"12\",\"minute\":\"0\","
        "\"second\":\"0\",\"utc_offset_min\":\"0\"}\n"));
    TEST_ASSERT_FALSE(accepts(
        "{\"cmd\":\"time_sync\",\"v\":\"1\",\"seq\":\"2\","
        "\"year\":\"2100\",\"month\":\"1\",\"day\":\"1\","
        "\"weekday\":\"0\",\"hour\":\"0\",\"minute\":\"0\","
        "\"second\":\"0\",\"utc_offset_min\":\"0\"}\n"));
    TEST_ASSERT_FALSE(accepts(
        "{\"cmd\":\"time_sync\",\"v\":\"1\",\"seq\":\"3\","
        "\"year\":\"2028\",\"month\":\"13\",\"day\":\"1\","
        "\"weekday\":\"7\",\"hour\":\"24\",\"minute\":\"60\","
        "\"second\":\"60\",\"utc_offset_min\":\"841\"}\n"));
}

TEST_CASE("time sync rejects missing numeric and malformed signed fields", "[protocol]")
{
    TEST_ASSERT_FALSE(accepts(
        "{\"cmd\":\"time_sync\",\"v\":\"1\",\"seq\":\"4\","
        "\"year\":\"2028\",\"month\":\"2\",\"day\":\"29\","
        "\"weekday\":\"2\",\"hour\":\"23\",\"minute\":\"58\","
        "\"utc_offset_min\":\"0\"}\n"));
    TEST_ASSERT_FALSE(accepts(
        "{\"cmd\":\"time_sync\",\"v\":1,\"seq\":\"5\","
        "\"year\":\"2028\",\"month\":\"2\",\"day\":\"29\","
        "\"weekday\":\"2\",\"hour\":\"23\",\"minute\":\"58\","
        "\"second\":\"59\",\"utc_offset_min\":\"0\"}\n"));
    TEST_ASSERT_FALSE(accepts(
        "{\"cmd\":\"time_sync\",\"v\":\"1\",\"seq\":\"6\","
        "\"year\":\"2028\",\"month\":\"2\",\"day\":\"29\","
        "\"weekday\":\"2\",\"hour\":\"23\",\"minute\":\"58\","
        "\"second\":\"59\",\"utc_offset_min\":\"--60\"}\n"));
}
