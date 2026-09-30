#include "beeper.hpp"
#include "board.hpp"
#include "canvas.hpp"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "panel_logic.hpp"
#include "screens.hpp"
#include "st7305.hpp"
#include "usage_ble/ble_service.hpp"
#include "usage_ota/backend.hpp"
#include "usage_ota/presentation.hpp"
#include <atomic>
#include <cstdio>
using namespace usage_panel;
using namespace usage_panel::rlcd;
namespace {
constexpr char kTag[] = "rlcd42";
constexpr char kAdvertisingPrefix[] = "QF-WS-S3-R42-";
constexpr uint64_t kFrameIntervalMs = 250;
constexpr uint64_t kOtaErrorVisibleMs = 15000;
static_assert(static_cast<int>(OtaPhase::Rebooting) == static_cast<int>(OtaStage::Rebooting));
static_assert(warning_level(0, 80) == 1 && warning_level(1, 78) == 1 && warning_level(1, 77) == 0);
static_assert(warning_level(0, 95) == 2 && warning_level(2, 93) == 2 && warning_level(2, 92) == 1);

AppEventQueue events;
BleService ble;
EspOtaBackend backend("ws_rlcd_42");
OtaSession ota(backend);
Board board;
Beeper beeper;
St7305 panel;
Canvas canvas;
Screens screens(canvas);
Navigator nav;
View view;
std::atomic<Page> location{Page::Home};

const char *page_name(void *) {
    switch (location.load()) {
    case Page::Codex:
        return "codex";
    case Page::Claude:
        return "claude";
    case Page::Trend:
        return "trend";
    case Page::Clock:
        return "clock";
    case Page::Settings:
        return "settings";
    default:
        return "overview";
    }
}

void apply_ota(const OtaSnapshot &s) {
    view.ota.stage = static_cast<OtaStage>(s.phase);
    view.ota.offset = s.offset;
    view.ota.size = s.size;
    snprintf(view.ota.version, sizeof(view.ota.version), "%s", s.version.c_str());
}
} // namespace

extern "C" void app_main() {
    // Preserve NVS on initialization failures; recovery must not silently erase bonds.
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(board.begin() ? ESP_OK : ESP_FAIL);
    view.settings = board.load();
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(view.device_name, sizeof(view.device_name), "%s%02X%02X", kAdvertisingPrefix, mac[4],
             mac[5]);
    view.now_ms = esp_timer_get_time() / 1000;
    board.poll(view);
    ESP_ERROR_CHECK(panel.begin() ? ESP_OK : ESP_FAIL);
    if (!beeper.begin(board.bus()))
        ESP_LOGW(kTag, "ES8311 unavailable; alerts stay silent");
    ESP_ERROR_CHECK(events.begin(32) ? ESP_OK : ESP_ERR_NO_MEM);
    // No OTA power source is registered: the board senses battery voltage only and cannot tell
    // whether USB power is present, so a voltage threshold would block updates on USB power.
    BleServiceConfig config{kAdvertisingPrefix, "Waveshare RLCD 4.2", "waveshare_rlcd_42", true,
                            &ota, page_name, nullptr, true, true};
    ESP_ERROR_CHECK(ble.start(events, config));

    uint64_t next_poll = 0, next_frame = 0, reboot_at = 0;
    RetainedOtaFailure shown_failure;
    Page clock_return = Page::Home;
    while (true) {
        view.now_ms = esp_timer_get_time() / 1000;
        bool redraw = false;
        ota.tick(view.now_ms);
        const OtaSnapshot snapshot = ota.snapshot();
        apply_ota(snapshot);
        if (retain_ota_failure(snapshot, shown_failure, view.now_ms))
            redraw = true;
        view.ota.error = shown_failure.error != OtaError::None &&
                                 view.now_ms - shown_failure.started_ms < kOtaErrorVisibleMs
                             ? ota_error_presentation(shown_failure.error).title
                             : nullptr;
        if (snapshot.phase == OtaPhase::Rebooting) {
            // Leave the RESTARTING frame on the panel briefly before the reset.
            if (!reboot_at)
                reboot_at = view.now_ms + 1000;
            else if (view.now_ms >= reboot_at)
                esp_restart();
        }

        AppEvent event{};
        while (events.receive(event)) {
            switch (event.type) {
            case AppEventType::Usage:
                view.model.apply(event.usage, view.now_ms);
                break;
            case AppEventType::Link:
                view.link = {event.link.connected, event.link.encrypted, event.link.has_passkey,
                             event.link.passkey};
                break;
            case AppEventType::TimeSync:
                if (!board.sync(event.time_sync.calendar))
                    ESP_LOGW(kTag, "RTC sync failed");
                next_poll = 0;
                break;
            case AppEventType::ScreenToggle:
                if (!ota.busy() && !view.link.has_passkey) {
                    if (view.page == Page::Clock)
                        view.page = clock_return;
                    else {
                        clock_return = view.page == Page::Settings ? Page::Home : view.page;
                        view.page = Page::Clock;
                    }
                    Navigator::normalize(view);
                }
                break;
            case AppEventType::ScreenPage:
                if (!ota.busy() && !view.link.has_passkey && view.page != Page::Settings) {
                    const bool portrait =
                        view.settings.rotation == static_cast<uint8_t>(Rotation::Portrait);
                    view.page = event.previous_page ? Navigator::previous(view.page, portrait)
                                                    : Navigator::next(view.page, portrait);
                }
                break;
            }
            redraw = true;
        }

        for (int i = 0; i < 2; ++i) {
            const auto &s = view.model.snapshot(i ? Provider::Claude : Provider::Codex);
            view.warning[i] = short_present(s, view.link.encrypted)
                                  ? warning_level(view.warning[i], s.short_window.used_percent)
                                  : 0;
        }
        const int previous_alert = view.alert;
        if (nav.update_alert(view) &&
            view.settings.alert == static_cast<uint8_t>(AlertMode::Beep))
            beeper.alert();
        redraw = redraw || previous_alert != view.alert;

        const Input input = board.key(view.now_ms);
        if (input != Input::None) {
            redraw = true;
            if (snapshot.phase == OtaPhase::Confirming) {
                if (input == Input::Key)
                    ota.confirm(view.now_ms);
                else if (input == Input::KeyHold)
                    ota.deny();
            } else if (ota.busy()) {
            } else if (view.link.has_passkey) {
                if (input == Input::Key || input == Input::Boot)
                    ble.reset_pairing();
            } else {
                const NavResult r = nav.input(input, view, view.now_ms);
                if (r.save)
                    view.save_error = !board.save(view.settings);
            }
        }
        if (!ota.busy() && !view.link.has_passkey)
            redraw = nav.auto_cycle(view, view.now_ms) || redraw;

        if (view.now_ms >= next_poll) {
            next_poll = view.now_ms + 1000;
            board.poll(view);
            board.sample_trend(view);
            redraw = true;
        }
        location = view.page;
        // Frames are composed at a fixed cadence so countdowns tick; unchanged frames are not sent.
        if (redraw || view.now_ms >= next_frame) {
            screens.render(view);
            panel.show(canvas);
            next_frame = view.now_ms + kFrameIntervalMs;
        }
        // BLE startup is asynchronous; verification retries until the service is ready.
        ble.verify_boot();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
