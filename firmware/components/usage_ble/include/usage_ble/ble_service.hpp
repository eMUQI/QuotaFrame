#pragma once

#include <atomic>

#include "esp_desktop_buddy/esp_desktop_buddy.h"
#include "esp_desktop_buddy/folder_push.h"
#include "esp_desktop_buddy/transport_ble.h"
#include "usage_ble/app_events.hpp"
#include "usage_ble/ota_commands.hpp"
#include "usage_ota/folder_push_sink.hpp"
#include "usage_ota/session.hpp"

namespace usage_panel {

/** Returns the current public UI location, such as overview/codex/claude. */
using UiLocationGetter = const char* (*)(void*);

/** Configuration copied or retained by BleService::start(). */
struct BleServiceConfig {
    // Identity strings are copied into fixed service-owned buffers during start.
    const char* advertising_prefix;
    const char* status_name;
    const char* target;
    bool enable_time_sync;
    // The OTA session, callback, and callback context must outlive BleService.
    OtaSession* ota;
    UiLocationGetter get_ui_location;
    void* ui_context;
    bool enable_screen_toggle = false;
    bool enable_screen_page = false;
};

/**
 * Validates and queues a time-sync command, returning a protocol ACK.
 * Disabled support is reported as `unsupported`; a full application queue is
 * reported as `queue_full` rather than blocking the BLE callback.
 */
esp_desktop_buddy_command_extension_result_t route_time_sync_command(
    const esp_desktop_buddy_command_view_t* request,
    bool enabled,
    AppEventQueue* queue);

/** Validates a screen toggle and queues it for the application loop. */
esp_desktop_buddy_command_extension_result_t route_screen_toggle_command(
    const esp_desktop_buddy_command_view_t* request,
    bool enabled, bool ota_busy, AppEventQueue* queue);

/** Queues a single relative page change; OTA prevents navigation. */
esp_desktop_buddy_command_extension_result_t route_screen_page_command(
    const esp_desktop_buddy_command_view_t* request,
    bool enabled, bool ota_busy, AppEventQueue* queue);

/**
 * Owns the authenticated BLE transport and routes Buddy commands to app state.
 *
 * The service requires bonding, MITM protection, Secure Connections, and a
 * DisplayOnly passkey ceremony. A link is usable only when both `connected`
 * and `encrypted` are true.
 */
class BleService {
public:
    BleService() = default;
    explicit BleService(OtaSession& ota) : ota_(&ota), ota_commands_(ota) {}

    /** Creates the Buddy core, Folder Push adapter, and secure BLE transport. */
    esp_err_t start(AppEventQueue& queue, const BleServiceConfig& config);

    /** Call from the app loop after display initialization and the first render. */
    void verify_boot();

    /** Handles one validated usage command unless an OTA session is active. */
    esp_desktop_buddy_command_extension_result_t handle_usage(
        const esp_desktop_buddy_command_view_t* request);

    /** Aborts OTA state and rebuilds Folder Push protocol state on success. */
    esp_desktop_buddy_command_extension_result_t handle_ota_abort(
        const esp_desktop_buddy_command_view_t* request);

    esp_desktop_buddy_command_extension_result_t handle_time_sync(
        const esp_desktop_buddy_command_view_t* request);

    /** Returns current public capability/status data, including encryption state. */
    esp_desktop_buddy_status_reply_t status();

    /** Clears all stored BLE bonds for a device-local recovery action. */
    esp_desktop_buddy_command_result_t reset_pairing();

private:
    static esp_desktop_buddy_command_extension_result_t usage_trampoline(
        void*, esp_desktop_buddy_t*, const esp_desktop_buddy_command_view_t*);
    static esp_desktop_buddy_command_extension_result_t ota_abort_trampoline(
        void*, esp_desktop_buddy_t*, const esp_desktop_buddy_command_view_t*);
    static esp_desktop_buddy_command_extension_result_t screen_toggle_trampoline(
        void*, esp_desktop_buddy_t*, const esp_desktop_buddy_command_view_t*);
    static esp_desktop_buddy_command_extension_result_t screen_page_trampoline(
        void*, esp_desktop_buddy_t*, const esp_desktop_buddy_command_view_t*);
    static esp_desktop_buddy_command_extension_result_t time_sync_trampoline(
        void*, esp_desktop_buddy_t*, const esp_desktop_buddy_command_view_t*);
    static esp_desktop_buddy_command_extension_result_t folder_push_trampoline(
        void*, esp_desktop_buddy_t*, const esp_desktop_buddy_command_view_t*);
    static esp_desktop_buddy_status_reply_t status_trampoline(
        void*, esp_desktop_buddy_t*);
    static void transport_trampoline(
        void*, const esp_desktop_buddy_transport_ble_event_t*);
    bool reset_folder_push();

    AppEventQueue* queue_ = nullptr;
    UiLocationGetter get_ui_location_ = nullptr;
    void* ui_context_ = nullptr;
    esp_desktop_buddy_t* buddy_ = nullptr;
    esp_desktop_buddy_transport_ble_t* transport_ = nullptr;
    esp_desktop_buddy_folder_push_t* folder_push_ = nullptr;
    char advertising_prefix_[20]{};
    char advertising_name_[24]{};
    char status_name_[129]{};
    char target_[32]{};
    char status_json_[1024]{};
    std::atomic<bool> boot_valid_{false};
    int64_t startup_deadline_us_ = 0;
    bool enable_time_sync_ = false;
    bool enable_screen_toggle_ = false;
    bool enable_screen_page_ = false;
    OtaSession* ota_ = nullptr;
    OtaCommandRouter ota_commands_{};
    OtaFolderPushSink ota_folder_sink_{};
};

}  // namespace usage_panel
