#include "board.hpp"
#include "demo_view.hpp"
#include "display_ui.hpp"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "usage_ble/ble_service.hpp"
#include "usage_ota/backend.hpp"
#include "usage_ota/power_gate.hpp"
#include "usage_ota/presentation.hpp"
#include <algorithm>
#include <atomic>
#include <cstdio>
using namespace usage_panel;
using namespace usage_panel::read_pico;
namespace {
AppEventQueue events;
BleService ble;
EspOtaBackend backend("mr_read_pico");
OtaSession ota(backend);
AtomicPowerSource power;
Board board;
DisplayUi ui;
View view;
std::atomic<Page> location{Page::Home};
constexpr char kAdvertisingPrefix[] = "QF-MR-PICO-";
const char *page_name(void *) {
    switch (location.load()) {
    case Page::Home:
        return "overview";
    case Page::Clock:
        return "clock";
    case Page::Settings:
        return "settings";
    }
    return "overview";
}
constexpr uint8_t warning(uint8_t old, uint8_t value) {
    if (value >= 95)
        return 2;
    if (old == 2 && value >= 93)
        return 2;
    if (value >= 80 || (old >= 1 && value >= 78))
        return 1;
    return 0;
}
static_assert(warning(0, 80) == 1 && warning(1, 78) == 1 && warning(1, 77) == 0);
static_assert(warning(0, 95) == 2 && warning(2, 93) == 2 && warning(2, 92) == 1);
} // namespace
extern "C" void app_main() {
    // Preserve NVS on initialization failures; recovery must not silently erase bonds.
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(board.begin() ? ESP_OK : ESP_FAIL);
    view.settings = board.load();
    view.orientation = view.settings.rotation == 2 ? 1 : 0;
    if (view.settings.rotation == 0) {
        uint8_t measured;
        if (board.orientation(measured))
            view.orientation = measured;
    }
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(view.device_name, sizeof(view.device_name), "%s%02X%02X", kAdvertisingPrefix, mac[4],
             mac[5]);
    view.now_ms = esp_timer_get_time() / 1000;
    board.poll(view);
    power.publish(view.power_valid, board.external_power(), view.battery >= 0 ? view.battery : 0);
    ota.set_power_source(&power);
    ESP_ERROR_CHECK(events.begin(32) ? ESP_OK : ESP_ERR_NO_MEM);
    ui.begin(board.display());
    BleServiceConfig config{kAdvertisingPrefix,
                            "MindReset Read Pico",
                            "mindreset_read_pico",
                            true,
                            &ota,
                            page_name,
                            nullptr,
                            true,
                            true};
    ESP_ERROR_CHECK(ble.start(events, config));
    usb_serial_jtag_driver_config_t usb = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb));
    uint64_t next_poll = 0, next_draw = 0, last_activity = view.now_ms, orientation_since = 0,
             next_orientation = 0;
    bool force = true, baseline = false, diagnostic = false, idle_clock = false,
         reboot_drawn = false;
    uint8_t candidate = view.orientation;
    Page return_page = Page::Home;
    OtaPhase last_phase = OtaPhase::Idle;
    int last_progress = -1;
    RetainedOtaFailure shown_failure;
    // 255 marks a window without a displayed value.
    uint8_t shown[2][2] = {{255, 255}, {255, 255}};
    ESP_LOGI("read_pico", "READY serial: 1-9 design boards, i state, r live");
    while (true) {
        view.now_ms = esp_timer_get_time() / 1000;
        ui.tick(view.now_ms);
        ota.tick(view.now_ms);
        const OtaError previous_error = view.ota.error;
        view.ota = ota.snapshot();
        if (retain_ota_failure(view.ota, shown_failure, view.now_ms))
            force = true;
        view.ota.error = shown_failure.error != OtaError::None &&
                                 view.now_ms - shown_failure.started_ms < 15000
                             ? shown_failure.error
                             : OtaError::None;
        force = force || view.ota.error != previous_error;
        int progress = ota_progress_percent(view.ota.offset, view.ota.size) / 5;
        if (view.ota.phase != last_phase || progress != last_progress) {
            force = true;
            last_phase = view.ota.phase;
            last_progress = progress;
        }
        // The RESTARTING frame is drawn synchronously one iteration before the restart.
        if (view.ota.phase == OtaPhase::Rebooting && reboot_drawn)
            esp_restart();
        AppEvent event{};
        while (events.receive(event)) {
            switch (event.type) {
            case AppEventType::Usage:
                view.model.apply(event.usage, view.now_ms);
                break;
            case AppEventType::Link:
                // Online and offline differ by a gray level across both service blocks.
                baseline = baseline || view.link.encrypted != event.link.encrypted;
                view.link = event.link;
                force = true;
                break;
            case AppEventType::TimeSync:
                if (!board.sync(event.time_sync.calendar))
                    ESP_LOGW("read_pico", "RTC sync failed");
                next_poll = 0;
                force = true;
                break;
            case AppEventType::ScreenToggle:
            case AppEventType::ScreenPage:
                // Home and Clock are the whole ring, so every remote step is a toggle.
                if (!ota.busy() && !view.link.has_passkey) {
                    view.page = view.page == Page::Clock ? Page::Home : Page::Clock;
                    idle_clock = false;
                    force = true;
                    last_activity = view.now_ms;
                }
                break;
            }
        }
        for (int i = 0; i < 2; ++i) {
            const auto &s = view.model.snapshot(i ? Provider::Claude : Provider::Codex);
            const bool short_present = s.has_valid_data && s.short_window.present &&
                                       (s.latest_short_present || !view.link.encrypted);
            const bool week_present = s.has_valid_data && s.week_window.present &&
                                      (s.latest_week_present || !view.link.encrypted);
            // The alarm level follows the fuller of the reported windows.
            uint8_t next = warning(
                view.warning[i],
                std::max(short_present ? s.short_window.used_percent : 0,
                         week_present ? s.week_window.used_percent : 0));
            if (next != view.warning[i]) {
                view.warning[i] = next;
                force = baseline = true;
            }
            const uint8_t values[2] = {
                short_present ? s.short_window.used_percent : uint8_t{255},
                week_present ? s.week_window.used_percent : uint8_t{255}};
            for (int window = 0; window < 2; ++window) {
                // A shrinking bar exposes track gray, which only a GC16 refresh restores cleanly.
                if (shown[i][window] != 255 && values[window] < shown[i][window])
                    baseline = true;
                shown[i][window] = values[window];
            }
        }
        const Input input = board.input(view.now_ms);
        if (input.active || input.key || input.tap)
            last_activity = view.now_ms;
        if (input.key || input.tap) {
            diagnostic = false;
            force = true;
            const TapTarget target = input.tap ? ui.hit(view, input.x, input.y) : TapTarget{};
            if (view.ota.phase == OtaPhase::Confirming) {
                if (input.key == 2 || target.kind == TapTarget::OtaConfirm)
                    ota.confirm(view.now_ms);
                else if (input.key == 5 || target.kind == TapTarget::OtaDeny)
                    ota.deny();
            } else if (ota.busy() || view.link.has_passkey) {
            } else if (view.page == Page::Settings) {
                if (input.key == 5 || target.kind == TapTarget::Done)
                    view.page = return_page;
                else if (target.kind == TapTarget::Option) {
                    auto &s = view.settings;
                    uint8_t *fields[] = {&s.refresh, &s.idle, &s.rotation};
                    *fields[target.row] = target.index;
                    view.save_error = !board.save(s);
                } else if (input.key == 2)
                    baseline = true;
            } else if (input.key == 5) {
                return_page = view.page;
                view.page = Page::Settings;
            } else if (input.key == 2) {
                baseline = true;
                ESP_LOGI("read_pico", "Manual display refresh; Bridge publication is host-driven");
            } else if (input.key || view.page == Page::Clock) {
                view.page = view.page == Page::Clock ? Page::Home : Page::Clock;
                idle_clock = false;
            }
        }
        if (view.now_ms >= next_orientation) {
            next_orientation = view.now_ms + 100;
            uint8_t measured;
            const bool decisive = board.orientation(measured);
            if (view.settings.rotation) {
                const uint8_t fixed = view.settings.rotation == 2 ? 1 : 0;
                if (view.orientation != fixed) {
                    view.orientation = fixed;
                    force = true;
                }
            } else if (ota.busy() || view.link.has_passkey || !decisive)
                orientation_since = view.now_ms;
            else if (candidate != measured) {
                candidate = measured;
                orientation_since = view.now_ms;
            } else if (measured != view.orientation && view.now_ms - orientation_since >= 1500) {
                view.orientation = measured;
                force = true;
            }
            // Picking the device up leaves the idle clock; a clock opened by key stays.
            if (board.moved() && idle_clock && view.page == Page::Clock) {
                view.page = Page::Home;
                idle_clock = false;
                force = true;
                last_activity = view.now_ms;
            }
        }
        const int idle_minutes[] = {0, 5, 15, 30};
        if (!ota.busy() && !view.link.has_passkey && view.page == Page::Home &&
            idle_minutes[view.settings.idle] &&
            view.now_ms - last_activity >= idle_minutes[view.settings.idle] * 60000ULL) {
            view.page = Page::Clock;
            idle_clock = true;
            force = true;
        }
        if (view.now_ms >= next_poll) {
            next_poll = view.now_ms + 1000;
            const bool clock_valid = view.clock_valid;
            const auto calendar = view.calendar;
            board.poll(view);
            if (clock_valid != view.clock_valid ||
                (view.clock_valid && (calendar.minute != view.calendar.minute ||
                                      calendar.hour != view.calendar.hour ||
                                      calendar.day != view.calendar.day)))
                force = true;
            board.sample_trend(view);
            power.publish(view.power_valid, board.external_power(),
                          view.battery >= 0 ? view.battery : 0);
        }
        uint8_t command = 0;
        if (usb_serial_jtag_read_bytes(&command, 1, 0) == 1 && !ota.busy()) {
            if (command == 'i') {
                uint8_t measured;
                board.orientation(measured, true);
                ESP_LOGI("read_pico",
                         "STATE clock=%d battery=%d external=%d sd=%d orientation=%u "
                         "rotation_mode=%u page=%s",
                         view.clock_valid, view.battery, board.external_power(), view.sd,
                         view.orientation, view.settings.rotation, page_name(nullptr));
            } else if (command == 'r') {
                diagnostic = false;
                force = baseline = true;
            } else if ((command >= '1' && command <= '9') || command == 't') {
                ui.render(demo_view(static_cast<char>(command), view), true);
                diagnostic = true;
            }
        }
        if (diagnostic && (ota.busy() || view.link.has_passkey)) {
            diagnostic = false;
            force = baseline = true;
        }
        location = view.page;
        if (!diagnostic && (force || view.now_ms >= next_draw)) {
            ui.render(view, baseline);
            force = false;
            baseline = false;
            reboot_drawn = ui.ready() && view.ota.phase == OtaPhase::Rebooting;
            const uint64_t intervals[] = {15000, 30000, 60000, 120000};
            next_draw = esp_timer_get_time() / 1000 +
                        (!ui.ready() ? 1000 : ota.busy() ? 60000 : intervals[view.settings.refresh]);
        }
        if (ui.ready())
            ble.verify_boot();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
