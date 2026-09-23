#include <atomic>
#include <cstdint>

#include "bsp/esp_mosaico.h"
#include "display_ui.hpp"
#include "local_clock.hpp"
#include "usage_panel_state/panel_presentation.hpp"
#include "power_monitor.hpp"
#include "usage_panel_state/render_schedule.hpp"
#include "usage_panel_state/screensaver_state.hpp"
#include "usage_ble/app_events.hpp"
#include "usage_ble/ble_service.hpp"
#include "usage_ota/backend.hpp"
#include "usage_ota/power_gate.hpp"
#include "usage_ota/presentation.hpp"
#include "usage_ota/session.hpp"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

using namespace usage_panel;
using namespace usage_panel::mosaico;

namespace {
constexpr char TAG[] = "mosaico_usage_panel";
constexpr uint64_t ORIENTATION_POLL_PERIOD_MS = 100;
constexpr uint64_t ORIENTATION_ERROR_RETRY_MS = 1000;
constexpr uint64_t CLOCK_ERROR_RETRY_MS = 1000;
constexpr uint64_t POWER_POLL_PERIOD_MS = 1000;
constexpr uint32_t ORIENTATION_STARTUP_SETTLE_MS = 50;
constexpr uint32_t ORIENTATION_STARTUP_SAMPLE_MS = 20;
constexpr uint8_t ORIENTATION_STARTUP_MAX_SAMPLES = 12;

// The advertising name buffer holds 19 characters plus the terminator, and
// the service appends the four-digit MAC suffix. An empty prefix is rejected
// by the BLE service at runtime, so it must not reach a bootable image.
static_assert(
    sizeof(CONFIG_MOSAICO_USAGE_PANEL_BLE_ADVERTISING_PREFIX) >= 2 &&
        sizeof(CONFIG_MOSAICO_USAGE_PANEL_BLE_ADVERTISING_PREFIX) - 1 <= 15,
    "BLE advertising prefix must be non-empty and, plus the 4-digit MAC "
    "suffix, fit the 20-byte name buffer");

#ifdef CONFIG_MOSAICO_USAGE_PANEL_AUTO_ROTATION
constexpr bool kAutoRotationEnabled = true;
#else
constexpr bool kAutoRotationEnabled = false;
#endif

#if defined(CONFIG_MOSAICO_USAGE_PANEL_SHAKE_WAKE) || \
    defined(CONFIG_MOSAICO_USAGE_PANEL_TILT_WAKE)
constexpr bool kWakeDetectionEnabled = true;
#else
constexpr bool kWakeDetectionEnabled = false;
#endif

// Only meaningful while auto rotation is off; with auto rotation on the
// choice symbols are absent and this falls through to Deg0 unused.
constexpr ScreenRotation kFixedRotation =
#if defined(CONFIG_MOSAICO_USAGE_PANEL_FIXED_ROTATION_90)
    ScreenRotation::Deg90;
#elif defined(CONFIG_MOSAICO_USAGE_PANEL_FIXED_ROTATION_180)
    ScreenRotation::Deg180;
#elif defined(CONFIG_MOSAICO_USAGE_PANEL_FIXED_ROTATION_270)
    ScreenRotation::Deg270;
#else
    ScreenRotation::Deg0;
#endif

uint64_t milliseconds_to_next_minute(const LocalCalendarTime& calendar)
{
    return static_cast<uint64_t>(60U - calendar.second) * 1000U;
}

AppEventQueue events;
UsageModel model;
EspOtaBackend ota_backend("mosaico_usage_panel");
OtaSession ota(ota_backend);
AtomicPowerSource ota_power_source;
BleService ble;
DisplayUi ui;
LinkUpdate ble_link{};
std::atomic<Page> status_page{Page::Overview};
std::atomic<bool> ai_button_pressed{false};

const char* get_ui_location(void* context)
{
    const auto* current = static_cast<const std::atomic<Page>*>(context);
    return page_name(current->load(std::memory_order_relaxed));
}

const BleServiceConfig ble_config{
    .advertising_prefix = CONFIG_MOSAICO_USAGE_PANEL_BLE_ADVERTISING_PREFIX,
    .status_name = "ESP-Mosaico Usage Panel",
    .target = "esp_mosaico",
    .enable_time_sync = true,
    .ota = &ota,
    .get_ui_location = get_ui_location,
    .ui_context = &status_page,
    .enable_screen_toggle = true,
    .enable_screen_page = true,
};

void ai_button_event(void* /*button_handle*/, void* /*user_data*/)
{
    ai_button_pressed.store(true, std::memory_order_relaxed);
}

bool init_ai_button()
{
    button_handle_t buttons[BSP_BUTTON_NUM]{};
    int count = 0;
    const esp_err_t error =
        bsp_iot_button_create(buttons, &count, BSP_BUTTON_NUM);
    if (error != ESP_OK || count <= BSP_BUTTON_AI) {
        ESP_LOGW(TAG, "AI button unavailable: %s", esp_err_to_name(error));
        return false;
    }
    if (iot_button_register_cb(
            buttons[BSP_BUTTON_AI], BUTTON_SINGLE_CLICK, nullptr,
            ai_button_event, nullptr) != ESP_OK) {
        ESP_LOGW(TAG, "AI button callback registration failed");
        iot_button_delete(buttons[BSP_BUTTON_AI]);
        return false;
    }
    return true;
}

bool init_orientation_sensor()
{
    if (bsp_imu_init() != ESP_OK) {
        ESP_LOGE(TAG, "BMI270 initialization failed; rotation and motion "
                      "wake stay disabled");
        return false;
    }
    const bsp_imu_config_t config = BSP_IMU_CONFIG_DEFAULT();
    if (bsp_imu_start(&config) != ESP_OK) {
        ESP_LOGE(TAG, "BMI270 start failed; rotation and motion wake stay "
                      "disabled");
        return false;
    }
    ESP_LOGI(TAG, "BMI270 accelerometer enabled");
    return true;
}
}  // namespace

extern "C" void app_main(void)
{
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs);

    ScreenRotation initial_rotation = kFixedRotation;
    OrientationTracker orientation_tracker;
    // The IMU shares the board I2C bus, which bsp_display_start() brings up
    // together with the VCC_3V3 rail, so the panel has to come first.
    ESP_ERROR_CHECK(ui.begin(initial_rotation) ? ESP_OK : ESP_FAIL);

    const bool orientation_sensor_ready =
        (kAutoRotationEnabled || kWakeDetectionEnabled) &&
        init_orientation_sensor();
    if (orientation_sensor_ready && kAutoRotationEnabled) {
        vTaskDelay(pdMS_TO_TICKS(ORIENTATION_STARTUP_SETTLE_MS));
        for (uint8_t sample = 0;
             sample < ORIENTATION_STARTUP_MAX_SAMPLES; ++sample) {
            float accel_x = 0.0F;
            float accel_y = 0.0F;
            float accel_z = 0.0F;
            if (bsp_imu_get_accel(&accel_x, &accel_y, &accel_z) != ESP_OK) {
                ESP_LOGW(TAG, "initial BMI270 read failed; using default rotation");
                break;
            }
            if (orientation_tracker.observe(accel_x, accel_y)) {
                initial_rotation = orientation_tracker.rotation();
                ESP_LOGI(TAG, "initial display rotation: %u",
                         static_cast<unsigned>(initial_rotation));
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(ORIENTATION_STARTUP_SAMPLE_MS));
        }
    }

    PowerMonitor power_monitor;
    const bool power_monitor_ready = power_monitor.begin();
    if (!power_monitor_ready) {
        ESP_LOGW(TAG, "BQ27220 telemetry unavailable; battery will be hidden");
    }
    const bool ai_button_ready = init_ai_button();

    DisplaySettings saved_settings = load_display_settings({
        CONFIG_MOSAICO_USAGE_PANEL_DISPLAY_BRIGHTNESS_PERCENT,
        CONFIG_MOSAICO_USAGE_PANEL_SCREENSAVER_TIMEOUT_SECONDS});
    DisplaySettings settings = saved_settings;
    bool settings_open = false;
    ui.apply_settings(settings, false);

    ScreenRotation desired_rotation = initial_rotation;
    // ui.begin() brought the panel up at kFixedRotation. Anything the startup
    // probe chose is applied by the loop's rotation step, which retries until
    // it succeeds rather than committing a rotation the panel never took.
    ScreenRotation applied_rotation = kFixedRotation;
    uint64_t next_orientation_poll_ms = 0;

    ESP_ERROR_CHECK(events.begin() ? ESP_OK : ESP_ERR_NO_MEM);
    // Wired before BLE so no peer can reach the manifest path ungated; the
    // verdict itself is published by the main-loop power poll below.
    ota.set_power_source(&ota_power_source);
    ESP_ERROR_CHECK(ble.start(events, ble_config));

    ESP_LOGI(TAG, "ESP-Mosaico usage panel started");

    Page page = Page::Overview;
    bool dirty = true;
    bool has_last_render_key = false;
    TimedRenderKey last_render_key{};
    uint64_t last_checked_second = UINT64_MAX;
    OtaSnapshot last_ota{};
    RetainedOtaFailure shown_failure{};
    uint64_t reboot_at_ms = 0;
    LocalCalendarTime calendar{};
    bool calendar_available = false;
    uint64_t next_clock_poll_ms = 0;
    PowerStateFilter power_filter;
    BatteryView battery_view{};
    RawPowerSample raw_sample{};
    uint64_t next_power_poll_ms = 0;
    ScreensaverController screensaver(
        static_cast<uint64_t>(settings.clock_timeout_seconds) * 1000ULL);
    screensaver.reset(esp_timer_get_time() / 1000, page);
#if defined(CONFIG_MOSAICO_USAGE_PANEL_SHAKE_WAKE)
    ShakeDetector shake_detector(
        CONFIG_MOSAICO_USAGE_PANEL_SHAKE_WAKE_THRESHOLD_CG * 0.01F);
#endif
#if defined(CONFIG_MOSAICO_USAGE_PANEL_TILT_WAKE)
    TiltWakeDetector tilt_detector(
        static_cast<float>(CONFIG_MOSAICO_USAGE_PANEL_TILT_WAKE_DEGREES));
#endif

    while (true) {
        const uint64_t now_ms = esp_timer_get_time() / 1000;
        ota.tick(now_ms);
        OtaSnapshot ota_view = ota.snapshot();
        if (ui.take_ota_confirmation() &&
            route_touch(ota_view.phase, OtaTouchControl::Confirm) ==
                TouchAction::Confirm) {
            ota.confirm(now_ms);
            ota_view = ota.snapshot();
            dirty = true;
        }
        if (ui.take_ota_denial() &&
            route_touch(ota_view.phase, OtaTouchControl::Deny) ==
                TouchAction::Deny) {
            ota.deny();
            ota_view = ota.snapshot();
            dirty = true;
        }
        if (ui.take_touch_down()) {
            const bool was_asleep = screensaver.view(now_ms).active;
            screensaver.note_touch_down(now_ms);
            if (was_asleep) {
                page = screensaver.restore_page();
                status_page.store(page, std::memory_order_relaxed);
            }
            dirty = true;
        }
        if (ai_button_ready &&
            ai_button_pressed.exchange(false, std::memory_order_relaxed)) {
            const bool was_asleep = screensaver.view(now_ms).active;
            screensaver.note_touch_down(now_ms);
            if (was_asleep) {
                page = screensaver.restore_page();
            } else if (ota_view.phase == OtaPhase::Idle &&
                       !ble_link.has_passkey && !settings_open) {
                page = page_after_swipe(page, SwipeDirection::Next);
                ui.request_page(page);
                screensaver.reset(now_ms, page);
            }
            status_page.store(page, std::memory_order_relaxed);
            dirty = true;
        }
        if (retain_ota_failure(ota_view, shown_failure, now_ms)) {
            dirty = true;
        }
        const SettingsAction action = ui.take_settings_action();
        const bool settings_blocked = ble_link.has_passkey || ota_view.phase != OtaPhase::Idle;
        if (settings_open && settings_blocked) {
            settings = saved_settings;
            settings_open = false;
            ui.apply_settings(settings, false);
            screensaver.reset(now_ms, page);
            dirty = true;
        }
        if (action != SettingsAction::None && !settings_blocked) {
            bool save_error = false;
            const DisplaySettings before = settings;
            const bool was_open = settings_open;
            if (action == SettingsAction::Open) settings_open = true;
            else if (settings_open) {
                if (action == SettingsAction::Dimmer)
                    settings.brightness = settings.brightness <= 10 ? 1 : settings.brightness - 10;
                else if (action == SettingsAction::Brighter)
                    settings.brightness = settings.brightness >= 90 ? 100 : settings.brightness + 10;
                else if (action >= SettingsAction::TimeoutOff && action <= SettingsAction::Timeout30)
                    select_clock_timeout(action, settings.clock_timeout_seconds);
                else if (action == SettingsAction::Save) {
                    const esp_err_t error = save_display_settings(settings);
                    save_error = error != ESP_OK;
                    if (!save_error) { saved_settings = settings; settings_open = false; }
                    else ESP_LOGW(TAG, "display settings save failed: %s", esp_err_to_name(error));
                } else if (action == SettingsAction::Cancel) {
                    settings = saved_settings;
                    settings_open = false;
                }
            }
            if (!ui.apply_settings(settings, settings_open, save_error,
                    settings.brightness != saved_settings.brightness ||
                    settings.clock_timeout_seconds != saved_settings.clock_timeout_seconds)) {
                settings = before;
                settings_open = was_open;
                ESP_LOGW(TAG, "display settings preview failed");
            }
            screensaver.set_idle_delay(static_cast<uint64_t>(settings.clock_timeout_seconds) * 1000);
            screensaver.reset(now_ms, page);
            dirty = true;
        }
        const Page requested = ui.requested_page();
        if (requested != page) {
            page = requested;
            status_page.store(page, std::memory_order_relaxed);
            dirty = true;
        }

        AppEvent event{};
        while (events.receive(event)) {
            if (event.type == AppEventType::Usage) {
                model.apply(event.usage, esp_timer_get_time() / 1000);
            } else if (event.type == AppEventType::Link) {
                ble_link = event.link;
            } else if (event.type == AppEventType::ScreenPage) {
                if (ota_view.phase == OtaPhase::Idle && !ble_link.has_passkey) {
                    if (screensaver.view(now_ms).active) page = screensaver.restore_page();
                    page = page_after_swipe(page, event.previous_page
                        ? SwipeDirection::Previous : SwipeDirection::Next);
                    ui.request_page(page);
                    screensaver.reset(now_ms, page);
                    settings = saved_settings;
                    settings_open = false;
                    ui.apply_settings(settings, false);
                    status_page.store(page, std::memory_order_relaxed);
                }
            } else if (event.type == AppEventType::ScreenToggle) {
                settings = saved_settings;
                settings_open = false;
                ui.apply_settings(settings, false);
                const bool was_asleep = screensaver.view(now_ms).active;
                screensaver.note_remote_toggle(now_ms, page);
                if (was_asleep) {
                    page = screensaver.restore_page();
                    status_page.store(page, std::memory_order_relaxed);
                }
            } else if (event.type == AppEventType::TimeSync) {
                set_local_calendar(event.time_sync.calendar);
                calendar = event.time_sync.calendar;
                calendar_available = true;
                next_clock_poll_ms = now_ms + milliseconds_to_next_minute(calendar);
                ESP_LOGI(
                    TAG, "clock synchronized from bridge sequence %lu",
                    static_cast<unsigned long>(event.time_sync.sequence));
            }
            dirty = true;
        }

        if (now_ms >= next_clock_poll_ms) {
            LocalCalendarTime current{};
            calendar_available = read_local_calendar(current);
            if (calendar_available) {
                calendar = current;
                next_clock_poll_ms = now_ms + milliseconds_to_next_minute(calendar);
            } else {
                next_clock_poll_ms = now_ms + CLOCK_ERROR_RETRY_MS;
            }
        }
        if (now_ms >= next_power_poll_ms) {
            next_power_poll_ms = now_ms + POWER_POLL_PERIOD_MS;
            raw_sample = {};
            if (power_monitor_ready) power_monitor.read(raw_sample);
            battery_view = power_filter.update(raw_sample, now_ms);
            const OtaPowerReading reading =
                derive_power_reading(battery_view, raw_sample);
            ota_power_source.publish(
                reading.valid, reading.external_power, reading.percent);
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
        if (orientation_sensor_ready && now_ms >= next_orientation_poll_ms) {
            next_orientation_poll_ms = now_ms + ORIENTATION_POLL_PERIOD_MS;

            float accel_x = 0.0F;
            float accel_y = 0.0F;
            float accel_z = 0.0F;
            const esp_err_t sensor_error =
                bsp_imu_get_accel(&accel_x, &accel_y, &accel_z);
            if (sensor_error != ESP_OK) {
                ESP_LOGW(TAG, "BMI270 acceleration read failed: %s",
                         esp_err_to_name(sensor_error));
                next_orientation_poll_ms =
                    esp_timer_get_time() / 1000 + ORIENTATION_ERROR_RETRY_MS;
            } else if (kAutoRotationEnabled &&
                       orientation_tracker.observe(accel_x, accel_y)) {
                desired_rotation = orientation_tracker.rotation();
            }
#if defined(CONFIG_MOSAICO_USAGE_PANEL_SHAKE_WAKE) || \
    defined(CONFIG_MOSAICO_USAGE_PANEL_TILT_WAKE)
            if (sensor_error == ESP_OK) {
                bool woke = false;
#if defined(CONFIG_MOSAICO_USAGE_PANEL_SHAKE_WAKE)
                woke = shake_detector.update(accel_x, accel_y, accel_z, now_ms);
#endif
#if defined(CONFIG_MOSAICO_USAGE_PANEL_TILT_WAKE)
                woke = tilt_detector.update(accel_x, accel_y, accel_z) || woke;
#endif
                if (woke) {
                    screensaver.note_shake(now_ms);
                    dirty = true;
                }
            }
#endif
        }

        if (desired_rotation != applied_rotation &&
            ui.set_rotation(desired_rotation)) {
            applied_rotation = desired_rotation;
            dirty = true;
        }

        screensaver.set_idle_delay(static_cast<uint64_t>(settings.clock_timeout_seconds) * 1000);
        screensaver.update(
            now_ms, page, ble_link.has_passkey,
            ota_view.phase != OtaPhase::Idle || show_ota_error || settings_open);
        const ScreensaverView screensaver_view = screensaver.view(now_ms);
        const PanelPresentation presentation{
            .clock = format_clock_view(
                calendar_available ? &calendar : nullptr),
            .battery = battery_view,
            .screensaver = screensaver_view,
        };
        const bool input_enabled =
            !screensaver_view.active && screensaver.touch_allowed(now_ms);
        const bool connected = ble_link.connected && ble_link.encrypted;
        const auto render_key = make_timed_render_key(model, page, connected, now_ms, presentation);
        const bool render_key_changed =
            !has_last_render_key || render_key != last_render_key;

        const uint64_t second = now_ms / 1000;
        const bool second_changed = second != last_checked_second;
        // The confirmation countdown and the failure visibility window are both
        // derived from now_ms alone, so neither reaches the render key. While
        // any OTA overlay is up the panel therefore redraws on the second.
        const bool ota_visible =
            ota_view.phase != OtaPhase::Idle || show_ota_error;
        if (dirty || render_key_changed || (ota_visible && second_changed)) {
            const OtaSnapshot display_ota = ota_snapshot_for_display(
                ota_view, shown_failure.error, show_ota_error);
            ui.render(
                model, page, connected,
                ble_link.has_passkey, ble_link.passkey, now_ms,
                display_ota, confirm_seconds, show_ota_error,
                presentation, input_enabled);
            last_render_key = render_key;
            has_last_render_key = true;
            dirty = false;
        }
        last_checked_second = second;

        ble.verify_boot();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
