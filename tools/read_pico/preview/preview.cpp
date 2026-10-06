// Renders the nine design boards and the gray-level test card with the firmware drawing code, one PGM per board and rotation.
#include "demo_view.hpp"
#include "epdiy.h"
#include <cstdio>
#include <cstring>
#include <string>
namespace {
constexpr int kWidth = 1216, kHeight = 684;
uint8_t front[kWidth / 2 * kHeight], back[kWidth / 2 * kHeight];
EpdRotation rotation = EPD_ROT_LANDSCAPE;
int refreshes = 0, failures = 0;
EpdDrawMode last_mode = MODE_GC16;
EpdDrawError refresh(EpdiyHighlevelState *state, EpdDrawMode mode) {
    ++refreshes;
    last_mode = mode;
    // epdiy advances its back buffer even when the panel update fails.
    memcpy(state->back_fb, state->front_fb, sizeof(front));
    if (failures) {
        --failures;
        return EPD_DRAW_EMPTY_LINE_QUEUE;
    }
    return EPD_DRAW_SUCCESS;
}
// Same mapping as epdiy's _rotate().
void to_native(int &x, int &y) {
    const int lx = x, ly = y;
    switch (rotation) {
    case EPD_ROT_LANDSCAPE:
        break;
    case EPD_ROT_PORTRAIT:
        x = kWidth - ly - 1;
        y = lx;
        break;
    case EPD_ROT_INVERTED_LANDSCAPE:
        x = kWidth - lx - 1;
        y = kHeight - ly - 1;
        break;
    case EPD_ROT_INVERTED_PORTRAIT:
        x = ly;
        y = kHeight - lx - 1;
        break;
    }
}
} // namespace
extern "C" {
int epd_width(void) { return kWidth; }
int epd_height(void) { return kHeight; }
void epd_set_rotation(EpdRotation r) { rotation = r; }
void epd_draw_pixel(int x, int y, uint8_t color, uint8_t *fb) {
    to_native(x, y);
    if (x < 0 || x >= kWidth || y < 0 || y >= kHeight)
        return;
    uint8_t &b = fb[y * kWidth / 2 + x / 2];
    b = x % 2 ? (b & 0x0F) | (color & 0xF0) : (b & 0xF0) | (color >> 4);
}
void epd_poweron(void) {}
void epd_poweroff(void) {}
void epd_clear(void) {}
void epd_lcd_set_prefill_lines(int) {}
uint8_t *epd_hl_get_framebuffer(EpdiyHighlevelState *s) { return s->front_fb; }
void epd_hl_set_all_white(EpdiyHighlevelState *s) { memset(s->front_fb, 0xFF, sizeof(front)); }
EpdDrawError epd_hl_update_screen(EpdiyHighlevelState *s, EpdDrawMode m, int) { return refresh(s, m); }
EpdDrawError epd_hl_update_screen_full(EpdiyHighlevelState *s, EpdDrawMode m, int) { return refresh(s, m); }
EpdDrawError epd_hl_update_screen_from_white(EpdiyHighlevelState *s, EpdDrawMode m, int) { return refresh(s, m); }
}
int main(int argc, char **argv) {
    using namespace usage_panel::read_pico;
    const std::string out = argc > 1 ? argv[1] : ".";
    EpdiyHighlevelState state{front, back};
    DisplayUi ui;
    ui.begin(&state);
    View live{};
    snprintf(live.device_name, sizeof(live.device_name), "QF-MR-PICO-3C7A");
    // Each board is rendered upright and, with the suffix "l", turned to landscape.
    // The test card is portrait only.
    for (int n = 0; n < 19; ++n) {
        const char board = "123456789t"[n / 2];
        View v = demo_view(board, live);
        v.orientation = n % 2;
        ui.render(v, true);
        const bool portrait = !(v.orientation & 1);
        const int w = portrait ? kHeight : kWidth, h = portrait ? kWidth : kHeight;
        FILE *f = fopen((out + "/board" + board + (n % 2 ? "l" : "") + ".pgm").c_str(), "wb");
        if (!f)
            return 1;
        fprintf(f, "P5\n%d %d\n255\n", w, h);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                int nx = x, ny = y;
                to_native(nx, ny);
                const uint8_t b = front[ny * kWidth / 2 + nx / 2];
                fputc((nx % 2 ? b >> 4 : b & 0x0F) * 17, f);
            }
        fclose(f);
    }
    // Every control drawn on the settings and confirmation boards must be reachable by touch.
    const View settings = demo_view('7', live), confirm = demo_view('9', live);
    const bool ok = ui.hit(settings, 342, 1128).kind == TapTarget::Done &&
                    ui.hit(settings, 600, 750).kind == TapTarget::Option &&
                    ui.hit(settings, 600, 750).row == 2 && ui.hit(settings, 600, 750).index == 2 &&
                    ui.hit(confirm, 100, 1080).kind == TapTarget::OtaDeny &&
                    ui.hit(confirm, 600, 1080).kind == TapTarget::OtaConfirm &&
                    ui.hit(settings, 342, 60).kind == TapTarget::None;
    puts(ok ? "hit tests passed" : "HIT TESTS FAILED");
    if (!ok)
        return 1;

    DisplayUi recovering;
    recovering.begin(&state);
    refreshes = 0;
    failures = 2;
    recovering.render(settings);
    if (recovering.ready() || refreshes != 2)
        return 1;
    recovering.render(settings);
    if (!recovering.ready() || refreshes != 3)
        return 1;
    recovering.render(settings);
    if (refreshes != 3)
        return 1;
    failures = 1;
    recovering.render(settings, true);
    if (!recovering.ready() || refreshes != 5)
        return 1;
    failures = 2;
    recovering.render(settings, true);
    if (recovering.ready() || refreshes != 7)
        return 1;
    puts("refresh failure and recovery checks passed");

    View clock = live;
    clock.page = Page::Clock;
    clock.link = {true, true, false, 0};
    ui.render(clock);
    const std::string no_short(reinterpret_cast<char *>(front), sizeof(front));
    usage_panel::UsageUpdate week{};
    week.state = usage_panel::SourceState::Partial;
    week.week_window = {true, 87, false, 0};
    if (!clock.model.apply(week, clock.now_ms))
        return 1;
    ui.render(clock);
    // Without a short window the clock summary shows the week window.
    if (!memcmp(no_short.data(), front, sizeof(front)))
        return 1;
    puts("week-only clock check passed");

    // Text-only updates use DU; a changed historical curve can require a gray refresh.
    View home = demo_view('1', live);
    home.trend[46].claude = 90;
    ui.render(home, true);
    ++home.calendar.minute;
    ui.render(home);
    if (last_mode != MODE_DU)
        return 1;
    home.trend[46].claude = 10;
    ui.render(home);
    if (last_mode != MODE_GL16)
        return 1;
    for (int orientation : {0, 1}) {
        View scrolling = demo_view('1', live);
        scrolling.orientation = orientation;
        // Start after the day-to-hour countdown reflow, which resizes the landscape gray bar.
        scrolling.now_ms += 120000;
        ui.render(scrolling, true);
        for (int step = 1; step <= 10; ++step) {
            scrolling.now_ms += 30000;
            scrolling.calendar.minute = 32 + step / 2;
            const int before = refreshes;
            ui.render(scrolling);
            if (refreshes != before && last_mode != MODE_DU)
                return 1;
        }
    }
    View alarm = demo_view('2', live);
    ui.render(alarm, true);
    alarm.now_ms += 60000;
    ui.render(alarm);
    if (last_mode != MODE_GL16)
        return 1;
    puts("refresh mode checks passed");

    // Expired, future and undated history must render like an empty chart, including its labels.
    for (int scenario = 0; scenario < 4; ++scenario) {
        View history = demo_view('1', live);
        history.link = {};
        if (scenario == 0)
            history.now_ms += 48 * 3600000ULL;
        else {
            history.model = {};
            const uint32_t last = history.trend[history.trend_count - 1].epoch;
            history.utc_epoch = scenario == 1 ? last + 48 * 3600
                                : scenario == 2 ? history.trend[0].epoch - 1 : 0;
        }
        ui.render(history);
        const std::string actual(reinterpret_cast<char *>(front), sizeof(front));
        history.trend_count = 0;
        ui.render(history);
        if (memcmp(actual.data(), front, sizeof(front)))
            return 1;
    }
    // A restored RTC can date SD history before the first Bridge publication.
    View restored = demo_view('1', live);
    restored.link = {};
    restored.model = {};
    restored.utc_epoch = restored.trend[restored.trend_count - 1].epoch;
    ui.render(restored);
    const std::string recent(reinterpret_cast<char *>(front), sizeof(front));
    restored.trend_count = 0;
    ui.render(restored);
    if (!memcmp(recent.data(), front, sizeof(front)))
        return 1;
    puts("trend clock and expiry checks passed");

    // The gray snapshot stays fixed between stored samples while its age labels advance.
    auto trend_pixels = [&](const View &v, bool labels = true) {
        ui.render(v);
        std::string pixels;
        for (int y = labels ? 760 : 830; y < (labels ? kWidth : 1141); ++y)
            for (int x = labels ? 0 : 40; x < (labels ? kHeight : 505); ++x) {
                int nx = x, ny = y;
                to_native(nx, ny);
                const uint8_t b = front[ny * kWidth / 2 + nx / 2];
                pixels += static_cast<char>(nx % 2 ? b >> 4 : b & 0x0F);
            }
        return pixels;
    };
    View latest = demo_view('1', live);
    const std::string before = trend_pixels(latest);
    usage_panel::UsageUpdate update{};
    update.provider = usage_panel::Provider::Claude;
    update.state = usage_panel::SourceState::Partial;
    update.sent_at = update.sampled_at = latest.model.snapshot(update.provider).sampled_at;
    update.short_window = {true, 75, false, 0};
    if (!latest.model.apply(update, latest.now_ms) || trend_pixels(latest) != before)
        return 1;
    const std::string online = trend_pixels(latest);
    latest.link = {};
    if (trend_pixels(latest) != online)
        return 1;
    const std::string curve = trend_pixels(latest, false);
    latest.now_ms += 20 * 60000;
    if (trend_pixels(latest) == online || trend_pixels(latest, false) != curve)
        return 1;
    // Week-only publications leave the short-window chart on dated history.
    update.short_window = {};
    update.week_window = {true, 30, false, 0};
    latest.model.apply(update, latest.now_ms);
    const std::string week_only = trend_pixels(latest);
    update.week_window.used_percent = 90;
    latest.model.apply(update, latest.now_ms);
    if (trend_pixels(latest) != week_only)
        return 1;
    latest.now_ms += 10 * 60000;
    latest.trend[47].epoch += 1800;
    latest.trend[47].claude = 75;
    if (trend_pixels(latest, false) == curve)
        return 1;
    puts("stored trend snapshot, offline age and half-hour update checks passed");
    return 0;
}
