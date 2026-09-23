#include <cstring>

#include "esp_desktop_buddy/esp_desktop_buddy.h"
#include "esp_desktop_buddy/transport_port.h"
#include "unity.h"
#include "usage_ble/ble_service.hpp"

using namespace usage_panel;

namespace {

struct RouteContext {
    AppEventQueue* queue;
    bool enabled;
    bool ota_busy = false;
};

esp_desktop_buddy_command_extension_result_t route(
    void* context,
    esp_desktop_buddy_t*,
    const esp_desktop_buddy_command_view_t* request)
{
    const auto* route_context = static_cast<RouteContext*>(context);
    if (strcmp(esp_desktop_buddy_command_view_name(request), "screen_toggle") == 0) {
        return route_screen_toggle_command(
            request, route_context->enabled, route_context->ota_busy,
            route_context->queue);
    }
    if (strcmp(esp_desktop_buddy_command_view_name(request), "screen_page") == 0) {
        return route_screen_page_command(request, route_context->enabled,
            route_context->ota_busy, route_context->queue);
    }
    return route_time_sync_command(
        request, route_context->enabled, route_context->queue);
}

void feed_line(const char* line, RouteContext& context, char* reply, size_t reply_size)
{
    static const esp_desktop_buddy_command_extension_entry_t entries[] = {
        {"time_sync", route},
        {"screen_toggle", route},
        {"screen_page", route},
    };
    esp_desktop_buddy_config_t config{};
    config.handlers.command_extension.ctx = &context;
    config.handlers.command_extension.bindings = entries;
    config.handlers.command_extension.binding_count = 3;

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

constexpr char kValidTimeSync[] =
    "{\"cmd\":\"time_sync\",\"v\":\"1\",\"seq\":\"77\","
    "\"year\":\"2028\",\"month\":\"2\",\"day\":\"29\","
    "\"weekday\":\"2\",\"hour\":\"23\",\"minute\":\"58\","
    "\"second\":\"59\",\"utc_offset_min\":\"480\"}\n";

}  // namespace

TEST_CASE("enabled time sync command queues a validated event", "[ble_time_sync]")
{
    AppEventQueue queue;
    TEST_ASSERT_TRUE(queue.begin());
    RouteContext context{&queue, true};
    char reply[160]{};
    feed_line(kValidTimeSync, context, reply, sizeof(reply));

    AppEvent event{};
    TEST_ASSERT_TRUE(queue.receive(event));
    TEST_ASSERT_EQUAL(int(AppEventType::TimeSync), int(event.type));
    TEST_ASSERT_EQUAL_UINT32(77, event.time_sync.sequence);
    TEST_ASSERT_EQUAL_UINT16(2028, event.time_sync.calendar.year);
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"ok\":true"));
}

TEST_CASE("disabled time sync command is rejected without an event", "[ble_time_sync]")
{
    AppEventQueue queue;
    TEST_ASSERT_TRUE(queue.begin());
    RouteContext context{&queue, false};
    char reply[160]{};
    feed_line(kValidTimeSync, context, reply, sizeof(reply));

    AppEvent event{};
    TEST_ASSERT_FALSE(queue.receive(event));
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"ok\":false"));
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"error\":\"unsupported\""));
}

TEST_CASE("screen toggle queues exactly one event and rejects unsafe requests", "[ble_screen_toggle]")
{
    AppEventQueue queue;
    TEST_ASSERT_TRUE(queue.begin(1));
    RouteContext context{&queue, true};
    char reply[160]{};
    constexpr char valid[] = "{\"cmd\":\"screen_toggle\",\"v\":\"1\",\"seq\":\"9\"}\n";
    feed_line(valid, context, reply, sizeof(reply));
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"ok\":true"));
    feed_line(valid, context, reply, sizeof(reply));
    TEST_ASSERT_NOT_NULL(strstr(reply, "queue_full"));
    AppEvent event{};
    TEST_ASSERT_TRUE(queue.receive(event));
    TEST_ASSERT_EQUAL(int(AppEventType::ScreenToggle), int(event.type));
    TEST_ASSERT_FALSE(queue.receive(event));

    context.enabled = false;
    feed_line(valid, context, reply, sizeof(reply));
    TEST_ASSERT_NOT_NULL(strstr(reply, "unsupported"));
    context.enabled = true;
    context.ota_busy = true;
    feed_line(valid, context, reply, sizeof(reply));
    TEST_ASSERT_NOT_NULL(strstr(reply, "ota_busy"));
    context.ota_busy = false;
    const char* invalid[] = {
        "{\"cmd\":\"screen_toggle\",\"v\":\"2\",\"seq\":\"9\"}\n",
        "{\"cmd\":\"screen_toggle\",\"v\":\"1\",\"seq\":\"4294967296\"}\n",
        "{\"cmd\":\"screen_toggle\",\"v\":\"1\",\"seq\":\"09\"}\n",
        "{\"cmd\":\"screen_toggle\",\"v\":\"1\",\"seq\":\"-1\"}\n",
        "{\"cmd\":\"screen_toggle\",\"v\":\"1\"}\n",
    };
    for (const char* line : invalid) {
        feed_line(line, context, reply, sizeof(reply));
        TEST_ASSERT_NOT_NULL(strstr(reply, "invalid_request"));
    }
    TEST_ASSERT_FALSE(queue.receive(event));
}

TEST_CASE("page command validates direction and respects capability and OTA", "[ble_page]")
{
    AppEventQueue queue;
    TEST_ASSERT_TRUE(queue.begin());
    RouteContext context{&queue, true, false};
    char reply[256]{};
    const char* command = "{\"cmd\":\"screen_page\",\"v\":\"1\",\"seq\":\"2\",\"previous\":\"1\"}\n";
    feed_line(command, context, reply, sizeof(reply));
    AppEvent event{};
    TEST_ASSERT_TRUE(queue.receive(event));
    TEST_ASSERT_EQUAL(int(AppEventType::ScreenPage), int(event.type));
    TEST_ASSERT_TRUE(event.previous_page);
    context.ota_busy = true;
    feed_line(command, context, reply, sizeof(reply));
    TEST_ASSERT_NOT_NULL(strstr(reply, "ota_busy"));
    TEST_ASSERT_FALSE(queue.receive(event));
    context.ota_busy = false;
    context.enabled = false;
    feed_line(command, context, reply, sizeof(reply));
    TEST_ASSERT_NOT_NULL(strstr(reply, "unsupported"));
    context.enabled = true;
    feed_line("{\"cmd\":\"screen_page\",\"v\":\"1\",\"seq\":\"3\",\"previous\":\"2\"}\n", context, reply, sizeof(reply));
    TEST_ASSERT_NOT_NULL(strstr(reply, "invalid_request"));
    TEST_ASSERT_FALSE(queue.receive(event));
}
