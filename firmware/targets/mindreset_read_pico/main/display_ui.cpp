#include "display_ui.hpp"
#include "epdiy.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "fonts.hpp"
#include "read_pico_board.h"
#include "read_pico_epd_timing.h"
#include "usage_ota/presentation.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
namespace usage_panel::read_pico {
namespace {
// The panel gray levels in use; epdiy reads the upper nibble. The panel renders light
// grays close to paper, so tracks, rules and washes use darker levels than an emissive display.
// kSub: labels and secondary text. kDim: offline values and the secondary bar fill.
// kRule: rules, dashes and offline bar fills. kWash: alarm background. kTint: chart 80-100% band.
constexpr uint8_t kInk = 0x00, kSub = 0x20, kDim = 0x40, kRule = 0x60, kTrack = 0x90,
                  kWash = 0xC0, kTint = 0xD0, kPaper = 0xF0;
// DU and GL16 updates accumulate ghosting; a GC16 refresh renews the optical baseline.
constexpr unsigned kSoftRefreshesPerBaseline = 14;
// Rail power-down discharges for 500 ms, so the rails stay up across a burst of refreshes.
constexpr uint64_t kRailsIdleMs = 8000;
constexpr int kPanelTemperatureC = 25;
constexpr int kTouchWidth = 684, kTouchHeight = 1216;
uint32_t trend_now(const View &v) {
    return v.utc_epoch ? v.utc_epoch
                       : std::max(v.model.estimated_epoch(Provider::Codex, v.now_ms),
                                  v.model.estimated_epoch(Provider::Claude, v.now_ms));
}
uint32_t trend_end(const View &v) {
    return v.sd && v.trend_count ? v.trend[v.trend_count - 1].epoch : 0;
}
void trend_age(uint32_t now, uint32_t epoch, char *out, size_t size) {
    const uint32_t minutes = now > epoch ? (now - epoch) / 60 : 0;
    if (!minutes)
        snprintf(out, size, "NOW");
    else if (minutes < 60)
        snprintf(out, size, "%luM AGO", static_cast<unsigned long>(minutes));
    else
        snprintf(out, size, "%luH %02luM AGO", static_cast<unsigned long>(minutes / 60),
                 static_cast<unsigned long>(minutes % 60));
}
void trend_time(const View &v, char *out, size_t size) {
    const uint32_t epoch = trend_end(v);
    if (!epoch) {
        snprintf(out, size, "--:--");
    } else if (!v.clock_valid) {
        trend_age(trend_now(v), epoch, out, size);
    } else {
        const time_t local = static_cast<time_t>(epoch) + v.calendar.utc_offset_minutes * 60;
        tm t{};
        gmtime_r(&local, &t);
        snprintf(out, size, "%02d:%02d", t.tm_hour, t.tm_min);
    }
}
struct Box {
    int x, y, w, h;
    bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};
Box option_box(int width, int height, int row, int index) {
    const bool portrait = height > width;
    const int count = row == 2 ? 3 : 4;
    const int left = portrait ? 40 : 380, top = portrait ? 224 + row * 242 : 113 + row * 142;
    const int w = (width - 40 - left - (count - 1) * 10) / count;
    return {left + index * (w + 10), top, w, 82};
}
Box done_box(int width, int height) {
    return height > width ? Box{40, height - 136, width - 80, 96}
                          : Box{40, height - 120, width - 80, 84};
}
Box ota_button(int width, int height, int index) {
    const int w = (width - 80 - 14) / 2;
    return {40 + index * (w + 14), height - (height > width ? 40 : 36) - 140, w, 96};
}
int scene_of(const View &v) {
    if (v.test_card)
        return 6;
    if (v.ota.phase == OtaPhase::Confirming)
        return 5;
    if (v.ota.phase != OtaPhase::Idle)
        return 3;
    if (v.link.has_passkey)
        return 4;
    return static_cast<int>(v.page);
}
const Font *font_for(int size, int family) {
    const Font *f = &fonts[0];
    int distance = 10000;
    for (int i = 0; i < font_count; ++i) {
        int d = abs(fonts[i].size - size) + (fonts[i].family != family ? 1000 : 0);
        if (d < distance) {
            distance = d;
            f = &fonts[i];
        }
    }
    return f;
}
const Glyph *glyph_for(const Font *f, unsigned char c, bool uppercase) {
    if (uppercase && c >= 'a' && c <= 'z')
        c -= 32;
    for (int i = 0; i < f->count; ++i)
        if (f->glyphs[i].character == c)
            return &f->glyphs[i];
    return nullptr;
}
// Width of the inked run; the spacing after the last glyph is excluded.
int text_width(const char *s, int size, int family, int spacing) {
    const Font *f = font_for(size, family);
    int width = 0;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(s); *p; ++p)
        if (const Glyph *g = glyph_for(f, *p, true))
            width += g->advance + spacing;
    return width ? width - spacing : 0;
}
// True when every changed pixel turns to paper or to a level the threshold DU waveform shows as
// ink. Changes to a lighter gray, such as text redrawn on an alarm wash, need a gray waveform.
bool du_reaches(const uint8_t *front, const uint8_t *back, size_t size) {
    constexpr uint8_t kDarkest = kDim >> 4, kWhite = kPaper >> 4;
    for (size_t i = 0; i < size; ++i) {
        if (front[i] == back[i])
            continue;
        for (int shift = 0; shift <= 4; shift += 4) {
            const uint8_t to = front[i] >> shift & 0x0F;
            if (to != (back[i] >> shift & 0x0F) && to > kDarkest && to != kWhite)
                return false;
        }
    }
    return true;
}
// Height of the digits, which text() places with their top edge at y.
int digit_height(int size, int family) { return glyph_for(font_for(size, family), '0', false)->height; }
void format_clock(const View &v, char *time, size_t time_size, char *date, size_t date_size) {
    static const char *days[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char *months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                   "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    if (!v.clock_valid) {
        snprintf(time, time_size, "--:--");
        snprintf(date, date_size, "CLOCK NOT SET");
        return;
    }
    snprintf(time, time_size, "%02u:%02u", v.calendar.hour, v.calendar.minute);
    snprintf(date, date_size, "%s / %s %u", days[v.calendar.weekday % 7],
             months[(v.calendar.month + 11) % 12], v.calendar.day);
}
} // namespace
struct DisplayUi::WindowSummary {
    const char *label;
    bool present, warn;
    int percent;
    char reset[32];
};
struct DisplayUi::ServiceSummary {
    const char *name;
    // none: no window can be shown. tag: alarm label, or nullptr below the alarm level.
    bool none, offline;
    const char *tag;
    // The short window leads; the week window takes its place when no short window is reported.
    WindowSummary primary, secondary;
    char age[16];
};
DisplayUi::ServiceSummary DisplayUi::summarize(const View &v, int index) {
    const Provider provider = index ? Provider::Claude : Provider::Codex;
    const auto &s = v.model.snapshot(provider);
    const bool online = v.link.encrypted;
    const uint32_t epoch = v.model.estimated_epoch(provider, v.now_ms);
    // Offline keeps the last valid sample; online shows only what the latest sample reported.
    const auto window = [&](const char *label, const UsageWindow &w, bool latest) {
        WindowSummary out{};
        out.label = label;
        out.present = s.has_valid_data && w.present && (latest || !online);
        out.percent = w.used_percent;
        out.warn = out.present && online && w.used_percent >= 80 && w.used_percent < 95;
        char countdown[24];
        format_countdown(w.has_reset, w.reset_at, epoch, countdown, sizeof(countdown));
        if (!out.present)
            snprintf(out.reset, sizeof(out.reset), "--");
        else if (!online || !w.has_reset)
            snprintf(out.reset, sizeof(out.reset), "RESET --");
        else if (!strcmp(countdown, "WAIT"))
            snprintf(out.reset, sizeof(out.reset), "RESET WAIT");
        else
            snprintf(out.reset, sizeof(out.reset), "RESETS %s", countdown);
        return out;
    };
    const WindowSummary brief = window("SHORT", s.short_window, s.latest_short_present),
                        week = window("WEEK", s.week_window, s.latest_week_present);
    ServiceSummary r{};
    r.name = index ? "CLAUDE" : "CODEX";
    r.offline = !online;
    r.primary = brief.present ? brief : week;
    r.secondary = brief.present ? week : brief;
    r.none = !r.primary.present;
    if (!r.none && online && v.warning[index] == 2)
        r.tag = std::max(r.primary.percent, r.secondary.present ? r.secondary.percent : 0) >= 100
                    ? "EXHAUSTED"
                    : "NEAR LIMIT";
    const uint32_t age = epoch > s.sampled_at ? epoch - s.sampled_at : 0;
    // Minute granularity keeps the label from changing the frame on every redraw.
    if (r.none)
        snprintf(r.age, sizeof(r.age), "NO DATA");
    else if (age < 60)
        snprintf(r.age, sizeof(r.age), "NOW");
    else
        snprintf(r.age, sizeof(r.age), "%luM AGO", static_cast<unsigned long>(age / 60));
    return r;
}
void DisplayUi::begin(EpdiyHighlevelState *state) {
    state_ = state;
    pixels_ = epd_hl_get_framebuffer(state);
}
void DisplayUi::tick(uint64_t now_ms) {
    if (rails_deadline_ms_ && now_ms >= rails_deadline_ms_) {
        rails_deadline_ms_ = 0;
        epd_poweroff();
    }
}
void DisplayUi::pixel(int x, int y, uint8_t c) {
    if (x >= 0 && y >= 0 && x < width_ && y < height_)
        epd_draw_pixel(x, y, c, pixels_);
}
void DisplayUi::rect(int x, int y, int w, int h, uint8_t c) {
    for (int j = std::max(0, y); j < std::min(height_, y + h); ++j)
        for (int i = std::max(0, x); i < std::min(width_, x + w); ++i)
            epd_draw_pixel(i, j, c, pixels_);
}
void DisplayUi::frame(int x, int y, int w, int h, uint8_t c) {
    rect(x, y, w, 2, c);
    rect(x, y + h - 2, w, 2, c);
    rect(x, y, 2, h, c);
    rect(x + w - 2, y, 2, h, c);
}
void DisplayUi::line(int x, int y, int x2, int y2, uint8_t c, int thick, bool dash) {
    int dx = abs(x2 - x), sx = x < x2 ? 1 : -1, dy = -abs(y2 - y), sy = y < y2 ? 1 : -1,
        e = dx + dy;
    for (;;) {
        // Dashes are phased by position so that consecutive short segments form one pattern.
        if (!dash || (dx >= -dy ? x : y) % 16 < 9)
            rect(x - thick / 2, y - thick / 2, thick, thick, c);
        if (x == x2 && y == y2)
            break;
        int e2 = 2 * e;
        if (e2 >= dy) {
            e += dy;
            x += sx;
        }
        if (e2 <= dx) {
            e += dx;
            y += sy;
        }
    }
}
// y is the top of the digit height; the return value is the pen advance.
int DisplayUi::text(int x, int y, const char *s, int size, uint8_t c, int family, int spacing,
                    bool uppercase) {
    const Font *f = font_for(size, family);
    int start = x;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(s); *p; ++p) {
        const Glyph *g = glyph_for(f, *p, uppercase);
        if (!g)
            continue;
        for (int j = 0; j < g->height; ++j)
            for (int i = 0; i < g->width; ++i) {
                int n = j * g->width + i;
                if (f->bitmap[g->offset + n / 8] & (128 >> (n % 8)))
                    pixel(x + g->left + i, y + g->top + j, c);
            }
        x += g->advance + spacing;
    }
    return x - start;
}
void DisplayUi::text_right(int right, int y, const char *s, int size, uint8_t c, int family,
                           int spacing) {
    text(right - text_width(s, size, family, spacing), y, s, size, c, family, spacing);
}
void DisplayUi::text_center(int x, int w, int y, const char *s, int size, uint8_t c, int family,
                            int spacing) {
    text(x + (w - text_width(s, size, family, spacing)) / 2, y, s, size, c, family, spacing);
}
void DisplayUi::bar(int x, int y, int w, int h, int p, uint8_t fill, bool hatch, bool notches) {
    rect(x, y, w, h, kTrack);
    const int filled = w * std::clamp(p, 0, 100) / 100;
    if (hatch) {
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < filled; ++i)
                pixel(x + i, y + j, (i + j) % 10 < 5 ? kInk : kPaper);
    } else
        rect(x, y, filled, h, fill);
    // The 80% and 95% thresholds are gaps, which stay visible on both the track and the fill.
    for (int mark : {80, 95})
        if (notches)
            rect(x + w * mark / 100, y, 3, h, kPaper);
}
int DisplayUi::badge(int x, int center_y, const char *s, int size, bool filled, bool align_right) {
    const int ascent = digit_height(size, 2), w = text_width(s, size, 2, 2) + 20, h = ascent + 16;
    if (align_right)
        x -= w;
    if (filled)
        rect(x, center_y - h / 2, w, h, kInk);
    else
        frame(x, center_y - h / 2, w, h, kInk);
    text(x + 10, center_y - h / 2 + 8, s, size, filled ? kPaper : kInk, 2, 2);
    return w;
}
// bottom is the baseline of the digits; the unit follows on the same baseline.
int DisplayUi::value(int x, int bottom, const char *digits, int size, int unit_size, uint8_t color,
                     int spacing, const char *unit) {
    int advance = text(x, bottom - digit_height(size, 0), digits, size, color, 0, spacing);
    if (*unit)
        advance += text(x + advance + size / 24, bottom - digit_height(unit_size, 0), unit,
                        unit_size, color, 0, 0);
    return advance;
}
void DisplayUi::battery(const View &v, int y, uint8_t ink) {
    const int x = width_ - 91;
    frame(x, y, 46, 22, ink);
    rect(x + 48, y + 6, 3, 10, ink);
    const int segments = v.battery < 0 ? 0 : (v.battery + 24) / 25;
    for (int i = 0; i < 4; ++i)
        rect(x + 5 + i * 9, y + 5, 7, 12, i < segments ? ink : kTrack);
    // The bolt slot left of the battery stays reserved while not charging.
    if (v.charging) {
        const int bx = x - 17;
        line(bx + 7, y + 1, bx + 1, y + 11, ink, 2);
        line(bx + 1, y + 11, bx + 9, y + 11, ink, 2);
        line(bx + 9, y + 11, bx + 3, y + 21, ink, 2);
    }
}
void DisplayUi::status(const View &v, int y) {
    battery(v, y, kSub);
    // A healthy link shows no status; only exceptions are named.
    const char *label = v.link.has_passkey              ? "PAIRING"
                        : v.ota.phase != OtaPhase::Idle ? "UPDATING"
                        : !v.link.encrypted             ? "OFFLINE"
                                                        : nullptr;
    if (label)
        badge(width_ - 91 - (v.charging ? 34 : 14), y + 11, label, 18, false, true);
}
void DisplayUi::header(const View &v, const char *title) {
    const bool portrait = height_ > width_;
    const int top = portrait ? 40 : 30, size = portrait ? 40 : 34;
    const int baseline = top + (size + digit_height(size, 0)) / 2;
    char time[16], date[32];
    format_clock(v, time, sizeof(time), date, sizeof(date));
    const int advance =
        text(40, baseline - digit_height(size, 0), title ? title : time, size, kInk, 0, -1);
    if (!title && v.clock_valid)
        text(40 + advance + 18, baseline - digit_height(20, 2), date, 20, kSub, 2, 2);
    status(v, top + (size - 22) / 2);
    rect(40, top + size + (portrait ? 18 : 16), width_ - 80, 2, kInk);
}
void DisplayUi::service_title(const ServiceSummary &s, int x, int w, int top, int size) {
    text(x, top + (size - digit_height(size, 0)) / 2, s.name, size,
         s.none ? kSub : s.offline ? kDim : kInk, 0, -1);
    const int center = top + size / 2;
    text_right(x + w, center - digit_height(18, 2) / 2, s.age, 18, kSub, 2, 2);
    if (s.tag)
        badge(x + w - text_width(s.age, 18, 2, 2) - 14, center, s.tag, 18, true, true);
}
void DisplayUi::window_column(const ServiceSummary &s, bool primary, int x, int w, int bar_y) {
    const WindowSummary &win = primary ? s.primary : s.secondary;
    const int size = primary ? 120 : 60, box = size * 9 / 10, label_y = bar_y - 46 - box;
    text(x, label_y + 5, win.label, 18, kSub, 2, 3);
    if (!win.present) {
        text(x, label_y + 40, "NOT REPORTED", 20, kSub, 2, 1);
        line(x, bar_y + 6, x + w, bar_y + 6, kRule, 2, true);
        text(x, bar_y + 32, "--", 20, kSub, 2, 1);
        return;
    }
    if (win.warn)
        badge(x + w, label_y + 12, "WARN", 16, false, true);
    char b[12];
    snprintf(b, sizeof(b), "%d", win.percent);
    value(x, bar_y - 16 - (box - digit_height(size, 0)) / 2, b, size, primary ? 46 : 26,
          s.offline ? kDim : kInk, primary ? -5 : -2);
    bar(x, bar_y, w, 14, win.percent, s.offline ? kRule : primary ? kInk : kDim, win.warn, true);
    text(x, bar_y + 32, win.reset, primary ? 22 : 20,
         !primary ? kSub : s.offline ? kDim : kInk, 2, 1);
}
int DisplayUi::service_portrait(const View &v, int index, int y, bool rule) {
    const ServiceSummary s = summarize(v, index);
    const int x = 40, w = width_ - 80, height = s.none ? 146 : 329;
    if (s.tag)
        rect(16, y, width_ - 32, height, kWash);
    else if (rule)
        rect(16, y, width_ - 32, 2, kRule);
    service_title(s, x, w, y + 28, 40);
    if (s.none) {
        const int advance = text(x, y + 96, "WAITING FOR FIRST SAMPLE", 20, kSub, 2, 1);
        line(x + advance + 18, y + 103, x + w, y + 103, kRule, 2, true);
        return height;
    }
    // The primary and secondary windows share the width 1.4 : 1 and one bar baseline.
    const int primary_w = (w - 40) * 14 / 24;
    window_column(s, true, x, primary_w, y + 244);
    window_column(s, false, x + primary_w + 40, w - primary_w - 40, y + 244);
    return height;
}
void DisplayUi::service_landscape(const View &v, int index, int x, int w, int top, int bottom) {
    const ServiceSummary s = summarize(v, index);
    if (s.tag)
        rect(x, top, w, bottom - top, kWash);
    if (index)
        rect(x, top, 2, bottom - top, kRule);
    const int cx = x + 32, cw = w - 64;
    service_title(s, cx, cw, top + 22, 34);
    if (s.none) {
        text(cx, top + 84, "WAITING FOR", 20, kSub, 2, 1);
        text(cx, top + 114, "FIRST SAMPLE", 20, kSub, 2, 1);
        line(cx, top + 162, cx + cw, top + 162, kRule, 2, true);
        return;
    }
    const uint8_t ink = s.offline ? kDim : kInk;
    char b[12];
    text(cx, top + 83, s.primary.label, 18, kSub, 2, 3);
    if (s.primary.warn)
        badge(cx + cw, top + 90, "WARN", 16, false, true);
    snprintf(b, sizeof(b), "%d", s.primary.percent);
    value(cx, top + 224 - (116 - digit_height(132, 0)) / 2, b, 132, 50, ink, -7);
    bar(cx, top + 240, cw, 14, s.primary.percent, s.offline ? kRule : kInk, s.primary.warn, true);
    text(cx, top + 272, s.primary.reset, 22, ink, 2, 1);
    // The secondary window is one row along the bottom edge of the column.
    const int center = bottom - 40, text_y = center - digit_height(18, 2) / 2;
    int left = cx + text(cx, text_y, s.secondary.label, 18, kSub, 2, 3) + 14;
    if (!s.secondary.present) {
        left += text(left, text_y, "NOT REPORTED", 18, kSub, 2, 1) + 16;
        line(left, center, cx + cw, center, kRule, 2, true);
        return;
    }
    snprintf(b, sizeof(b), "%d", s.secondary.percent);
    left += value(left, center + digit_height(36, 0) / 2, b, 36, 18, ink, -1) + 16;
    const int right = cx + cw - text_width(s.secondary.reset, 18, 2, 1);
    text(right, text_y, s.secondary.reset, 18, kSub, 2, 1);
    bar(left, center - 5, right - 16 - left, 10, s.secondary.percent, s.offline ? kRule : kDim,
        s.secondary.warn, false);
}
DisplayUi::TrendValue DisplayUi::trend_last(const View &v, int provider) const {
    const uint32_t now = trend_now(v);
    if (!v.sd)
        return {};
    for (int i = v.trend_count - 1; i >= 0; --i) {
        const int value = provider ? v.trend[i].claude : v.trend[i].codex;
        if (value <= 100 && v.trend[i].epoch <= now && now - v.trend[i].epoch <= 86400)
            return {v.trend[i].epoch, value};
    }
    return {};
}
void DisplayUi::trend_swatch(int x, int y, int provider) {
    for (int i = 0; i < 22; ++i)
        if (!provider || i % 11 < 7)
            rect(x + i, y, 1, 4, kInk);
}
// Plots the last 24 hours of the short window in a w x h area whose bottom edge is 0%.
void DisplayUi::chart(const View &v, int x, int y, int w, int h) {
    rect(x, y, w, h / 5, kTint);
    line(x, y + h / 5, x + w, y + h / 5, kRule, 2, true);
    rect(x, y + h / 2, w, 2, kTrack);
    line(x + w / 2, y, x + w / 2, y + h, kTrack, 2, true);
    rect(x, y + h, w, 2, kRule);
    const uint32_t now = trend_now(v);
    // The axis stays anchored to the stored snapshot between half-hour samples.
    const uint32_t end = trend_end(v);
    for (int provider = 0; provider < 2; ++provider) {
        const TrendValue last = trend_last(v, provider);
        if (last.percent < 0)
            continue;
        TrendValue previous;
        const int count = v.sd ? v.trend_count : 0;
        for (int i = 0; i <= count; ++i) {
            const TrendValue point = i == count ? last
                : TrendValue{v.trend[i].epoch, provider ? v.trend[i].claude : v.trend[i].codex};
            // Draw the final valid stored value once, including its endpoint marker.
            if (i < count && point.epoch >= last.epoch)
                continue;
            if (point.percent > 100 || point.epoch > now || point.epoch > end ||
                now - point.epoch > 86400) {
                previous = {};
                continue;
            }
            const int px = x + w - static_cast<int>((end - point.epoch) * static_cast<uint64_t>(w) / 86400),
                      py = y + h - point.percent * h / 100;
            // Gaps longer than an hour are left open rather than interpolated.
            if (previous.percent >= 0 && previous.epoch <= point.epoch &&
                point.epoch - previous.epoch <= 3600) {
                const int prev_x = x + w - static_cast<int>((end - previous.epoch) * static_cast<uint64_t>(w) / 86400);
                line(prev_x, y + h - previous.percent * h / 100, px, py, kInk, 3, provider);
            }
            if (i == count) {
                rect(px - 2, py - 3, 5, 7, kInk);
                rect(px - 3, py - 2, 7, 5, kInk);
            }
            previous = point;
        }
    }
}
void DisplayUi::home(const View &v) {
    header(v);
    const bool alarm[2] = {summarize(v, 0).tag != nullptr, summarize(v, 1).tag != nullptr};
    const bool none[2] = {summarize(v, 0).none, summarize(v, 1).none};
    const TrendValue last[2] = {trend_last(v, 0), trend_last(v, 1)};
    const bool empty = last[0].percent < 0 && last[1].percent < 0;
    char b[12];
    char sampled[20], caption[32];
    trend_time(v, sampled, sizeof(sampled));
    snprintf(caption, sizeof(caption), "AS OF %s", sampled);
    if (height_ > width_) {
        int y = 100;
        for (int i = 0; i < 2; ++i)
            y += service_portrait(v, i, y, i && !alarm[0] && !alarm[1]);
        const int x = 40, w = width_ - 220, top = y + 72, h = height_ - 77 - top;
        rect(x, y, width_ - 80, 2, kRule);
        text(x, y + 31, "LAST 24H / SHORT", 18, kSub, 2, 3);
        text_right(width_ - 40, y + 32, empty ? "NO TREND DATA" : caption, 16, kSub, 2, 1);
        chart(v, x, top, w, h);
        text(x, height_ - 56, "-24H", 16, kSub, 2, 1);
        text_center(x, w, height_ - 56, "-12H", 16, kSub, 2, 1);
        text_right(x + w, height_ - 56, empty ? "--:--" : sampled, 16, kSub, 2, 1);
        for (int provider = 0, row = 0; provider < 2; ++provider) {
            if (last[provider].percent < 0)
                continue;
            const int lx = x + w + 20, ly = top + 12 + row++ * 100;
            trend_swatch(lx, ly + 5, provider);
            text(lx + 30, ly, provider ? "CLAUDE" : "CODEX", 16, kSub, 2, 1);
            snprintf(b, sizeof(b), "%d", last[provider].percent);
            value(lx, ly + 24 + digit_height(30, 0), b, 30, 16, kInk, -1);
            char age[20];
            trend_age(trend_now(v), last[provider].epoch, age, sizeof(age));
            text(lx, ly + 58, age, 16, kSub, 2, 0);
        }
        return;
    }
    const int top = 82, bottom = height_ - 150;
    // A service without data yields its width to the one that has some.
    const int w0 = none[0] ? 340 : none[1] ? width_ - 16 - 340 : (width_ - 16) / 2;
    service_landscape(v, 0, 8, w0, top, bottom);
    service_landscape(v, 1, 8 + w0, none[1] ? 340 : width_ - 16 - w0, top, bottom);
    rect(40, bottom, width_ - 80, 2, kRule);
    text(40, bottom + 23, "LAST 24H", 18, kSub, 2, 3);
    text(40, bottom + 48, empty ? "NO DATA" : "SHORT", 18, kSub, 2, 3);
    text(40, height_ - 42, "-24H", 16, kSub, 2, 1);
    chart(v, 184, bottom + 20, width_ - 418, 98);
    const int lx = width_ - 210;
    for (int provider = 0, row = 0; provider < 2; ++provider) {
        if (last[provider].percent < 0)
            continue;
        const int ly = bottom + 12 + row++ * 44;
        trend_swatch(lx, ly + 8, provider);
        text(lx + 32, ly + 4, provider ? "CLAUDE" : "CODEX", 16, kSub, 2, 1);
        snprintf(b, sizeof(b), "%d", last[provider].percent);
        value(lx + 112, ly + 4 + digit_height(24, 0), b, 24, 16, kInk, 0);
        char age[20];
        trend_age(trend_now(v), last[provider].epoch, age, sizeof(age));
        text(lx + 32, ly + 27, age, 16, kSub, 2, 0);
    }
    text(lx, height_ - 42, empty ? "--:--" : caption, 16, kSub, 2, 1);
}
// One usage summary row of the clock; x and w bound the text, the wash extends past them.
int DisplayUi::clock_row(const View &v, int index, int x, int w, int y, bool rule) {
    const bool portrait = height_ > width_;
    const ServiceSummary s = summarize(v, index);
    const int height = (portrait ? 111 : 150) + (s.none ? 0 : 26);
    const int right = portrait ? x + w + 20 : width_;
    if (s.tag)
        rect(x - 20, y, right - x + 20, height, kWash);
    else if (rule)
        rect(x - 20, y, right - x + 20, 2, kRule);
    const uint8_t ink = s.none ? kSub : s.offline ? kDim : kInk;
    const int advance = text(x, y + 30, s.name, 22, ink, 2, 3);
    if (s.tag)
        badge(x + advance + 12, y + 38, s.tag, 16, true, false);
    char sub[64], b[12] = "--";
    if (s.none)
        snprintf(sub, sizeof(sub), "WAITING FOR FIRST SAMPLE");
    else {
        snprintf(sub, sizeof(sub), "%s%s%s%s", s.primary.label[0] == 'W' ? "WEEK / " : "",
                 s.primary.reset, s.offline ? " / " : "", s.offline ? s.age : "");
        snprintf(b, sizeof(b), "%d", s.primary.percent);
    }
    const char *unit = s.none ? "" : "%";
    const int size = portrait ? 64 : 72, unit_size = portrait ? 28 : 30;
    const int bottom = y + (portrait ? 87 : 126), sub_y = bottom - digit_height(18, 2);
    if (portrait) {
        text(x, sub_y, sub, 18, kSub, 2, 1);
        const int value_w = text_width(b, size, 0, -2) + (s.none ? 0 : size / 24 - 2 +
                                                           text_width(unit, unit_size, 0, 0));
        value(x + w - value_w, bottom, b, size, unit_size, ink, -2, unit);
    } else {
        value(x, bottom, b, size, unit_size, ink, -2, unit);
        text_right(x + w, sub_y, sub, 18, kSub, 2, 1);
    }
    if (!s.none)
        bar(x, bottom + 16, w, 10, s.primary.percent, s.offline ? kRule : kInk, s.primary.warn,
            false);
    return height;
}
void DisplayUi::clock(const View &v) {
    const bool portrait = height_ > width_;
    char time[16], date[32];
    format_clock(v, time, sizeof(time), date, sizeof(date));
    const bool alarm = summarize(v, 0).tag || summarize(v, 1).tag;
    const int rows = (portrait ? 222 : 300) + (summarize(v, 0).none ? 0 : 26) +
                     (summarize(v, 1).none ? 0 : 26);
    const int size = portrait ? 212 : 236, line_h = size * 85 / 100;
    if (portrait) {
        status(v, 40);
        const int x = 44, w = width_ - 88, section = height_ - 44 - 41 - rows;
        // Time and date are centered between the status row and the usage summary.
        const int top = (62 + section - line_h - 70) / 2;
        text(x - 8, top + (line_h - digit_height(size, 0)) / 2, time, size, kInk, 0, -8);
        text(x, top + line_h + 42, date, 28, kSub, 2, 4);
        rect(x, section, w, 2, kRule);
        int y = section + 2;
        for (int i = 0; i < 2; ++i)
            y += clock_row(v, i, x, w, y, i && !alarm);
        text(x, y + 22, "TAP OR LIFT TO WAKE", 16, kSub, 2, 2);
        return;
    }
    const int column = width_ - 460, x = column + 36, w = width_ - 40 - x;
    const int top = (height_ - line_h - 73) / 2;
    text(30, top + (line_h - digit_height(size, 0)) / 2, time, size, kInk, 0, -9);
    text(40, top + line_h + 42, date, 30, kSub, 2, 4);
    rect(column, 36, 2, height_ - 72, kRule);
    status(v, 36);
    int y = (58 + height_ - 57 - rows) / 2;
    for (int i = 0; i < 2; ++i)
        y += clock_row(v, i, x, w, y, i && !alarm);
    text(x, height_ - 52, "TAP OR LIFT TO WAKE", 16, kSub, 2, 2);
}
void DisplayUi::settings(const View &v) {
    const bool portrait = height_ > width_;
    header(v, "DISPLAY");
    const char *labels[] = {"AUTO UPDATE", "IDLE CLOCK", "ROTATION"};
    const char *subs[] = {"INTERVAL / SEC", "AFTER IDLE / MIN", "1.5S HOLD"};
    const char *values[][4] = {{"15", "30", "60", "120"},
                               {"OFF", "5", "15", "30"},
                               {"AUTO", "PORTRAIT", "LANDSCAPE", nullptr}};
    const int selected[] = {v.settings.refresh, v.settings.idle, v.settings.rotation};
    for (int row = 0; row < 3; ++row) {
        const int top = option_box(width_, height_, row, 0).y - (portrait ? 85 : 0);
        text(40, top + (portrait ? 6 : 14), labels[row], 30, kInk, 0, 0);
        text(40, top + (portrait ? 46 : 54), subs[row], 18, kSub, 2, 1);
        if (row < 2)
            rect(40, top + (portrait ? 205 : 112), width_ - 80, 1, kRule);
        for (int i = 0; i < (row == 2 ? 3 : 4); ++i) {
            const Box box = option_box(width_, height_, row, i);
            if (i == selected[row])
                rect(box.x, box.y, box.w, box.h, kInk);
            else
                frame(box.x, box.y, box.w, box.h, kInk);
            text_center(box.x, box.w, box.y + 32, values[row][i], 24,
                        i == selected[row] ? kPaper : kInk, 2, 1);
        }
    }
    const Box done = done_box(width_, height_);
    text(40, done.y - 37,
         v.save_error ? "SAVE FAILED / TAP TO RETRY" : "EACH TAP SAVES / HOLD 0.8S OR DONE TO EXIT",
         18, kSub, 2, 1);
    rect(done.x, done.y, done.w, done.h, kInk);
    text_center(done.x, done.w, done.y + (done.h - 18) / 2, "DONE", 24, kPaper, 2, 3);
}
void DisplayUi::pairing(const View &v) {
    const bool portrait = height_ > width_;
    const int pad = portrait ? 40 : 36, content = portrait ? 101 : 93;
    header(v);
    const int top = (content + height_ - pad - 24 - 376) / 2;
    text(40, top + 6, "PAIRING CODE", 22, kSub, 2, 4);
    char b[24];
    snprintf(b, sizeof(b), "%03lu %03lu", static_cast<unsigned long>(v.link.passkey / 1000),
             static_cast<unsigned long>(v.link.passkey % 1000));
    text(40, top + 71, b, 124, kInk, 0, 2);
    rect(40, top + 221, width_ - 80, 1, kRule);
    text(40, top + 260, "ENTER THIS CODE", 30, kInk, 0, 0);
    text(40, top + 302, "ON THE DESKTOP BRIDGE", 30, kInk, 0, 0);
    text(40, top + 355, "DEVICE CONNECTS AFTER CONFIRMATION", 19, kSub, 2, 1);
    text(40, height_ - pad - 19, v.device_name, 18, kSub, 2, 2);
}
void DisplayUi::ota(const View &v) {
    const bool portrait = height_ > width_;
    const int pad = portrait ? 40 : 36, content = portrait ? 101 : 93;
    header(v);
    char b[80];
    if (v.ota.phase == OtaPhase::Confirming) {
        const Box deny = ota_button(width_, height_, 0), confirm = ota_button(width_, height_, 1);
        const int top = (content + deny.y - 340) / 2;
        text(40, top + 6, "FIRMWARE", 22, kSub, 2, 4);
        text(40, top + 57, v.ota.version.c_str(), 52, kInk, 0, -1, false);
        snprintf(b, sizeof(b), "%.1f MB / FROM DESKTOP BRIDGE", v.ota.size / 1000000.);
        text(40, top + 124, b, 20, kSub, 2, 1);
        rect(40, top + 200, width_ - 80, 1, kRule);
        text(40, top + 249, "CONFIRM UPDATE", 56, kInk, 0, -1);
        text(40, top + 320, "INSTALL STARTS ONLY AFTER CONFIRMATION", 19, kSub, 2, 1);
        frame(deny.x, deny.y, deny.w, deny.h, kInk);
        text_center(deny.x, deny.w, deny.y + 39, "DENY", 24, kInk, 2, 3);
        rect(confirm.x, confirm.y, confirm.w, confirm.h, kInk);
        text_center(confirm.x, confirm.w, confirm.y + 39, "CONFIRM", 24, kPaper, 2, 3);
        text(40, height_ - pad - 19, "KEY2 PRESS CONFIRM / HOLD DENY", 18, kSub, 2, 1);
        return;
    }
    const int gap = portrait ? 52 : 24, gap2 = portrait ? 56 : 28;
    int y = (content + height_ - pad - (421 + gap + gap2)) / 2;
    text(40, y + 6, "FIRMWARE", 22, kSub, 2, 4);
    y += 47;
    text(40, y + 10, v.ota.version.c_str(), 52, kInk, 0, -1, false);
    y += 57 + gap;
    // Progress advances in 5% cells so a whole transfer costs about twenty refreshes.
    const int percent = ota_progress_percent(v.ota.offset, v.ota.size) / 5 * 5;
    snprintf(b, sizeof(b), "%d", percent);
    const int advance = text(40, y + 14, b, 144, kInk, 0, -6);
    text(40 + advance + 8, y + 76, "%", 58, kInk, 0, 0);
    y += 150;
    const int pitch = width_ - 80 + 4;
    for (int i = 0; i < 20; ++i)
        rect(40 + i * pitch / 20, y, (i + 1) * pitch / 20 - i * pitch / 20 - 4, 26,
             i < percent / 5 ? kInk : kTrack);
    y += 48;
    const char *phase = v.ota.phase == OtaPhase::Rebooting   ? "RESTARTING"
                        : v.ota.phase == OtaPhase::Verifying ? "VERIFYING"
                                                             : "WRITING";
    snprintf(b, sizeof(b), "%s / %.1f MB / %.1f MB", phase, v.ota.offset / 1000000.,
             v.ota.size / 1000000.);
    text(40, y + 6, b, 20, kSub, 2, 1);
    y += 26 + gap2;
    rect(40, y, width_ - 80, 1, kRule);
    y += 27;
    text(40, y + 5, "DO NOT UNPLUG OR POWER OFF", 28, kInk, 0, 0);
    text(40, y + 46, "DEVICE RESTARTS AND RECONNECTS AUTOMATICALLY", 19, kSub, 2, 1);
}
TapTarget DisplayUi::hit(const View &v, int touch_x, int touch_y) const {
    // The touch panel reports upright portrait coordinates regardless of the drawn rotation.
    const bool portrait = !(v.orientation & 1);
    const int width = portrait ? kTouchWidth : kTouchHeight,
              height = portrait ? kTouchHeight : kTouchWidth;
    int x = touch_x, y = touch_y;
    switch (v.orientation & 3) {
    case 1:
        x = touch_y;
        y = kTouchWidth - 1 - touch_x;
        break;
    case 2:
        x = kTouchWidth - 1 - touch_x;
        y = kTouchHeight - 1 - touch_y;
        break;
    case 3:
        x = kTouchHeight - 1 - touch_y;
        y = touch_x;
        break;
    }
    const int scene = scene_of(v);
    if (scene == 5) {
        if (ota_button(width, height, 0).contains(x, y))
            return {TapTarget::OtaDeny};
        if (ota_button(width, height, 1).contains(x, y))
            return {TapTarget::OtaConfirm};
    } else if (scene == static_cast<int>(Page::Settings)) {
        if (done_box(width, height).contains(x, y))
            return {TapTarget::Done};
        for (int row = 0; row < 3; ++row)
            for (int i = 0; i < (row == 2 ? 3 : 4); ++i)
                if (option_box(width, height, row, i).contains(x, y))
                    return {TapTarget::Option, row, i};
    }
    return {};
}
// Five rows of four candidates each for small text, bar tracks, rules, large digits and alarm
// washes, labeled with a code and the gray level as its hex digit tripled. Portrait only.
void DisplayUi::test_card() {
    struct Sample {
        const char *note;
        uint8_t gray;
        // Font family for T, thickness in pixels for R; unused otherwise.
        int arg;
    };
    struct Row {
        const char *title;
        Sample samples[4];
    };
    static const Row rows[] = {
        {"T / SMALL TEXT 18PX", {{"555/500", 0x50, 1}, {"444/700", 0x40, 2}, {"222/700", 0x20, 2}, {"000/700", 0x00, 2}}},
        {"B / BAR TRACK", {{"DDD", 0xD0, 0}, {"CCC", 0xC0, 0}, {"BBB", 0xB0, 0}, {"999", 0x90, 0}}},
        {"R / RULE", {{"BBB 1PX", 0xB0, 1}, {"BBB 2PX", 0xB0, 2}, {"888 2PX", 0x80, 2}, {"666 2PX", 0x60, 2}}},
        {"N / LARGE DIGITS", {{"000", 0x00, 0}, {"222", 0x20, 0}, {"333", 0x30, 0}, {"444", 0x40, 0}}},
        {"W / ALARM WASH", {{"000", 0x00, 0}, {"EEE", 0xE0, 0}, {"DDD", 0xD0, 0}, {"CCC", 0xC0, 0}}},
    };
    const int w = 139;
    for (int r = 0; r < 5; ++r) {
        const Row &row = rows[r];
        const int top = 40 + r * 242, sy = top + 38;
        text(40, top + 4, row.title, 18, 0x20, 2, 3);
        for (int i = 0; i < 4; ++i) {
            const Sample &s = row.samples[i];
            const int x = 40 + i * (w + 16);
            switch (row.title[0]) {
            case 'T':
                text(x, sy + 28, "RESETS", 18, s.gray, s.arg, 1);
                text(x, sy + 55, "4D 18H", 18, s.gray, s.arg, 1);
                break;
            case 'B':
                rect(x, sy + 41, w, 14, s.gray);
                rect(x, sy + 41, w * 45 / 100, 14, kInk);
                break;
            case 'R':
                rect(x, sy + 48 - s.arg / 2, w, s.arg, s.gray);
                break;
            case 'N':
                text(x, sy + 15, "98", 96, s.gray, 0, -4);
                break;
            default:
                rect(x, sy + 15, w, 66, s.gray);
                text(x + 14, sy + 37, "98%", 30, s.gray ? kInk : kPaper, 0, 0);
            }
            const char code[] = {row.title[0], static_cast<char>('1' + i), 0};
            const int advance = text(x, sy + 111, code, 18, kInk, 2, 1);
            text(x + advance + 8, sy + 112, s.note, 16, 0x40, 2, 0);
        }
    }
}
void DisplayUi::render(const View &v, bool baseline) {
    // The upright portrait view is epdiy's inverted portrait; each quarter turn advances one step.
    epd_set_rotation(static_cast<EpdRotation>((EPD_ROT_INVERTED_PORTRAIT + v.orientation) % 4));
    const bool portrait = !(v.orientation & 1);
    width_ = portrait ? kTouchWidth : kTouchHeight;
    height_ = portrait ? kTouchHeight : kTouchWidth;
    epd_hl_set_all_white(state_);
    const int scene = scene_of(v);
    if (scene == 6)
        test_card();
    else if (scene == 3 || scene == 5)
        ota(v);
    else if (scene == 4)
        pairing(v);
    else if (v.page == Page::Clock)
        clock(v);
    else if (v.page == Page::Settings)
        settings(v);
    else
        home(v);
    if (v.ota.error != OtaError::None && v.ota.phase == OtaPhase::Idle) {
        const int pad = portrait ? 40 : 36;
        rect(0, height_ - pad - 34, width_, pad + 34, kPaper);
        text(40, height_ - pad - 19, ota_error_presentation(v.ota.error).title, 19, kInk, 2, 1);
    }
    const bool first = rendered_scene_ < 0;
    const bool full = baseline || scene != rendered_scene_ || v.orientation != rendered_orientation_;
    if (!full && !memcmp(state_->front_fb, state_->back_fb, epd_width() / 2 * epd_height()))
        return;
    read_pico_epd_use_scan(READ_PICO_EPD_SCAN_FULL);
    epd_lcd_set_prefill_lines(32);
    epd_poweron();
    if (!read_pico_rails_on()) {
        // epdiy reports success without panel rails; the frame must not count as displayed.
        rendered_scene_ = -1;
        ESP_LOGE("read_pico", "Panel rails did not power up");
        return;
    }
    const int64_t start = esp_timer_get_time();
    const char *mode = "GL16";
    EpdDrawError result;
    if (first) {
        // Startup and failed refreshes require a known optical baseline before drawing.
        epd_clear();
        result = epd_hl_update_screen_from_white(state_, MODE_GC16, kPanelTemperatureC);
        mode = "GC16 from white";
        soft_refreshes_ = 0;
    } else if (full || soft_refreshes_ + 1 >= kSoftRefreshesPerBaseline) {
        result = epd_hl_update_screen_full(state_, MODE_GC16, kPanelTemperatureC);
        mode = "GC16";
        soft_refreshes_ = 0;
    } else if (du_reaches(state_->front_fb, state_->back_fb, epd_width() / 2 * epd_height())) {
        // DU drives the changed pixels alone, so the rest of the frame does not flicker.
        result = epd_hl_update_screen(state_, MODE_DU, kPanelTemperatureC);
        mode = "DU";
        ++soft_refreshes_;
    } else {
        // GL16 drives every pixel; skipping unchanged white pixels leaves a gray floor.
        result = epd_hl_update_screen_full(state_, MODE_GL16, kPanelTemperatureC);
        ++soft_refreshes_;
    }
    if (result & EPD_DRAW_EMPTY_LINE_QUEUE) {
        // A starved line queue leaves a torn frame; redraw from white at the safe pixel clock.
        read_pico_epd_set_pclk(READ_PICO_EPD_PCLK_MIN_MHZ);
        epd_clear();
        result = epd_hl_update_screen_from_white(state_, MODE_GC16, kPanelTemperatureC);
        soft_refreshes_ = 0;
        ESP_LOGW("read_pico", "Line queue underrun; pixel clock lowered to %d MHz",
                 READ_PICO_EPD_PCLK_MIN_MHZ);
    }
    rails_deadline_ms_ = esp_timer_get_time() / 1000 + kRailsIdleMs;
    if (result != EPD_DRAW_SUCCESS) {
        rendered_scene_ = -1;
        ESP_LOGE("read_pico", "Display refresh failed: 0x%x", static_cast<unsigned>(result));
        return;
    }
    rendered_scene_ = scene;
    rendered_orientation_ = v.orientation;
    ESP_LOGI("read_pico", "REFRESH %s %lld ms", mode, (esp_timer_get_time() - start) / 1000);
}
} // namespace usage_panel::read_pico
