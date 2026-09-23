#include "M5Unified.h"
#include "usage_ble/app_events.hpp"
#include "usage_ble/ble_service.hpp"
#include "usage_ota/backend.hpp"
#include "usage_ota/presentation.hpp"
#include "usage_ota/session.hpp"
#include "battery_view.hpp"
#include "button_controller.hpp"
#include "display_ui.hpp"
#include "power_reading.hpp"
#include "render_schedule.hpp"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

using namespace usage_panel;
namespace {
const char* TAG = "usage_panel";
AppEventQueue events;
UsageModel model;
EspOtaBackend ota_backend("m5_usage_panel");
OtaSession ota(ota_backend);
AtomicPowerSource ota_power_source;
// TODO: Validate the polling interval against measured StickS3 power telemetry.
constexpr uint64_t POWER_POLL_PERIOD_MS = 1000;
usage_panel::m5::BatteryStateFilter battery_filter;
usage_panel::m5::BatteryView latest_battery;
BleService ble;
Page page = Page::Overview;
LinkUpdate ble_link{};
const char* get_ui_location(void*) { return page_name(page); }

// The advertising name buffer holds 19 characters plus the terminator, and
// the service appends the four-digit MAC suffix. An empty prefix is rejected
// by the BLE service at runtime, so it must not reach a bootable image.
static_assert(sizeof(CONFIG_M5_USAGE_PANEL_BLE_ADVERTISING_PREFIX) >= 2 &&
                  sizeof(CONFIG_M5_USAGE_PANEL_BLE_ADVERTISING_PREFIX) - 1 <= 15,
              "BLE advertising prefix must be non-empty and, plus the 4-digit "
              "MAC suffix, fit the 20-byte name buffer");

const BleServiceConfig ble_config{
    .advertising_prefix = CONFIG_M5_USAGE_PANEL_BLE_ADVERTISING_PREFIX,
    .status_name = "M5 Usage Panel",
    .target = "m5sticks3",
    .enable_time_sync = false,
    .ota = &ota,
    .get_ui_location = get_ui_location,
    .ui_context = nullptr,
    .enable_screen_page = true,
};
}

extern "C" void app_main(void)
{
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs);

    auto config = M5.config();
    config.clear_display = true;
    // This panel never drives the Grove/HAT 5 V rail, so the PM1 boost stays
    // off. M5Unified's output_power default would enable it (PWR_CFG bit 3,
    // which the PMIC's own reset default leaves clear), running a boost
    // converter off the cell for as long as the board is powered. The PM1's
    // charge current is fixed and not configurable, so it has no headroom to
    // absorb that drain when the input supply is marginal.
    config.output_power = false;
    M5.begin(config);
    if (M5.getBoard() != ::m5::board_t::board_M5StickS3) {
        ESP_LOGW(TAG, "board detected as %d, expected M5StickS3", int(M5.getBoard()));
    }
    // Power_Class::begin() leaves the M5PM1 charger disabled on this board:
    // it only sets up the CHG status pin as input and never sets
    // M5PM1_PWR_CFG_CHG_EN, so without this the battery drains even while
    // USB-powered.
    M5.Power.setBatteryCharge(true);
    static DisplayUi ui;
    ui.begin();
    ESP_ERROR_CHECK(events.begin() ? ESP_OK : ESP_ERR_NO_MEM);
    ota.set_power_source(&ota_power_source);
    ESP_ERROR_CHECK(ble.start(events, ble_config));

    bool dirty = true;
    bool has_last_render_key = false;
    static TimedRenderKey last_render_key{};
    uint64_t last_checked_second = UINT64_MAX;
    static OtaSnapshot last_ota{};
    static OtaSnapshot ota_view{};
    RetainedOtaFailure shown_failure{};
    uint64_t reboot_at_ms = 0;
    uint64_t next_power_poll_ms = 0;

    while (true) {
        M5.update();
        const uint64_t now_ms = esp_timer_get_time() / 1000;
        if (now_ms >= next_power_poll_ms) {
            next_power_poll_ms = now_ms + POWER_POLL_PERIOD_MS;
            const int level = M5.Power.getBatteryLevel();
            // isCharging() is tri-state; a bare bool conversion would read
            // charge_unknown as powered, so require confirmed charge.
            const bool charging =
                M5.Power.isCharging() == ::m5::Power_Class::is_charging_t::is_charging;
            latest_battery = battery_filter.update(
                level, charging, M5.Power.getVBUSVoltage());
            const OtaPowerReading reading =
                usage_panel::m5::derive_power_reading(latest_battery);
            ota_power_source.publish(
                reading.valid, reading.external_power, reading.percent);
        }
        ota.tick(now_ms);
        ota_view = ota.snapshot();
        if (retain_ota_failure(ota_view, shown_failure, now_ms)) {
            dirty = true;
        }
        if (M5.BtnA.wasClicked()) {
            const auto action = route_front_button(ota_view.phase);
            if (action == FrontButtonAction::Confirm) ota.confirm(now_ms);
            else if (action == FrontButtonAction::Navigate) page = handle_front_button(page);
            ota_view = ota.snapshot();
            dirty = true;
        }
        if (M5.BtnB.wasClicked() &&
            route_side_button(ota_view.phase) == SideButtonAction::Deny) {
            ota.deny();
            ota_view = ota.snapshot();
            dirty = true;
        }
        AppEvent event{};
        while (events.receive(event)) {
            if (event.type == AppEventType::Usage) model.apply(event.usage, esp_timer_get_time() / 1000);
            else if (event.type == AppEventType::Link) ble_link = event.link;
            else if (event.type == AppEventType::ScreenPage &&
                     ota_view.phase == OtaPhase::Idle && !ble_link.has_passkey) {
                page = event.previous_page ? next_page(next_page(page)) : next_page(page);
            }
            dirty = true;
        }
        if (ota_view.phase != last_ota.phase || ota_view.error != last_ota.error ||
            ota_view.offset != last_ota.offset || ota_view.size != last_ota.size ||
            ota_view.version != last_ota.version) {
            dirty = true;
            last_ota = ota_view;
        }
        if (ota_view.phase == OtaPhase::Rebooting && reboot_at_ms == 0) {
            reboot_at_ms = now_ms + 500;
        }
        if (reboot_at_ms != 0 && now_ms >= reboot_at_ms) esp_restart();
        const bool show_ota_error = ota_failure_visible(
            shown_failure.error, shown_failure.started_ms, now_ms);
        const uint32_t confirm_seconds = ota_view.phase == OtaPhase::Confirming
            ? ota_seconds_remaining(ota_view.phase_started_ms + 60000, now_ms)
            : 0;
        const uint64_t second = now_ms / 1000;
        if (dirty || second != last_checked_second || ota_view.phase != OtaPhase::Idle || show_ota_error) {
            // A BLE connection is not application-ready until link encryption is
            // established; treating an unencrypted connection as online would
            // expose stale data during the passkey ceremony.
            const bool connected = ble_link.connected && ble_link.encrypted;
            auto render_key = make_timed_render_key(model, page, connected, now_ms, latest_battery);
            render_key.ota_confirm_seconds = confirm_seconds;
            // Poll time-dependent state once per second, but redraw only when a
            // visible value changed. This keeps countdowns accurate
            // without causing periodic full-screen flicker.
            if (dirty || !has_last_render_key || render_key != last_render_key) {
                const OtaSnapshot display_ota = ota_snapshot_for_display(
                    ota_view, shown_failure.error, show_ota_error);
                ui.render(model, page, connected,
                          ble_link.has_passkey, ble_link.passkey, now_ms,
                          display_ota, confirm_seconds, show_ota_error, latest_battery);
                last_render_key = render_key;
                has_last_render_key = true;
            }
            dirty = false;
            last_checked_second = second;
        }
        ble.verify_boot();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
