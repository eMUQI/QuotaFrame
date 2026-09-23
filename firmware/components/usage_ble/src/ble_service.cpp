#include "usage_ble/ble_service.hpp"

#include <cstdio>
#include <cstring>

#include "esp_app_desc.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "usage_ble/ble_identity.hpp"
#include "usage_protocol/time_sync.hpp"
#include "usage_protocol/usage_command.hpp"

namespace usage_panel {
namespace {

esp_desktop_buddy_command_extension_result_t ack(
    bool ok, uint32_t n, const char* error = nullptr)
{
    esp_desktop_buddy_command_extension_result_t result{};
    result.mode = ESP_DESKTOP_BUDDY_COMMAND_EXTENSION_ACK;
    result.reply.ok = ok;
    result.reply.n = n;
    result.reply.error = error;
    return result;
}

bool copy_identity(const char* source, char* output, size_t output_size)
{
    if (!source || source[0] == '\0' || !output || output_size == 0) return false;
    const int written = snprintf(output, output_size, "%s", source);
    return written >= 0 && static_cast<size_t>(written) < output_size;
}

}  // namespace

esp_desktop_buddy_command_extension_result_t route_time_sync_command(
    const esp_desktop_buddy_command_view_t* request,
    bool enabled,
    AppEventQueue* queue)
{
    const auto parsed = parse_time_sync_command(request);
    if (!enabled) return ack(false, parsed.sequence, "unsupported");
    if (!parsed.valid) return ack(false, parsed.sequence, "invalid_request");
    if (!queue || !queue->send_time_sync({parsed.sequence, parsed.calendar})) {
        return ack(false, parsed.sequence, "queue_full");
    }
    return ack(true, parsed.sequence);
}

esp_desktop_buddy_command_extension_result_t route_screen_toggle_command(
    const esp_desktop_buddy_command_view_t* request,
    bool enabled, bool ota_busy, AppEventQueue* queue)
{
    uint32_t sequence = 0;
    uint32_t version = 0;
    if (get_decimal_u32(request, "seq", sequence) != FieldResult::Valid) {
        return ack(false, 0, "invalid_request");
    }
    if (get_decimal_u32(request, "v", version) != FieldResult::Valid ||
        version != 1) {
        return ack(false, sequence, "invalid_request");
    }
    if (!enabled) return ack(false, sequence, "unsupported");
    if (ota_busy) return ack(false, sequence, "ota_busy");
    if (!queue || !queue->send_screen_toggle()) {
        return ack(false, sequence, "queue_full");
    }
    return ack(true, sequence);
}

esp_desktop_buddy_command_extension_result_t route_screen_page_command(
    const esp_desktop_buddy_command_view_t* request,
    bool enabled, bool ota_busy, AppEventQueue* queue)
{
    uint32_t sequence = 0, version = 0, previous = 0;
    if (get_decimal_u32(request, "seq", sequence) != FieldResult::Valid)
        return ack(false, 0, "invalid_request");
    if (get_decimal_u32(request, "v", version) != FieldResult::Valid || version != 1 ||
        get_decimal_u32(request, "previous", previous) != FieldResult::Valid || previous > 1)
        return ack(false, sequence, "invalid_request");
    if (!enabled) return ack(false, sequence, "unsupported");
    if (ota_busy) return ack(false, sequence, "ota_busy");
    if (!queue || !queue->send_screen_page(previous != 0))
        return ack(false, sequence, "queue_full");
    return ack(true, sequence);
}

esp_err_t BleService::start(AppEventQueue& queue, const BleServiceConfig& config)
{
    if (!copy_identity(
            config.advertising_prefix, advertising_prefix_,
            sizeof(advertising_prefix_)) ||
        !copy_identity(config.status_name, status_name_, sizeof(status_name_))) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!config.ota ||
        !copy_identity(config.target, target_, sizeof(target_))) {
        return ESP_ERR_INVALID_ARG;
    }
    startup_deadline_us_ = esp_timer_get_time() + 10000000;
    ota_ = config.ota;
    enable_time_sync_ = config.enable_time_sync;
    enable_screen_toggle_ = config.enable_screen_toggle;
    enable_screen_page_ = config.enable_screen_page;
    ota_commands_.bind(*ota_);
    const esp_app_desc_t* app = esp_app_get_description();
    if (!make_status_json(
            status_name_, false, "overview",
            app ? app->version : "unknown", target_, ota_->snapshot(),
            enable_time_sync_,
            status_json_, sizeof(status_json_), enable_screen_toggle_, boot_valid_.load(), enable_screen_page_)) {
        return ESP_ERR_INVALID_ARG;
    }

    queue_ = &queue;
    get_ui_location_ = config.get_ui_location;
    ui_context_ = config.ui_context;

    uint8_t mac[6]{};
    esp_read_mac(mac, ESP_MAC_BT);
    const uint16_t mac_suffix =
        static_cast<uint16_t>((uint16_t(mac[4]) << 8u) | uint16_t(mac[5]));
    if (!make_advertising_name(
            advertising_prefix_, mac_suffix,
            advertising_name_, sizeof(advertising_name_))) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ota_folder_sink_.bind(*ota_, target_) || !reset_folder_push()) {
        return ESP_ERR_NO_MEM;
    }

    static const esp_desktop_buddy_command_extension_entry_t entries[] = {
        {"usage", &BleService::usage_trampoline},
        {"time_sync", &BleService::time_sync_trampoline},
        {"screen_toggle", &BleService::screen_toggle_trampoline},
        {"screen_page", &BleService::screen_page_trampoline},
        {"ota_abort", &BleService::ota_abort_trampoline},
        {"char_begin", &BleService::folder_push_trampoline},
        {"file", &BleService::folder_push_trampoline},
        {"chunk", &BleService::folder_push_trampoline},
        {"file_end", &BleService::folder_push_trampoline},
        {"char_end", &BleService::folder_push_trampoline},
    };
    esp_desktop_buddy_config_t buddy_config{};
    buddy_config.handlers.ctx = this;
    buddy_config.handlers.on_status = &BleService::status_trampoline;
    buddy_config.handlers.command_extension.ctx = this;
    buddy_config.handlers.command_extension.bindings = entries;
    buddy_config.handlers.command_extension.binding_count =
        sizeof(entries) / sizeof(entries[0]);
    esp_err_t err = esp_desktop_buddy_new(&buddy_config, &buddy_);
    if (err != ESP_OK) {
        esp_desktop_buddy_folder_push_delete(folder_push_);
        folder_push_ = nullptr;
        return err;
    }

    esp_desktop_buddy_transport_ble_config_t transport_config{};
    transport_config.buddy = buddy_;
    transport_config.advertising_name_override = advertising_name_;
    transport_config.security.bonding = true;
    transport_config.security.mitm = true;
    transport_config.security.secure_connections = true;
    transport_config.security.io_capability =
        ESP_DESKTOP_BUDDY_TRANSPORT_BLE_IO_CAP_DISPLAY_ONLY;
    transport_config.on_event = &BleService::transport_trampoline;
    transport_config.event_ctx = this;
    err = esp_desktop_buddy_transport_ble_new(&transport_config, &transport_);
    if (err != ESP_OK) {
        esp_desktop_buddy_delete(buddy_);
        buddy_ = nullptr;
        esp_desktop_buddy_folder_push_delete(folder_push_);
        folder_push_ = nullptr;
    }
    return err;
}

void BleService::verify_boot()
{
    if (boot_valid_.load()) return;
    esp_desktop_buddy_transport_ble_state_t link{};
    if (transport_) esp_desktop_buddy_transport_ble_get_state(transport_, &link);
    if (!ble_hs_synced() || (!ble_gap_adv_active() && !link.connected)) {
        if (esp_timer_get_time() >= startup_deadline_us_) {
            ESP_LOGE("boot_check", "BLE startup timed out; restarting unconfirmed application");
            esp_restart();
        }
        return;
    }

    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    esp_err_t result = esp_ota_get_state_partition(running, &state);
    // A serial installation with erased otadata has no pending OTA to confirm.
    if (result == ESP_ERR_NOT_FOUND) {
        boot_valid_.store(true);
        return;
    }
    if (result == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        result = esp_ota_mark_app_valid_cancel_rollback();
    } else if (result == ESP_OK && state != ESP_OTA_IMG_VALID &&
               state != ESP_OTA_IMG_UNDEFINED) {
        result = ESP_ERR_INVALID_STATE;
    }
    if (result != ESP_OK) {
        ESP_LOGE("boot_check", "boot confirmation failed: %s", esp_err_to_name(result));
        esp_restart();
        return;
    }
    boot_valid_.store(true);
}

esp_desktop_buddy_command_extension_result_t BleService::handle_usage(
    const esp_desktop_buddy_command_view_t* request)
{
    const auto parsed = parse_usage_command(request);
    if (!parsed.valid) return ack(false, parsed.sequence, "invalid_request");
    if (ota_commands_.busy()) return ack(false, parsed.sequence, "ota_busy");
    if (!queue_ || !queue_->send_usage(parsed.update)) {
        return ack(false, parsed.sequence, "queue_full");
    }
    return ack(true, parsed.sequence);
}

esp_desktop_buddy_command_extension_result_t BleService::handle_ota_abort(
    const esp_desktop_buddy_command_view_t* request)
{
    auto result = ota_commands_.handle_abort(request);
    if (result.reply.ok && !reset_folder_push()) {
        return ack(false, result.reply.n, "folder_push_reset_failed");
    }
    return result;
}

esp_desktop_buddy_command_extension_result_t BleService::handle_time_sync(
    const esp_desktop_buddy_command_view_t* request)
{
    return route_time_sync_command(request, enable_time_sync_, queue_);
}

esp_desktop_buddy_status_reply_t BleService::status()
{
    esp_desktop_buddy_transport_ble_state_t state{};
    if (transport_) esp_desktop_buddy_transport_ble_get_state(transport_, &state);
    const char* location =
        get_ui_location_ ? get_ui_location_(ui_context_) : nullptr;
    const esp_app_desc_t* app = esp_app_get_description();
    if (!make_status_json(
            status_name_, state.encrypted, location,
            app ? app->version : "unknown", target_,
            ota_ ? ota_->snapshot() : OtaSnapshot{}, enable_time_sync_,
            status_json_, sizeof(status_json_), enable_screen_toggle_, boot_valid_.load(), enable_screen_page_)) {
        return esp_desktop_buddy_status_err(
            ESP_ERR_INVALID_ARG, "status_invalid");
    }
    esp_desktop_buddy_json_object_view_t view{
        reinterpret_cast<const uint8_t*>(status_json_), strlen(status_json_)};
    return esp_desktop_buddy_status_ok(view);
}

esp_desktop_buddy_command_extension_result_t BleService::ota_abort_trampoline(
    void* context, esp_desktop_buddy_t*,
    const esp_desktop_buddy_command_view_t* request)
{
    return static_cast<BleService*>(context)->handle_ota_abort(request);
}

esp_desktop_buddy_command_extension_result_t BleService::screen_page_trampoline(
    void* context, esp_desktop_buddy_t*, const esp_desktop_buddy_command_view_t* request)
{
    auto* self = static_cast<BleService*>(context);
    return route_screen_page_command(
        request, self->enable_screen_page_, self->ota_commands_.busy(), self->queue_);
}

esp_desktop_buddy_command_extension_result_t BleService::screen_toggle_trampoline(
    void* context, esp_desktop_buddy_t*,
    const esp_desktop_buddy_command_view_t* request)
{
    auto* self = static_cast<BleService*>(context);
    return route_screen_toggle_command(
        request, self->enable_screen_toggle_, self->ota_commands_.busy(), self->queue_);
}

esp_desktop_buddy_command_extension_result_t BleService::time_sync_trampoline(
    void* context, esp_desktop_buddy_t*,
    const esp_desktop_buddy_command_view_t* request)
{
    return static_cast<BleService*>(context)->handle_time_sync(request);
}

esp_desktop_buddy_command_extension_result_t BleService::folder_push_trampoline(
    void* context, esp_desktop_buddy_t* buddy,
    const esp_desktop_buddy_command_view_t* request)
{
    auto* self = static_cast<BleService*>(context);
    if (!self || !self->folder_push_ || !request) {
        return ack(false, 0, "folder_push_unavailable");
    }
    const char* command = esp_desktop_buddy_command_view_name(request);
    const auto extension =
        esp_desktop_buddy_folder_push_command_extension(self->folder_push_);
    for (size_t index = 0; index < extension.binding_count; ++index) {
        const auto& binding = extension.bindings[index];
        if (command && binding.command && strcmp(command, binding.command) == 0) {
            return binding.handler(extension.ctx, buddy, request);
        }
    }
    return ack(false, 0, "folder_push_unavailable");
}

bool BleService::reset_folder_push()
{
    esp_desktop_buddy_folder_push_delete(folder_push_);
    folder_push_ = nullptr;
    auto config = ota_folder_sink_.config();
    return esp_desktop_buddy_folder_push_new(&config, &folder_push_) == ESP_OK;
}

esp_desktop_buddy_command_result_t BleService::reset_pairing()
{
    if (!transport_) {
        return esp_desktop_buddy_command_err(
            ESP_ERR_INVALID_STATE, "transport_missing");
    }
    // esp-desktop-buddy exposes a clear-all-bonds primitive here; the reset
    // stays device-wide because no multi-host model defines peer ownership.
    const esp_err_t err =
        esp_desktop_buddy_transport_ble_clear_bonds(transport_);
    return err == ESP_OK
        ? esp_desktop_buddy_command_ok()
        : esp_desktop_buddy_command_err(err, "bond_clear_failed");
}

esp_desktop_buddy_command_extension_result_t BleService::usage_trampoline(
    void* context, esp_desktop_buddy_t*,
    const esp_desktop_buddy_command_view_t* request)
{
    return static_cast<BleService*>(context)->handle_usage(request);
}

esp_desktop_buddy_status_reply_t BleService::status_trampoline(
    void* context, esp_desktop_buddy_t*)
{
    return static_cast<BleService*>(context)->status();
}

void BleService::transport_trampoline(
    void* context, const esp_desktop_buddy_transport_ble_event_t* event)
{
    auto* self = static_cast<BleService*>(context);
    if (!self || !self->queue_ || !event) return;
    if (!event->state.connected && self->ota_) self->ota_->link_lost();
    self->queue_->send_link({
        event->state.connected, event->state.encrypted,
        event->state.has_passkey, event->state.passkey});
}

}  // namespace usage_panel
