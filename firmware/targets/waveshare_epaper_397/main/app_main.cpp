#include "board.hpp"
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
#include <atomic>
#include <cstdio>
namespace usage_panel::epaper {
bool external_power();
}
using namespace usage_panel;
using namespace usage_panel::epaper;
namespace {
AppEventQueue events;
BleService ble;
EspOtaBackend backend("ws_epaper_397");
OtaSession ota(backend);
AtomicPowerSource power;
Board board;
DisplayUi ui;
View view;
std::atomic<Page> location{Page::Home};
constexpr char kAdvertisingPrefix[] = "QF-WS-S3-E397-";
const char *page_name(void *) {
    switch (location.load()) {
    case Page::Home:
        return "overview";
    case Page::Clock:
        return "clock";
    case Page::Trend:
        return "trend";
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
    view.portrait = view.settings.rotation == 2;
    view.orientation = view.portrait ? 1 : 0;
    if (view.settings.rotation == 0) {
        uint8_t measured;
        if (board.orientation(measured)) {
            view.orientation = measured;
            view.portrait = (measured & 1) != 0;
        }
    }
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(view.device_name, sizeof(view.device_name), "%s%02X%02X", kAdvertisingPrefix, mac[4],
             mac[5]);
    view.now_ms = esp_timer_get_time() / 1000;
    board.poll(view);
    power.publish(view.power_valid, external_power(), view.battery >= 0 ? view.battery : 0);
    ota.set_power_source(&power);
    ESP_ERROR_CHECK(events.begin(32) ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(ui.begin() ? ESP_OK : ESP_FAIL);
    BleServiceConfig config{kAdvertisingPrefix,
                            "Waveshare ePaper 3.97",
                            "waveshare_epaper_397",
                            true,
                            &ota,
                            page_name,
                            nullptr,
                            true,
                            true};
    ESP_ERROR_CHECK(ble.start(events, config));
    usb_serial_jtag_driver_config_t usb = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb));
    uint64_t next_poll = 0, next_draw = 0, last_key = view.now_ms, orientation_since = 0,
             next_orientation = 0, reboot_deadline = 0;
    bool force = true, baseline = false, diagnostic = false;
    uint8_t candidate = view.orientation;
    Page return_page = Page::Home, clock_return = Page::Home;
    OtaPhase last_phase = OtaPhase::Idle;
    int last_progress = -1;
    OtaError last_error = OtaError::None;
    uint64_t error_until = 0;
    ESP_LOGI("ws397", "READY COM USB / serial: t/u/v/w partial demos, n next, f full, b baseline, o rotate, s dump, r live");
    while (true) {
        view.now_ms = esp_timer_get_time() / 1000;
        ota.tick(view.now_ms);
        view.ota = ota.snapshot();
        if (view.ota.error != last_error) {
            last_error = view.ota.error;
            error_until = view.now_ms + 15000;
            force = true;
            // The banner lies outside the Clock partial windows and would cost two grayscale
            // baselines; the error resets the idle timer so the timeout does not reopen Clock.
            if (view.ota.error != OtaError::None && view.page == Page::Clock) {
                view.page = clock_return;
                last_key = view.now_ms;
            }
        }
        if (view.now_ms >= error_until)
            view.ota.error = OtaError::None;
        int progress = ota_progress_percent(view.ota.offset, view.ota.size) / 5;
        if (view.ota.phase != last_phase || progress != last_progress) {
            force = true;
            last_phase = view.ota.phase;
            last_progress = progress;
        }
        if (view.ota.phase == OtaPhase::Rebooting) {
            // The first Rebooting iteration submits the final frame; restart once it has refreshed.
            if (!reboot_deadline)
                reboot_deadline = view.now_ms + 20000;
            else if (ui.idle() || view.now_ms >= reboot_deadline)
                esp_restart();
        }
        AppEvent event{};
        while (events.receive(event)) {
            switch (event.type) {
            case AppEventType::Usage:
                view.model.apply(event.usage, view.now_ms);
                break;
            case AppEventType::Link:
                view.link = event.link;
                force = true;
                break;
            case AppEventType::TimeSync:
                if (!board.sync(event.time_sync.calendar))
                    ESP_LOGW("ws397", "RTC sync failed");
                next_poll = 0;
                force = true;
                break;
            case AppEventType::ScreenToggle:
                if (!ota.busy() && !view.link.has_passkey) {
                    if (view.page == Page::Clock)
                        view.page = clock_return;
                    else {
                        // Settings is never restored; the remote toggle leaves it like a key press.
                        clock_return = view.page == Page::Settings ? Page::Home : view.page;
                        view.page = Page::Clock;
                    }
                    force = true;
                    last_key = view.now_ms;
                }
                break;
            case AppEventType::ScreenPage:
                if (!ota.busy() && !view.link.has_passkey) {
                    if (view.page == Page::Clock)
                        view.page = clock_return;
                    // Home and Trend are the whole ring, so both directions are one step.
                    view.page = !view.portrait && view.page != Page::Trend ? Page::Trend : Page::Home;
                    force = true;
                    last_key = view.now_ms;
                }
                break;
            }
        }
        for (int i = 0; i < 2; ++i) {
            const auto &s = view.model.snapshot(i ? Provider::Claude : Provider::Codex);
            uint8_t next = warning(view.warning[i], s.short_window.used_percent);
            if (next != view.warning[i]) {
                view.warning[i] = next;
                force = true;
            }
        }
        int key = board.key(view.now_ms);
        if (key && ui.partial_demo_active() && !ota.busy() && !view.link.has_passkey) {
            if (key == 5) {
                ui.end_partial_demo();
                diagnostic = false;
                force = true;
            } else
                ui.partial_demo(key == 1 ? 'f' : key == 4 ? 'j' : 'n');
            key = 0;
        }
        if (key) {
            last_key = view.now_ms;
            diagnostic = false;
            force = true;
            if (view.ota.phase == OtaPhase::Confirming) {
                if (key == 2)
                    ota.confirm(view.now_ms);
                else if (key == 5)
                    ota.deny();
            } else if (ota.busy()) {
            } else if (view.link.has_passkey) {
                if (key == 2)
                    ble.reset_pairing();
            } else if (key == 4) {
                // BOOT changes nothing else, so let it renew the optical baseline.
                baseline = true;
            } else if (view.page == Page::Settings) {
                if (key == 1)
                    view.focus = (view.focus + 2) % 3;
                else if (key == 3)
                    view.focus = (view.focus + 1) % 3;
                else if (key == 5)
                    view.page = return_page;
                else if (key == 2) {
                    auto &s = view.settings;
                    if (view.focus == 0)
                        s.refresh = (s.refresh + 1) % 4;
                    else if (view.focus == 1)
                        s.idle = (s.idle + 1) % 4;
                    else
                        s.rotation = (s.rotation + 1) % 3;
                    view.save_error = !board.save(s);
                }
            } else if (key == 5) {
                return_page = view.page;
                view.page = Page::Settings;
            } else if (view.page == Page::Clock || view.page == Page::Trend)
                view.page = Page::Home;
            else if (key == 1 && !view.portrait)
                view.page = Page::Trend;
            else if (key == 3) {
                clock_return = view.page;
                view.page = Page::Clock;
            }
            else if (key == 2) {
                baseline = true;
                ESP_LOGI("ws397", "Manual display refresh; Bridge publication is host-driven");
            }
        }
        if (view.settings.rotation) {
            uint8_t p = view.settings.rotation == 2 ? 1 : 0;
            if (view.orientation != p) {
                view.orientation = p;
                view.portrait = (p & 1) != 0;
                force = true;
            }
        } else if (view.now_ms >= next_orientation && !ota.busy() && !view.link.has_passkey) {
            next_orientation = view.now_ms + 100;
            uint8_t p;
            if (board.orientation(p)) {
                if (candidate != p) {
                    candidate = p;
                    orientation_since = view.now_ms;
                } else if (p != view.orientation && view.now_ms - orientation_since >= 1500) {
                    view.orientation = p;
                    view.portrait = (p & 1) != 0;
                    force = true;
                }
            } else
                orientation_since = view.now_ms;
        }
        if (view.portrait && view.page == Page::Trend) {
            view.page = Page::Home;
            force = true;
        }
        int idle_minutes[] = {0, 5, 15, 30};
        if (!ota.busy() && !view.link.has_passkey && view.page != Page::Settings &&
            idle_minutes[view.settings.idle] &&
            view.now_ms - last_key >= idle_minutes[view.settings.idle] * 60000ULL) {
            if (view.page != Page::Clock) {
                clock_return = view.page;
                view.page = Page::Clock;
                force = true;
            }
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
            power.publish(view.power_valid, external_power(), view.battery >= 0 ? view.battery : 0);
        }
        uint8_t command = 0;
        if (usb_serial_jtag_read_bytes(&command, 1, 0) == 1 && !ota.busy()) {
            if (!view.link.has_passkey && ui.partial_demo(static_cast<char>(command))) {
                diagnostic = ui.partial_demo_active();
            } else if (command == 'i') {
                uint8_t measured;
                board.orientation(measured, true);
                ESP_LOGI("ws397",
                         "STATE clock=%d environment=%d temperature=%.1f humidity=%.1f battery=%d "
                         "external=%d portrait=%d sd=%d orientation=%u rotation_mode=%u page=%s",
                         view.clock_valid, view.environment_valid, view.temperature, view.humidity,
                         view.battery, external_power(), view.portrait, view.sd, view.orientation,
                         view.settings.rotation, page_name(nullptr));
            } else if (command == 'p') {
                ui.probe();
                diagnostic = true;
            } else if (command == 's')
                ui.dump();
            else if (command == 'r') {
                ui.end_partial_demo();
                diagnostic = false;
                force = true;
            } else if (command == 'd' || (command >= '1' && command <= '9')) {
                View demo = view;
                demo.page = Page::Home;
                demo.portrait = false;
                demo.orientation = 0;
                demo.link = {true, true, false, 0};
                demo.clock_valid = true;
                demo.calendar.year = 2026;
                demo.calendar.month = 9;
                demo.calendar.day = 12;
                demo.calendar.weekday = 6;
                demo.calendar.hour = 14;
                demo.calendar.minute = 32;
                for (int i = 0; i < 2; ++i) {
                    UsageUpdate u;
                    u.provider = i ? Provider::Claude : Provider::Codex;
                    u.state = SourceState::Ok;
                    u.sampled_at = 1789194600;
                    u.sent_at = u.sampled_at;
                    u.short_window = {true, static_cast<uint8_t>(i ? 86 : 42), true,
                                      u.sent_at + 8040};
                    u.week_window = {true, static_cast<uint8_t>(i ? 63 : 28), true,
                                     u.sent_at + 86400};
                    demo.model.apply(u, demo.now_ms);
                    demo.warning[i] = i ? 1 : 0;
                }
                if (command == '2') {
                    demo.page = Page::Trend;
                    demo.sd = true;
                    demo.trend_count = 48;
                    for (int i = 0; i < 48; ++i)
                        demo.trend[i] = {static_cast<uint32_t>(1789110000 + i * 1800),
                                         static_cast<uint8_t>((i * 3) % 98),
                                         static_cast<uint8_t>((i * 2 + 12) % 98),
                                         {}};
                }
                if (command == '3') {
                    UsageUpdate u;
                    u.provider = Provider::Claude;
                    u.state = SourceState::Ok;
                    u.sampled_at = 1789194600;
                    u.sent_at = u.sampled_at;
                    u.short_window = {true, 96, true, u.sent_at + 1000};
                    u.week_window = {true, 63, true, u.sent_at + 86400};
                    demo.model.apply(u, demo.now_ms);
                    demo.warning[1] = 2;
                }
                if (command == '4') {
                    static unsigned clock_step = 0;
                    const unsigned step = clock_step++ % 4;
                    demo.calendar.minute += step;
                    demo.page = Page::Clock;
                    demo.environment_valid = true;
                    demo.temperature = step >= 2 ? 25.5f : 24.5f;
                    demo.humidity = step >= 3 ? 52 : 48;
                }
                if (command == '5') {
                    demo.portrait = true;
                    demo.orientation = 1;
                }
                if (command == '6') {
                    static int focus = 0;
                    demo.page = Page::Settings;
                    demo.focus = focus++ % 3;
                }
                if (command == '7')
                    demo.link = {};
                if (command == '8')
                    demo.link = {true, false, true, 418502};
                if (command == '9') {
                    demo.ota.phase = OtaPhase::Receiving;
                    demo.ota.version = "v0.6.0";
                    demo.ota.size = 1600000;
                    demo.ota.offset = 960000;
                }
                ui.render(demo, true);
                diagnostic = true;
            }
        }
        if (diagnostic && (ota.busy() || view.link.has_passkey)) {
            ui.end_partial_demo();
            diagnostic = false;
            force = true;
        }
        location = view.page;
        if (!diagnostic && (force || view.now_ms >= next_draw)) {
            ui.render(view, force, baseline);
            force = false;
            baseline = false;
            const uint64_t intervals[] = {15000, 30000, 60000, 120000};
            next_draw = view.now_ms + (ota.busy() ? 60000 : intervals[view.settings.refresh]);
        }
        if (ui.ready())
            ble.verify_boot();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
