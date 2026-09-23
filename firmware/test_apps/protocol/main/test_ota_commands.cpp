#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "esp_desktop_buddy/esp_desktop_buddy.h"
#include "esp_desktop_buddy/folder_push.h"
#include "esp_desktop_buddy/transport_port.h"
#include "fixture_data.hpp"
#include "mbedtls/base64.h"
#include "unity.h"
#include "usage_ble/ble_service.hpp"
#include "usage_ble/ota_commands.hpp"
#include "usage_ota/folder_push_sink.hpp"
#include "usage_ota/power_gate.hpp"
#include "usage_ota/session.hpp"

using namespace usage_panel;

namespace {

constexpr char kDigest[] =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

uint64_t fixed_now_ms()
{
    return 1234;
}

class FakeBackend final : public OtaBackend {
public:
    size_t capacity() const override { return 4096; }
    bool begin(size_t size) override
    {
        begun = true;
        begun_size = size;
        return true;
    }
    bool write(const uint8_t* data, size_t size) override
    {
        written.insert(written.end(), data, data + size);
        return true;
    }
    bool finish(const std::array<uint8_t, 32>&, const char*) override
    {
        finished = true;
        return true;
    }
    void abort() override { aborted = true; }

    bool begun = false;
    bool finished = false;
    bool aborted = false;
    size_t begun_size = 0;
    std::vector<uint8_t> written;
};

class FolderPushHarness {
public:
    explicit FolderPushHarness(const char* target = "m5sticks3", bool gate = false)
        : session(backend)
    {
        if (gate) {
            session.set_power_source(&power_source);
            power_source.publish(false, false, 10);
        }
        TEST_ASSERT_TRUE(sink.bind(session, target, fixed_now_ms));
        auto config = sink.config();
        TEST_ASSERT_EQUAL(
            ESP_OK, esp_desktop_buddy_folder_push_new(&config, &push));
    }

    ~FolderPushHarness() { destroy(); }

    void destroy()
    {
        esp_desktop_buddy_folder_push_delete(push);
        push = nullptr;
    }

    FakeBackend backend;
    OtaSession session;
    OtaFolderPushSink sink;
    AtomicPowerSource power_source;
    esp_desktop_buddy_folder_push_t* push = nullptr;
};

std::string manifest(const char* target = "m5sticks3")
{
    char output[384]{};
    const int written = snprintf(
        output, sizeof(output),
        "{\"schema\":\"1\",\"firmware_project\":\"quotaframe\",\"target\":\"%s\",\"version\":\"0.5.0\","
        "\"size\":4,\"sha256\":\"%s\"}",
        target, kDigest);
    return written > 0 && static_cast<size_t>(written) < sizeof(output)
        ? std::string(output, static_cast<size_t>(written))
        : std::string();
}

std::string base64(const uint8_t* data, size_t size)
{
    std::array<unsigned char, 1024> encoded{};
    size_t encoded_size = 0;
    if (mbedtls_base64_encode(
            encoded.data(), encoded.size(), &encoded_size, data, size) != 0) {
        return {};
    }
    return std::string(
        reinterpret_cast<const char*>(encoded.data()), encoded_size);
}

void assert_reply_ok(const esp_desktop_buddy_command_ack_t& reply, uint32_t n)
{
    TEST_ASSERT_TRUE(reply.ok);
    TEST_ASSERT_EQUAL_UINT32(n, reply.n);
}

void write_bytes(
    esp_desktop_buddy_folder_push_t* push,
    const uint8_t* data, size_t size, uint32_t expected_n)
{
    const std::string encoded = base64(data, size);
    TEST_ASSERT_FALSE(encoded.empty());
    esp_desktop_buddy_command_ack_t reply{};
    TEST_ASSERT_EQUAL(
        ESP_OK,
        esp_desktop_buddy_folder_push_write_chunk_b64(
            push, encoded.c_str(), &reply));
    assert_reply_ok(reply, expected_n);
}

void push_manifest(FolderPushHarness& harness, const std::string& content)
{
    esp_desktop_buddy_command_ack_t reply{};
    TEST_ASSERT_EQUAL(
        ESP_OK,
        esp_desktop_buddy_folder_push_start_transfer(
            harness.push, "firmware",
            static_cast<uint32_t>(content.size() + 4), &reply));
    assert_reply_ok(reply, 0);
    TEST_ASSERT_EQUAL(
        ESP_OK,
        esp_desktop_buddy_folder_push_start_file(
            harness.push, "manifest.json",
            static_cast<uint32_t>(content.size()), &reply));
    assert_reply_ok(reply, 0);
    write_bytes(
        harness.push, reinterpret_cast<const uint8_t*>(content.data()),
        content.size(), static_cast<uint32_t>(content.size()));
    TEST_ASSERT_EQUAL(
        ESP_OK, esp_desktop_buddy_folder_push_finish_file(harness.push, &reply));
    assert_reply_ok(reply, static_cast<uint32_t>(content.size()));
}

esp_desktop_buddy_command_extension_result_t abort_handler(
    void* context, esp_desktop_buddy_t*,
    const esp_desktop_buddy_command_view_t* request)
{
    return static_cast<OtaCommandRouter*>(context)->handle_abort(request);
}

esp_desktop_buddy_command_extension_result_t usage_handler(
    void* context, esp_desktop_buddy_t*,
    const esp_desktop_buddy_command_view_t* request)
{
    return static_cast<BleService*>(context)->handle_usage(request);
}

void feed_line(
    const char* line, void* context,
    const esp_desktop_buddy_command_extension_entry_t* entries,
    size_t entry_count, char* reply, size_t reply_size)
{
    esp_desktop_buddy_config_t config{};
    config.handlers.command_extension.ctx = context;
    config.handlers.command_extension.bindings = entries;
    config.handlers.command_extension.binding_count = entry_count;

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

}  // namespace

TEST_CASE("folder push manifest gates firmware until physical confirmation", "[ota_folder_push]")
{
    FolderPushHarness harness;
    const std::string content = manifest();

    push_manifest(harness, content);

    TEST_ASSERT_EQUAL(int(OtaPhase::Confirming), int(harness.session.snapshot().phase));
    TEST_ASSERT_FALSE(harness.backend.begun);

    esp_desktop_buddy_command_ack_t reply{};
    TEST_ASSERT_EQUAL(
        ESP_OK,
        esp_desktop_buddy_folder_push_start_file(
            harness.push, "firmware.bin", 4, &reply));
    TEST_ASSERT_FALSE(reply.ok);
    TEST_ASSERT_EQUAL_STRING("transfer_failed", reply.error);
    TEST_ASSERT_EQUAL(int(OtaPhase::Idle), int(harness.session.snapshot().phase));
}

TEST_CASE("folder push writes and verifies a confirmed firmware image", "[ota_folder_push]")
{
    FolderPushHarness harness;
    const std::string content = manifest();
    push_manifest(harness, content);
    TEST_ASSERT_TRUE(harness.session.confirm(2000));

    esp_desktop_buddy_command_ack_t reply{};
    TEST_ASSERT_EQUAL(
        ESP_OK,
        esp_desktop_buddy_folder_push_start_file(
            harness.push, "firmware.bin", 4, &reply));
    assert_reply_ok(reply, 0);
    const uint8_t image[] = {1, 2, 3, 4};
    write_bytes(harness.push, image, sizeof(image), 4);
    TEST_ASSERT_EQUAL(
        ESP_OK, esp_desktop_buddy_folder_push_finish_file(harness.push, &reply));
    assert_reply_ok(reply, 4);
    TEST_ASSERT_EQUAL(
        ESP_OK,
        esp_desktop_buddy_folder_push_finish_transfer(harness.push, &reply));
    assert_reply_ok(reply, 0);

    TEST_ASSERT_TRUE(harness.backend.begun);
    TEST_ASSERT_EQUAL_UINT32(4, harness.backend.begun_size);
    TEST_ASSERT_TRUE(harness.backend.finished);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(image, harness.backend.written.data(), sizeof(image));
    TEST_ASSERT_EQUAL(int(OtaPhase::Rebooting), int(harness.session.snapshot().phase));
}

TEST_CASE("folder push rejects a manifest for another project or target", "[ota_folder_push]")
{
    for (bool other_project : {false, true}) {
        FolderPushHarness harness;
        std::string content = manifest(other_project ? "m5sticks3" : "waveshare_amoled_216");
        if (other_project) content.replace(content.find("quotaframe"), 10, "other_app");
        esp_desktop_buddy_command_ack_t reply{};
        TEST_ASSERT_EQUAL(
            ESP_OK,
            esp_desktop_buddy_folder_push_start_transfer(
                harness.push, "firmware",
                static_cast<uint32_t>(content.size() + 4), &reply));
        TEST_ASSERT_TRUE(reply.ok);
        TEST_ASSERT_EQUAL(
            ESP_OK,
            esp_desktop_buddy_folder_push_start_file(
                harness.push, "manifest.json",
                static_cast<uint32_t>(content.size()), &reply));
        TEST_ASSERT_TRUE(reply.ok);
        write_bytes(
            harness.push, reinterpret_cast<const uint8_t*>(content.data()),
            content.size(), static_cast<uint32_t>(content.size()));

        TEST_ASSERT_EQUAL(
            ESP_OK, esp_desktop_buddy_folder_push_finish_file(harness.push, &reply));
        TEST_ASSERT_FALSE(reply.ok);
        TEST_ASSERT_EQUAL_STRING("transfer_failed", reply.error);
        TEST_ASSERT_EQUAL(int(OtaPhase::Idle), int(harness.session.snapshot().phase));
        TEST_ASSERT_EQUAL(int(other_project ? OtaError::ProjectMismatch : OtaError::TargetMismatch),
                          int(harness.session.snapshot().error));
        TEST_ASSERT_EQUAL(
            ESP_OK,
            esp_desktop_buddy_folder_push_start_transfer(
                harness.push, "firmware", 1, &reply));
        TEST_ASSERT_TRUE(reply.ok);
    }
}

TEST_CASE("folder push rejects a manifest while the power gate refuses", "[ota_folder_push]")
{
    FolderPushHarness harness("m5sticks3", true);
    const std::string content = manifest();
    esp_desktop_buddy_command_ack_t reply{};
    TEST_ASSERT_EQUAL(
        ESP_OK,
        esp_desktop_buddy_folder_push_start_transfer(
            harness.push, "firmware",
            static_cast<uint32_t>(content.size() + 4), &reply));
    TEST_ASSERT_TRUE(reply.ok);
    TEST_ASSERT_EQUAL(
        ESP_OK,
        esp_desktop_buddy_folder_push_start_file(
            harness.push, "manifest.json",
            static_cast<uint32_t>(content.size()), &reply));
    TEST_ASSERT_TRUE(reply.ok);
    write_bytes(
        harness.push, reinterpret_cast<const uint8_t*>(content.data()),
        content.size(), static_cast<uint32_t>(content.size()));

    TEST_ASSERT_EQUAL(
        ESP_OK, esp_desktop_buddy_folder_push_finish_file(harness.push, &reply));
    TEST_ASSERT_FALSE(reply.ok);
    // The transport layer collapses every sink failure into one token, so the
    // gate refusal is indistinguishable from a bad manifest on the wire; the
    // session-level state below is what actually records the refusal reason.
    TEST_ASSERT_EQUAL_STRING("transfer_failed", reply.error);

    const OtaSnapshot snap = harness.session.snapshot();
    TEST_ASSERT_EQUAL(int(OtaPhase::Idle), int(snap.phase));
    TEST_ASSERT_EQUAL(int(OtaError::LowPower), int(snap.error));
    TEST_ASSERT_FALSE(harness.backend.begun);
}

TEST_CASE("destroying an active folder push aborts the OTA backend", "[ota_folder_push]")
{
    FolderPushHarness harness;
    push_manifest(harness, manifest());
    TEST_ASSERT_TRUE(harness.session.confirm(2000));

    harness.destroy();

    TEST_ASSERT_TRUE(harness.backend.aborted);
    TEST_ASSERT_EQUAL(int(OtaPhase::Idle), int(harness.session.snapshot().phase));
}

TEST_CASE("ota_abort remains available for folder push cleanup", "[ota_commands]")
{
    FakeBackend backend;
    OtaSession session(backend);
    OtaCommandRouter router(session);
    TEST_ASSERT_TRUE(session.begin(4, kDigest, "0.5.0", 0));
    TEST_ASSERT_TRUE(session.confirm(1));
    static const esp_desktop_buddy_command_extension_entry_t entries[] = {
        {"ota_abort", abort_handler},
    };
    char reply[160]{};

    feed_line(
        "{\"cmd\":\"ota_abort\",\"v\":\"1\",\"seq\":\"11\"}\n",
        &router, entries, 1, reply, sizeof(reply));

    TEST_ASSERT_NOT_NULL(strstr(reply, "\"ok\":true"));
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"n\":11"));
    TEST_ASSERT_TRUE(backend.aborted);
    TEST_ASSERT_EQUAL(int(OtaPhase::Idle), int(session.snapshot().phase));
}

TEST_CASE("usage is rejected while folder push OTA is non-idle", "[ota_commands]")
{
    FakeBackend backend;
    OtaSession session(backend);
    BleService service(session);
    TEST_ASSERT_TRUE(session.begin(4, kDigest, "0.5.0", 0));
    static const esp_desktop_buddy_command_extension_entry_t entries[] = {
        {"usage", usage_handler},
    };
    char reply[160]{};

    feed_line(kUsageOk, &service, entries, 1, reply, sizeof(reply));

    TEST_ASSERT_NOT_NULL(strstr(reply, "\"ok\":false"));
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"n\":42"));
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"error\":\"ota_busy\""));
}
