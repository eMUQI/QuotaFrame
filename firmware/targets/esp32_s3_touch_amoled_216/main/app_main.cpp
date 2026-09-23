#include <cstdint>
#include <atomic>

#include "bsp/esp-bsp.h"
#include "display_ui.hpp"
#include "memory_telemetry.hpp"
#include "performance_probe.hpp"
#include "usage_panel_state/panel_presentation.hpp"
#include "power_monitor.hpp"
#include "qmi8658.h"
#include "usage_panel_state/render_schedule.hpp"
#include "usage_rtc/rtc_clock.hpp"
#include "usage_panel_state/screensaver_state.hpp"
#include "usage_ble/app_events.hpp"
#include "usage_ble/ble_service.hpp"
#include "usage_ota/backend.hpp"
#include "usage_ota/power_gate.hpp"
#include "usage_ota/presentation.hpp"
#include "usage_ota/session.hpp"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

using namespace usage_panel;
using namespace usage_panel::amoled;

namespace {
constexpr char TAG[] = "ws_usage_panel";
constexpr uint64_t ORIENTATION_POLL_PERIOD_MS = 100;
constexpr uint64_t ORIENTATION_ERROR_RETRY_MS = 1000;
constexpr uint64_t CONNECTED_MEMORY_DELAY_MS = 60000;
constexpr uint64_t CLOCK_ERROR_RETRY_MS = 1000;
constexpr uint64_t POWER_POLL_PERIOD_MS = 1000;
// The PWR key is only visible as an AXP2101 interrupt latch, so it needs a poll
// fast enough to feel like a button rather than the power telemetry cadence.
constexpr uint64_t POWER_KEY_POLL_PERIOD_MS = 80;
constexpr uint32_t ORIENTATION_STARTUP_SETTLE_MS = 50;
constexpr uint32_t ORIENTATION_STARTUP_SAMPLE_MS = 20;
constexpr uint8_t ORIENTATION_STARTUP_MAX_SAMPLES = 12;
constexpr float ACCELERATION_ONE_G = 9.807F;

// The advertising name buffer holds 19 characters plus the terminator, and
// the service appends the four-digit MAC suffix. An empty prefix is rejected
// by the BLE service at runtime, so it must not reach a bootable image.
static_assert(
    sizeof(CONFIG_WS_USAGE_PANEL_BLE_ADVERTISING_PREFIX) >= 2 &&
        sizeof(CONFIG_WS_USAGE_PANEL_BLE_ADVERTISING_PREFIX) - 1 <= 15,
    "BLE advertising prefix must be non-empty and, plus the 4-digit MAC "
    "suffix, fit the 20-byte name buffer");

#ifdef CONFIG_WS_USAGE_PANEL_AUTO_ROTATION
constexpr bool kAutoRotationEnabled = true;
#else
constexpr bool kAutoRotationEnabled = false;
#endif

#if defined(CONFIG_WS_USAGE_PANEL_SHAKE_WAKE) || \
    defined(CONFIG_WS_USAGE_PANEL_TILT_WAKE)
constexpr bool kWakeDetectionEnabled = true;
#else
constexpr bool kWakeDetectionEnabled = false;
#endif

// Only meaningful while auto rotation is off; with auto rotation on the
// choice symbols are absent and this falls through to Deg0 unused.
constexpr ScreenRotation kFixedRotation =
#if defined(CONFIG_WS_USAGE_PANEL_FIXED_ROTATION_90)
    ScreenRotation::Deg90;
#elif defined(CONFIG_WS_USAGE_PANEL_FIXED_ROTATION_180)
    ScreenRotation::Deg180;
#elif defined(CONFIG_WS_USAGE_PANEL_FIXED_ROTATION_270)
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
EspOtaBackend ota_backend("ws_usage_panel");
OtaSession ota(ota_backend);
AtomicPowerSource ota_power_source;
BleService ble;
DisplayUi ui;
LinkUpdate ble_link{};
std::atomic<Page> status_page{Page::Overview};

const char* get_ui_location(void* context)
{
    const auto* current = static_cast<const std::atomic<Page>*>(context);
    return page_name(current->load(std::memory_order_relaxed));
}

const BleServiceConfig ble_config{
    .advertising_prefix = CONFIG_WS_USAGE_PANEL_BLE_ADVERTISING_PREFIX,
    .status_name = "Waveshare Usage Panel",
    .target = "waveshare_amoled_216",
    .enable_time_sync = true,
    .ota = &ota,
    .get_ui_location = get_ui_location,
    .ui_context = &status_page,
    .enable_screen_toggle = true,
    .enable_screen_page = true,
};

bool init_orientation_sensor(qmi8658_dev_t& sensor)
{
    const auto bus = bsp_i2c_get_handle();
    if (bus == nullptr) {
        ESP_LOGE(TAG, "Waveshare I2C bus is unavailable for QMI8658");
        return false;
    }

    if (qmi8658_init(&sensor, bus, QMI8658_ADDRESS_HIGH) != ESP_OK ||
        qmi8658_set_accel_range(&sensor, QMI8658_ACCEL_RANGE_2G) != ESP_OK ||
        qmi8658_set_accel_odr(&sensor, QMI8658_ACCEL_ODR_62_5HZ) != ESP_OK ||
        qmi8658_enable_sensors(&sensor, QMI8658_ENABLE_ACCEL) != ESP_OK) {
        ESP_LOGE(TAG, "QMI8658 initialization failed; rotation and motion "
                      "wake stay disabled");
        return false;
    }
    qmi8658_set_accel_unit_mps2(&sensor, true);
    ESP_LOGI(TAG, "QMI8658 accelerometer enabled");
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

    qmi8658_dev_t orientation_sensor{};
    const bool orientation_sensor_ready =
        (kAutoRotationEnabled || kWakeDetectionEnabled) &&
        init_orientation_sensor(orientation_sensor);
    OrientationTracker orientation_tracker;
    ScreenRotation initial_rotation = kFixedRotation;
    bool initial_orientation_committed = false;
    bool initial_orientation_read_failed = false;
    if (orientation_sensor_ready && kAutoRotationEnabled) {
        vTaskDelay(pdMS_TO_TICKS(ORIENTATION_STARTUP_SETTLE_MS));
        for (uint8_t sample = 0;
             sample < ORIENTATION_STARTUP_MAX_SAMPLES; ++sample) {
            float accel_x = 0.0F;
            float accel_y = 0.0F;
            float accel_z = 0.0F;
            const esp_err_t sensor_error = qmi8658_read_accel(
                &orientation_sensor, &accel_x, &accel_y, &accel_z);
            if (sensor_error != ESP_OK) {
                ESP_LOGW(TAG,
                         "initial QMI8658 read failed; using default rotation: %s",
                         esp_err_to_name(sensor_error));
                initial_orientation_read_failed = true;
                break;
            }
            if (orientation_tracker.observe(
                    accel_x / ACCELERATION_ONE_G,
                    accel_y / ACCELERATION_ONE_G)) {
                initial_rotation = orientation_tracker.rotation();
                initial_orientation_committed = true;
                ESP_LOGI(TAG, "initial display rotation: %u",
                         static_cast<unsigned>(initial_rotation));
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(ORIENTATION_STARTUP_SAMPLE_MS));
        }
        if (!initial_orientation_committed &&
            !initial_orientation_read_failed) {
            ESP_LOGW(TAG,
                     "initial orientation was not stable; using default rotation");
        }
    }

    const auto i2c_bus = bsp_i2c_get_handle();
    RtcClock rtc;
    const bool rtc_ready = i2c_bus != nullptr && rtc.begin(i2c_bus);
    if (!rtc_ready) {
        ESP_LOGW(TAG, "PCF85063A RTC unavailable; clock will show --:--");
    }
    PowerMonitor power_monitor;
    const bool power_monitor_ready =
        i2c_bus != nullptr && power_monitor.begin(i2c_bus);
    if (!power_monitor_ready) {
        ESP_LOGW(TAG, "AXP2101 telemetry unavailable; battery will be hidden");
    }

    log_memory_checkpoint("before_lvgl", false);
    ESP_ERROR_CHECK(ui.begin(initial_rotation) ? ESP_OK : ESP_FAIL);
    log_memory_checkpoint("after_ui", true);
    ESP_ERROR_CHECK(verify_lvgl_allocations_external()
                        ? ESP_OK
                        : ESP_ERR_INVALID_STATE);

    DisplaySettings saved_settings = load_display_settings({
        CONFIG_WS_USAGE_PANEL_DISPLAY_BRIGHTNESS_PERCENT,
        CONFIG_WS_USAGE_PANEL_SCREENSAVER_TIMEOUT_SECONDS});
    DisplaySettings settings = saved_settings;
    bool settings_open = false;
    ui.apply_settings(settings, false);

    ScreenRotation desired_rotation = initial_rotation;
    ScreenRotation applied_rotation = desired_rotation;
    uint64_t next_orientation_poll_ms = 0;

    ESP_ERROR_CHECK(events.begin() ? ESP_OK : ESP_ERR_NO_MEM);
    // Wired before BLE so no peer can reach the manifest path ungated; the
    // verdict itself is published by the main-loop power poll below.
    ota.set_power_source(&ota_power_source);
    ESP_ERROR_CHECK(ble.start(events, ble_config));
    log_memory_checkpoint("after_ble", true);
#if CONFIG_WS_USAGE_PANEL_MEMORY_PROBE
    ui.run_memory_probe();
#endif

    ESP_LOGI(TAG, "Waveshare usage panel started");

    Page page = Page::Overview;
    bool dirty = true;
    bool has_last_render_key = false;
    TimedRenderKey last_render_key{};
    uint64_t last_checked_second = UINT64_MAX;
    OtaSnapshot last_ota{};
    RetainedOtaFailure shown_failure{};
    uint64_t reboot_at_ms = 0;
    uint64_t encrypted_started_ms = 0;
    bool connected_memory_logged = false;
    LocalCalendarTime calendar{};
    bool calendar_available = false;
    uint64_t next_clock_poll_ms = 0;
    PowerStateFilter power_filter;
    BatteryView battery_view{};
    RawPowerSample raw_sample{};
    uint64_t next_power_poll_ms = 0;
    uint64_t next_power_key_poll_ms = 0;
    ScreensaverController screensaver(
        static_cast<uint64_t>(settings.clock_timeout_seconds) *
        1000ULL);
    screensaver.reset(esp_timer_get_time() / 1000, page);
#if defined(CONFIG_WS_USAGE_PANEL_SHAKE_WAKE)
    ShakeDetector shake_detector(
        CONFIG_WS_USAGE_PANEL_SHAKE_WAKE_THRESHOLD_CG * 0.01F);
#endif
#if defined(CONFIG_WS_USAGE_PANEL_TILT_WAKE)
    TiltWakeDetector tilt_detector(
        static_cast<float>(CONFIG_WS_USAGE_PANEL_TILT_WAKE_DEGREES));
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
            } else if (event.type == AppEventType::TimeSync && rtc_ready && rtc.write(event.time_sync.calendar)) {
                calendar = event.time_sync.calendar;
                calendar_available = true;
                next_clock_poll_ms = now_ms +
                    milliseconds_to_next_minute(calendar);
                ESP_LOGI(
                    TAG, "RTC synchronized from bridge sequence %lu",
                    static_cast<unsigned long>(event.time_sync.sequence));
            } else {
                ESP_LOGW(TAG, "RTC time synchronization failed");
            }
            dirty = true;
        }

        if (now_ms >= next_clock_poll_ms) {
            LocalCalendarTime current{};
            calendar_available = rtc_ready && rtc.read(current);
            if (calendar_available) {
                calendar = current;
                next_clock_poll_ms = now_ms +
                    milliseconds_to_next_minute(calendar);
            } else {
                next_clock_poll_ms = now_ms + CLOCK_ERROR_RETRY_MS;
            }
        }
        if (power_monitor_ready && now_ms >= next_power_key_poll_ms) {
            next_power_key_poll_ms = now_ms + POWER_KEY_POLL_PERIOD_MS;
            if (power_monitor.take_power_key_short_press() && !settings_open) {
                const bool was_asleep = screensaver.view(now_ms).active;
                screensaver.note_power_key(now_ms, page);
                if (was_asleep) {
                    page = screensaver.restore_page();
                    status_page.store(page, std::memory_order_relaxed);
                }
                dirty = true;
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

        const bool encrypted = ble_link.connected && ble_link.encrypted;
        if (!encrypted) {
            encrypted_started_ms = 0;
            connected_memory_logged = false;
        } else if (encrypted_started_ms == 0) {
            encrypted_started_ms = now_ms;
        } else if (!connected_memory_logged &&
                   now_ms - encrypted_started_ms >=
                       CONNECTED_MEMORY_DELAY_MS) {
            log_memory_checkpoint("connected_60s", true);
            connected_memory_logged = true;
#if CONFIG_WS_USAGE_PANEL_MEMORY_PROBE
            ui.run_memory_probe();
#endif
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
            const esp_err_t sensor_error = qmi8658_read_accel(
                &orientation_sensor, &accel_x, &accel_y, &accel_z);
            if (sensor_error != ESP_OK) {
                ESP_LOGW(TAG, "QMI8658 acceleration read failed: %s",
                         esp_err_to_name(sensor_error));
                next_orientation_poll_ms =
                    esp_timer_get_time() / 1000 +
                    ORIENTATION_ERROR_RETRY_MS;
            } else if (kAutoRotationEnabled && orientation_tracker.observe(
                           accel_x / ACCELERATION_ONE_G,
                           accel_y / ACCELERATION_ONE_G)) {
                desired_rotation = orientation_tracker.rotation();
            }
#if defined(CONFIG_WS_USAGE_PANEL_SHAKE_WAKE) || \
    defined(CONFIG_WS_USAGE_PANEL_TILT_WAKE)
            if (sensor_error == ESP_OK) {
                const float ax_g = accel_x / ACCELERATION_ONE_G;
                const float ay_g = accel_y / ACCELERATION_ONE_G;
                const float az_g = accel_z / ACCELERATION_ONE_G;
                bool woke = false;
#if defined(CONFIG_WS_USAGE_PANEL_SHAKE_WAKE)
                woke = shake_detector.update(ax_g, ay_g, az_g, now_ms);
#endif
#if defined(CONFIG_WS_USAGE_PANEL_TILT_WAKE)
                woke = tilt_detector.update(ax_g, ay_g, az_g) || woke;
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

#if CONFIG_WS_USAGE_PANEL_PERFORMANCE_PROBE
        poll_performance_probe(static_cast<uint32_t>(page), connected,
                               static_cast<uint32_t>(ota_view.phase), ota_view.offset);
#endif
        ble.verify_boot();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
