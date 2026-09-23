#include "display_ui.hpp"
#include <cstdio>
#include "esp_log.h"
#include "sdkconfig.h"
#include "usage_ota/presentation.hpp"

namespace usage_panel {
namespace {
constexpr char TAG[] = "display_ui";

// The panel UI is laid out for upright portrait; the menu only offers
// upright and inverted orientations for stands that mount it flipped.
// M5GFX takes a rotation index (0 upright, 2 inverted), not degrees.
#ifdef CONFIG_M5_USAGE_PANEL_ROTATION_180
constexpr uint8_t kDisplayRotation = 2;
#else
constexpr uint8_t kDisplayRotation = 0;
#endif
}  // namespace

void DisplayUi::begin()
{
    M5.Display.setRotation(kDisplayRotation);
    M5.Display.setBrightness(
        CONFIG_M5_USAGE_PANEL_DISPLAY_BRIGHTNESS_PERCENT * 255 / 100);
    draw_target_ = &M5.Display;
    canvas_.setColorDepth(16);
    canvas_ready_ =
        canvas_.createSprite(M5.Display.width(), M5.Display.height()) != nullptr;
    if (canvas_ready_) {
        draw_target_ = &canvas_;
    } else {
        ESP_LOGE(TAG, "full-screen canvas allocation failed; using direct rendering");
    }

    auto& display = target();
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setTextDatum(top_left);
    display.fillScreen(TFT_BLACK);
}

uint16_t DisplayUi::usage_color(uint8_t percent) const
{
    return percent >= 90 ? TFT_RED : percent >= 70 ? TFT_ORANGE : TFT_GREEN;
}

void DisplayUi::progress(int x, int y, int width, uint8_t percent, uint16_t color)
{
    auto& display = target();
    display.drawRect(x, y, width, 8, TFT_DARKGREY);
    display.fillRect(x + 1, y + 1, (width - 2) * percent / 100, 6, color);
}

void DisplayUi::provider_card(const UsageModel& model, Provider provider, int y,
                              bool connected, uint64_t now_ms)
{
    const auto& data = model.snapshot(provider);
    const auto state = model.display_state(provider, connected);
    auto& display = target();
    display.drawRoundRect(5, y, 125, 78, 6, TFT_DARKGREY);
    display.setTextSize(1);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setCursor(12, y + 7);
    display.print(provider == Provider::Codex ? "CODEX" : "CLAUDE");
    display.setTextColor(state == DisplayState::Online ? TFT_GREEN : TFT_LIGHTGREY, TFT_BLACK);
    display.setCursor(58, y + 7); display.print(display_state_name(state));
    if (!data.has_valid_data) return;
    const bool fresh = state == DisplayState::Online || state == DisplayState::Partial;
    display.setTextColor(fresh ? TFT_WHITE : TFT_DARKGREY, TFT_BLACK);
    display.setCursor(12, y + 21);
    if (data.short_window.present &&
        (data.source_state != SourceState::Partial || data.latest_short_present)) {
        display.printf("SHORT %u%%", data.short_window.used_percent);
        progress(12, y + 35, 108, data.short_window.used_percent,
                 fresh ? usage_color(data.short_window.used_percent) : TFT_DARKGREY);
    } else {
        display.print("SHORT --");
        progress(12, y + 35, 108, 0, TFT_DARKGREY);
    }
    display.setCursor(12, y + 49);
    if (data.week_window.present &&
        (data.source_state != SourceState::Partial || data.latest_week_present)) {
        display.printf("WEEK %u%%", data.week_window.used_percent);
        progress(12, y + 63, 108, data.week_window.used_percent,
                 fresh ? usage_color(data.week_window.used_percent) : TFT_DARKGREY);
    } else {
        display.print("WEEK --");
        progress(12, y + 63, 108, 0, TFT_DARKGREY);
    }
}

void DisplayUi::provider_page(const UsageModel& model, Provider provider,
                              bool connected, uint64_t now_ms)
{
    const auto& data = model.snapshot(provider);
    const auto state = model.display_state(provider, connected);
    auto& display = target();
    display.setTextSize(2); display.setCursor(8, 10);
    display.print(provider == Provider::Codex ? "CODEX" : "CLAUDE");
    display.setTextSize(1); display.setCursor(8, 34);
    display.print(display_state_name(state));
    if (!data.has_valid_data) return;
    const bool fresh = state == DisplayState::Online || state == DisplayState::Partial;
    const uint32_t epoch = model.estimated_epoch(provider, now_ms);
    char countdown[16]{};
    UsageWindow windows[] = {data.short_window, data.week_window};
    if (data.source_state == SourceState::Partial) {
        windows[0].present = data.latest_short_present;
        windows[1].present = data.latest_week_present;
    }
    const char* labels[] = {"SHORT", "WEEK"};
    for (int i = 0; i < 2; ++i) {
        const int y = 58 + i * 78;
        display.setTextSize(1); display.setCursor(8, y); display.print(labels[i]);
        display.setTextSize(3);
        display.setTextColor(windows[i].present && fresh ? usage_color(windows[i].used_percent)
                                                : TFT_LIGHTGREY,
                             TFT_BLACK);
        display.setCursor(8, y + 15);
        if (windows[i].present) display.printf("%u%%", windows[i].used_percent);
        else display.print("--");
        progress(8, y + 47, 119,
                 windows[i].present ? windows[i].used_percent : 0,
                 windows[i].present && fresh ? usage_color(windows[i].used_percent) : TFT_DARKGREY);
        format_countdown(windows[i].present && windows[i].has_reset,
                         windows[i].reset_at, epoch, countdown, sizeof(countdown));
        display.setTextSize(1);
        display.setTextColor(fresh ? TFT_LIGHTGREY : TFT_DARKGREY, TFT_BLACK);
        display.setCursor(8, y + 60); display.printf("RESET %s", countdown);
    }
}

void DisplayUi::ota_page(
    const OtaSnapshot& ota, uint32_t confirm_seconds, bool show_error)
{
    auto& display = target();
    display.fillScreen(TFT_BLACK);
    display.setTextDatum(top_center);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setTextSize(2);
    display.drawString("FIRMWARE", display.width() / 2, 20);

    if (show_error) {
        const OtaErrorPresentation message =
            ota_error_presentation(ota.error);
        display.setTextColor(TFT_RED, TFT_BLACK);
        display.drawString(message.title, display.width() / 2, 88);
        display.setTextSize(1);
        display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        display.drawString(message.detail, display.width() / 2, 122);
    } else if (ota.phase == OtaPhase::Confirming) {
        display.setTextSize(1);
        display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        display.drawString("Update to", display.width() / 2, 61);
        const uint8_t version_size =
            ota.version.size() * 12 <= display.width() ? 2 : 1;
        display.setTextSize(version_size);
        display.setTextColor(TFT_CYAN, TFT_BLACK);
        display.drawString(
            ota_fit_text(
                ota.version.c_str(), display.width() / (6 * version_size))
                .c_str(),
            display.width() / 2, 83);
        display.setTextSize(1);
        display.setTextColor(TFT_WHITE, TFT_BLACK);
        display.drawString("BtnA confirm", display.width() / 2, 124);
        display.drawString("BtnB cancel", display.width() / 2, 140);
        char countdown[24]{};
        snprintf(countdown, sizeof(countdown), "%lus remaining",
                 static_cast<unsigned long>(confirm_seconds));
        display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        display.drawString(countdown, display.width() / 2, 156);
    } else if (ota.phase == OtaPhase::Receiving) {
        const uint8_t percent = ota_progress_percent(ota.offset, ota.size);
        char value[16]{};
        snprintf(value, sizeof(value), "%u%%", percent);
        display.setTextSize(3);
        display.setTextColor(TFT_CYAN, TFT_BLACK);
        display.drawString(value, display.width() / 2, 70);
        display.setTextDatum(top_left);
        progress(8, 123, display.width() - 16, percent, TFT_CYAN);
        display.setTextDatum(top_center);
        display.setTextSize(1);
        display.setTextColor(TFT_ORANGE, TFT_BLACK);
        display.drawString("DO NOT POWER OFF", display.width() / 2, 153);
    } else {
        display.setTextSize(2);
        display.setTextColor(TFT_CYAN, TFT_BLACK);
        display.drawString(
            ota.phase == OtaPhase::Verifying ? "VERIFYING" : "REBOOTING",
            display.width() / 2, 91);
        display.setTextSize(1);
        display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        display.drawString("Please wait", display.width() / 2, 130);
    }
    display.setTextDatum(top_left);
}

void DisplayUi::battery_icon(const m5::BatteryView& battery)
{
    if (!battery.available) return;
    auto& display = target();
    constexpr int kX = 106, kY = 8, kW = 20, kH = 10;
    const uint16_t fill = battery.band <= 1 ? TFT_ORANGE : TFT_GREEN;
    display.drawRect(kX, kY, kW, kH, TFT_DARKGREY);
    display.fillRect(kX + kW, kY + 3, 2, 4, TFT_DARKGREY);
    for (int i = 0; i < static_cast<int>(battery.band); ++i) {
        display.fillRect(kX + 2 + i * 4, kY + 2, 3, kH - 4, fill);
    }
    // External-power glyph sits outside the shell, mirroring the
    // ESP32-S3-Touch-AMOLED-2.16 layout where LV_SYMBOL_CHARGE is drawn left
    // of the battery body.
    if (battery.status == m5::BatteryStatus::Battery) return;
    constexpr int gX = kX - 11;
    const uint16_t tint =
        battery.status == m5::BatteryStatus::Charging ? TFT_WHITE : TFT_DARKGREY;
    display.drawLine(gX + 3, kY, gX, kY + 4, tint);
    display.drawLine(gX, kY + 4, gX + 3, kY + 4, tint);
    display.drawLine(gX + 3, kY + 4, gX + 1, kY + 8, tint);
}

void DisplayUi::render(const UsageModel& model, Page page, bool connected,
                       bool has_passkey, uint32_t passkey, uint64_t now_ms,
                       const OtaSnapshot& ota, uint32_t confirm_seconds,
                       bool show_ota_error,
                       const m5::BatteryView& battery)
{
    auto& display = target();
    display.fillScreen(TFT_BLACK);
    if (ota.phase != OtaPhase::Idle || show_ota_error) {
        ota_page(ota, confirm_seconds, show_ota_error);
    } else if (page == Page::Overview) {
        display.setTextSize(2); display.setCursor(7, 5); display.print("USAGE");
        provider_card(model, Provider::Codex, 35, connected, now_ms);
        provider_card(model, Provider::Claude, 120, connected, now_ms);
        battery_icon(battery);
    } else {
        provider_page(model, page == Page::Codex ? Provider::Codex : Provider::Claude,
                      connected, now_ms);
        battery_icon(battery);
    }
    if (ota.phase == OtaPhase::Idle && !show_ota_error) {
        display.setTextSize(1); display.setTextColor(TFT_DARKGREY, TFT_BLACK);
        display.setCursor(49, 226);
        display.print(page == Page::Overview ? "o  .  ." : page == Page::Codex ? ".  o  ." : ".  .  o");
    }
    if (has_passkey && ota.phase == OtaPhase::Idle && !show_ota_error) {
        display.fillRoundRect(8, 72, 119, 92, 8, TFT_NAVY);
        display.setTextColor(TFT_WHITE, TFT_NAVY); display.setTextSize(2);
        display.setCursor(25, 84); display.print("PAIRING");
        display.setTextSize(3); display.setCursor(18, 111); display.printf("%06lu", (unsigned long)passkey);
        display.setTextSize(1); display.setCursor(26, 146); display.print("Enter on computer");
    }
    if (canvas_ready_) canvas_.pushSprite(0, 0);
}
}  // namespace usage_panel
