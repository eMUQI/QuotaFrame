#include "screens.hpp"
#include "panel_logic.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace usage_panel::rlcd {
namespace {
constexpr const char *kNames[2] = {"CODEX", "CLAUDE"};
constexpr const char *kDays[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
// Content sits 8 px inside the 2 px bezel frame drawn 4 px from the panel edge.
constexpr int kLeft = 14, kBezel = 4;
// Pixel-font line height at the label scale.
constexpr int kLine = 14;
// Inverted chips extend this far around their text, so the text aligns with plain labels.
constexpr int kChipPad = 4;
// First content line, 10 px below the header rule.
constexpr int kContentTop = 46;
// Smallest pace excess, in percentage points, that the detail page reports as OVER. Smaller
// deltas are within the rounding of whole-percent usage and the fixed window lengths.
constexpr int kOverThreshold = 5;
// Footer rule on pages with a footer line; the footer text follows 10 px below it.
constexpr int kFooterRule = 252, kPortraitFooterRule = 340;

Provider provider_of(int i) { return i ? Provider::Claude : Provider::Codex; }

int round_px(float v) { return static_cast<int>(std::lround(v)); }

/** Short-window value shown on compact rows: the short window, else the week window. */
int compact_percent(const View &v, int i) {
    const auto &s = v.model.snapshot(provider_of(i));
    if (short_present(s, v.link.encrypted))
        return s.short_window.used_percent;
    if (week_present(s, v.link.encrypted))
        return s.week_window.used_percent;
    return -1;
}

/** Empty-row message when neither window has a value. */
const char *empty_message(const View &v) {
    return v.link.encrypted ? "WAITING FOR DATA" : "NO DATA";
}

/** Offline or absent windows have no live reset countdown. */
void reset_countdown(const View &v, int i, bool week, char *out, size_t size) {
    const auto provider = provider_of(i);
    const auto &s = v.model.snapshot(provider);
    const bool present = week ? week_present(s, v.link.encrypted)
                              : short_present(s, v.link.encrypted);
    if (!v.link.encrypted || !present) {
        snprintf(out, size, "--");
        return;
    }
    const uint32_t epoch = v.model.estimated_epoch(provider, v.now_ms);
    if (week)
        format_week_countdown(s.week_window, epoch, out, size);
    else
        format_short_countdown(s.short_window, epoch, v.settings.seconds, out, size);
}

/** Sample age label; false when the sample is under a minute old and needs no label. */
bool age_label(const View &v, int i, char *out, size_t size) {
    const auto &s = v.model.snapshot(provider_of(i));
    if (!s.has_valid_data) {
        snprintf(out, size, "NO DATA");
        return true;
    }
    const uint32_t epoch = v.model.estimated_epoch(provider_of(i), v.now_ms);
    const uint32_t age = epoch > s.sampled_at ? epoch - s.sampled_at : 0;
    format_age(age, out, size);
    return age >= 60;
}

/** Pace markers for the two windows of one provider; invalid while offline or absent. */
struct Paces {
    Pace short_pace, week_pace;
};
Paces paces(const View &v, int i, bool sp, bool wp) {
    const auto p = provider_of(i);
    const auto &s = v.model.snapshot(p);
    const bool enc = v.link.encrypted;
    const uint32_t epoch = v.model.estimated_epoch(p, v.now_ms);
    return {enc && sp ? compute_pace(s.short_window, epoch, kShortWindowSeconds) : Pace{},
            enc && wp ? compute_pace(s.week_window, epoch, kWeekWindowSeconds) : Pace{}};
}

float marker(const Pace &pace) { return pace.valid ? pace_fraction(pace) : -1.f; }
} // namespace

const char *link_status(const View &v) {
    if (v.link.has_passkey)
        return "PAIRING";
    if (v.ota.stage != OtaStage::Idle)
        return "UPDATING";
    return v.link.encrypted ? "LINKED" : "OFFLINE";
}

void Screens::battery(const View &v, int x, Ink ink) {
    c_.frame(x, 12, 28, 18, 2, ink);
    c_.fill(x + 28, 17, 4, 8, ink);
    const int segments = v.battery < 0 ? 0 : std::clamp((v.battery * 3 + 99) / 100, 0, 3);
    for (int i = 0; i < segments; ++i)
        c_.fill(x + 4 + 6 * i, 16, 4, 10, ink);
}

int Screens::chip(int x, int top, const char *s, Ink ink) {
    const Ink paper = ink == Ink::Black ? Ink::White : Ink::Black;
    const int tw = Canvas::text_width(s, 2);
    const int x0 = x - kChipPad, y0 = top - kChipPad;
    const int w = tw + 2 * kChipPad, h = kLine + 2 * kChipPad;
    c_.fill(x0, y0, w, h, ink);
    // A 2 px corner radius: clear the three outermost pixels of each corner.
    for (int cx : {x0, x0 + w - 1})
        for (int cy : {y0, y0 + h - 1}) {
            const int dx = cx == x0 ? 1 : -1, dy = cy == y0 ? 1 : -1;
            c_.pixel(cx, cy, paper);
            c_.pixel(cx + dx, cy, paper);
            c_.pixel(cx, cy + dy, paper);
        }
    c_.text(x, top, s, 2, paper);
    return tw;
}

void Screens::header(const View &v, const char *title, int page) {
    char time[8];
    snprintf(time, sizeof(time), "%02u:%02u", v.clock.hour, v.clock.minute);
    c_.text(kLeft, 14, title ? title : v.clock.valid ? time : "--:--", 2);
    int edge = c_.width() - kLeft - 32;
    battery(v, edge);
    const char *status = link_status(v);
    // LINKED is the normal state and is not shown; every other state gets a chip.
    if (v.link.has_passkey || v.ota.stage != OtaStage::Idle || !v.link.encrypted) {
        const int tw = Canvas::text_width(status, 2);
        edge -= 8 + kChipPad + tw;
        chip(edge, 14, status);
        edge -= kChipPad;
    }
    if (page >= 0) {
        edge -= 10 + kRingPages * 12 - 4;
        for (int i = 0; i < kRingPages; ++i) {
            if (i == page)
                c_.fill(edge + 12 * i, 17, 8, 8);
            else
                c_.frame(edge + 12 * i, 17, 8, 8, 2);
        }
    }
    c_.fill(kLeft, 34, c_.width() - 2 * kLeft, 2);
}

void Screens::bar(int x, int y, int w, int h, int percent, float marker) {
    constexpr int kCells = 20, kGap = 2, kInset = 4;
    c_.frame(x, y, w, h, 2);
    const int cx = x + kInset, cw = w - 2 * kInset;
    const float cell = (cw - kGap * (kCells - 1.f)) / kCells;
    int lit = percent < 0 ? 0 : round_px(std::min(percent, 100) * kCells / 100.f);
    if (percent > 0 && lit == 0)
        lit = 1;
    for (int i = 0; i < lit; ++i) {
        const int x0 = cx + round_px(i * (cell + kGap));
        const int x1 = cx + round_px(i * (cell + kGap) + cell);
        c_.fill(x0, y + kInset, x1 - x0, h - 2 * kInset);
    }
    if (marker < 0)
        return;
    // A 2 px tick overhanging the bar by 4 px, with a 2 px white halo separating it from the cells.
    const int mx = x + round_px((w - 2) * marker);
    c_.fill(mx - 2, y - 4, 6, h + 8, Ink::White);
    c_.fill(mx, y - 4, 2, h + 8);
}

namespace {
struct Digits {
    int w, t;
};
/** Seven-segment proportions for a given digit height. */
Digits digits_for(int h) { return {h * 5 / 9, std::max(3, h / 10)}; }
/** The percent sign is 3/8 of a digit wide plus one stroke, bottom-aligned. */
int percent_width(int h) { return digits_for(h).w * 3 / 8 + digits_for(h).t; }
/**
 * Space between the digit field and the percent sign: a third of a digit, so a 1 in the last
 * cell (lit on the right-hand side of its cell) does not read as touching the sign.
 */
int percent_gap(int h) { return digits_for(h).w / 3; }

/** Percent sign drawn with the stroke thickness of the digits: two square dots and a diagonal. */
void percent_sign(Canvas &c, int x, int bottom, int h, Ink ink) {
    const Digits d = digits_for(h);
    const int w = percent_width(h), height = h * 45 / 100, top = bottom - height;
    const int dot = d.t;
    c.fill(x, top, dot, dot, ink);
    c.fill(x + w - dot, bottom - dot, dot, dot, ink);
    const int stroke = d.t;
    for (int y = top; y < bottom; ++y) {
        const int cx = x + (w - stroke) * (bottom - 1 - y) / (height - 1);
        c.fill(cx, y, stroke, 1, ink);
    }
}
} // namespace

namespace {
/**
 * A fixed field of a hundreds half digit and two digit cells, right-aligned like a segment LCD:
 * 5 shows as a lone digit in the last cell, 100 lights the half digit.
 */
void percent_digits(int percent, char *out, size_t size) {
    if (percent < 0)
        snprintf(out, size, "h--");
    else if (percent >= 100)
        snprintf(out, size, "H00");
    else
        snprintf(out, size, "h%2d", percent);
}
} // namespace

int Screens::big_percent_width(int h) {
    const Digits d = digits_for(h);
    return Canvas::segments_width("h88", d.w, d.t) + percent_gap(h) + percent_width(h);
}

int Screens::big_percent(int x, int top, int percent, int h, Ink ink) {
    const Digits d = digits_for(h);
    char b[8];
    percent_digits(percent, b, sizeof(b));
    // The percent sign stays at a fixed position after the field, including for missing values.
    const int w = c_.segments(x, top, b, d.w, h, d.t, ink);
    percent_sign(c_, x + w + percent_gap(h), top + h, h, ink);
    return w + percent_gap(h) + percent_width(h);
}

void Screens::home_row(const View &v, int i, int top) {
    const auto &s = v.model.snapshot(provider_of(i));
    const bool sp = short_present(s, v.link.encrypted), wp = week_present(s, v.link.encrypted);
    const int x0 = 148, x1 = c_.width() - kLeft;
    c_.text(kLeft, top, kNames[i], 2);
    if (!sp && !wp) {
        c_.text(x0, top + 44, empty_message(v), 2);
        return;
    }
    const Paces pace = paces(v, i, sp, wp);
    char b[32];
    // The figure spans top + 22 to top + 86, level with the top of the SHORT bar and the
    // bottom of the WEEK bar.
    big_percent(kLeft, top + 22, sp ? s.short_window.used_percent : -1, 64);
    if (age_label(v, i, b, sizeof(b)))
        c_.text(kLeft, top + 94, b, 2);

    c_.text(x0, top, "SHORT", 2);
    reset_countdown(v, i, false, b, sizeof(b));
    c_.text_right(x1, top, b, 2);
    bar(x0, top + 22, x1 - x0, 18, sp ? s.short_window.used_percent : -1, marker(pace.short_pace));

    if (wp)
        snprintf(b, sizeof(b), "WEEK %u%%", s.week_window.used_percent);
    else
        snprintf(b, sizeof(b), "WEEK --");
    c_.text(x0, top + 50, b, 2);
    reset_countdown(v, i, true, b, sizeof(b));
    c_.text_right(x1, top + 50, b, 2);
    bar(x0, top + 72, x1 - x0, 14, wp ? s.week_window.used_percent : -1, marker(pace.week_pace));
}

void Screens::home(const View &v) {
    // Rows of 108 px with 10 px between the header rule, each row, the separator and the bezel.
    header(v, nullptr, 0);
    home_row(v, 0, kContentTop);
    c_.dotted(kLeft, 164, c_.width() - 2 * kLeft);
    home_row(v, 1, 176);
}

void Screens::home_portrait(const View &v) {
    header(v, nullptr);
    const int right = c_.width() - kLeft, w = right - kLeft;
    for (int i = 0; i < 2; ++i) {
        const int t = i ? 224 : 44;
        const auto &s = v.model.snapshot(provider_of(i));
        const bool sp = short_present(s, v.link.encrypted), wp = week_present(s, v.link.encrypted);
        char b[32];
        c_.text(kLeft, t, kNames[i], 2);
        if (!sp && !wp) {
            c_.text(kLeft, t + 60, empty_message(v), 2);
            continue;
        }
        if (age_label(v, i, b, sizeof(b)))
            c_.text_right(right, t, b, 2);
        const Paces pace = paces(v, i, sp, wp);
        // The SHORT label and countdown end level with the bottom of the figure.
        big_percent(kLeft, t + 22, sp ? s.short_window.used_percent : -1, 64);
        c_.text_right(right, t + 50, "SHORT", 2);
        reset_countdown(v, i, false, b, sizeof(b));
        c_.text_right(right, t + 72, b, 2);
        bar(kLeft, t + 96, w, 18, sp ? s.short_window.used_percent : -1, marker(pace.short_pace));

        if (wp)
            snprintf(b, sizeof(b), "WEEK %u%%", s.week_window.used_percent);
        else
            snprintf(b, sizeof(b), "WEEK --");
        c_.text(kLeft, t + 124, b, 2);
        reset_countdown(v, i, true, b, sizeof(b));
        c_.text_right(right, t + 124, b, 2);
        bar(kLeft, t + 146, w, 14, wp ? s.week_window.used_percent : -1, marker(pace.week_pace));
    }
    c_.dotted(kLeft, 214, w);
}

void Screens::focus(const View &v, int i) {
    const auto &s = v.model.snapshot(provider_of(i));
    const bool sp = short_present(s, v.link.encrypted), wp = week_present(s, v.link.encrypted);
    const int right = c_.width() - kLeft, column = 240;
    header(v, kNames[i], static_cast<int>(v.page));
    const Paces pace = paces(v, i, sp, wp);
    char b[32];
    // Short window: 80 px figure, reset countdown and pace in the right column, then its bar.
    c_.text(kLeft, kContentTop, "SHORT", 2);
    big_percent(kLeft, 66, sp ? s.short_window.used_percent : -1, 80);
    c_.text(column, kContentTop, "RESET", 2);
    reset_countdown(v, i, false, b, sizeof(b));
    c_.text(column, 66, b, 3);
    if (pace.short_pace.valid && pace.short_pace.delta >= kOverThreshold) {
        snprintf(b, sizeof(b), "OVER +%d%%", pace.short_pace.delta);
        chip(column, 110, b);
    }
    bar(kLeft, 156, right - kLeft, 18, sp ? s.short_window.used_percent : -1,
        marker(pace.short_pace));
    if (pace.short_pace.valid) {
        char elapsed[16];
        format_elapsed(pace.short_pace.elapsed_s, elapsed, sizeof(elapsed));
        snprintf(b, sizeof(b), "ELAPSED %s", elapsed);
        c_.text(kLeft, 184, b, 2);
    }
    if (age_label(v, i, b, sizeof(b)))
        c_.text_right(right, 184, b, 2);

    // Week window: the same row structure as Home with the elapsed time below its bar.
    c_.dotted(kLeft, 208, right - kLeft);
    if (wp)
        snprintf(b, sizeof(b), "WEEK %u%%", s.week_window.used_percent);
    else
        snprintf(b, sizeof(b), "WEEK --");
    const int label = c_.text(kLeft, 218, b, 2);
    if (pace.week_pace.valid && pace.week_pace.delta >= kOverThreshold) {
        snprintf(b, sizeof(b), "OVER +%d%%", pace.week_pace.delta);
        chip(kLeft + label + 12, 218, b);
    }
    reset_countdown(v, i, true, b, sizeof(b));
    c_.text_right(right, 218, b, 2);
    bar(kLeft, 244, right - kLeft, 14, wp ? s.week_window.used_percent : -1,
        marker(pace.week_pace));
    if (pace.week_pace.valid) {
        char elapsed[16];
        format_elapsed(pace.week_pace.elapsed_s, elapsed, sizeof(elapsed));
        snprintf(b, sizeof(b), "ELAPSED %s", elapsed);
        c_.text(kLeft, 268, b, 2);
    }
}

void Screens::alert(const View &v) {
    const int i = v.alert;
    const auto p = provider_of(i);
    const auto &s = v.model.snapshot(p);
    const uint32_t epoch = v.model.estimated_epoch(p, v.now_ms);
    const int w = c_.width(), h = c_.height(), right = w - kLeft;
    const bool exhausted = s.short_window.used_percent >= 100;
    char b[32];
    c_.text(kLeft, 14, kNames[i], 2, Ink::White);
    const int other = compact_percent(v, 1 - i);
    if (other >= 0)
        snprintf(b, sizeof(b), "%s %d%%", kNames[1 - i], other);
    else
        snprintf(b, sizeof(b), "%s --", kNames[1 - i]);
    c_.text_right(right, 14, b, 2, Ink::White);
    big_percent(kLeft, portrait_ ? 50 : 38, s.short_window.used_percent, 150, Ink::White);
    format_short_countdown(s.short_window, epoch, v.settings.seconds, b, sizeof(b));
    // The 2x RESET label shares its bottom edge with the 3x countdown.
    int reset = 230;
    if (portrait_) {
        c_.text(kLeft, 224, "SHORT", 2, Ink::White);
        c_.text(kLeft, 244, exhausted ? "EXHAUSTED" : "NEAR LIMIT", 3, Ink::White);
        reset = 290;
    } else {
        c_.text(kLeft, 198, exhausted ? "SHORT EXHAUSTED" : "SHORT NEAR LIMIT", 3, Ink::White);
    }
    c_.text(kLeft, reset + 7, "RESET", 2, Ink::White);
    c_.text(kLeft + 72, reset, b, 3, Ink::White);
    const int rule = h - 38;
    c_.fill(kLeft, rule, w - 2 * kLeft, 2, Ink::White);
    c_.text(kLeft, rule + 10, "KEY DISMISS", 2, Ink::White);
    c_.text_right(right, rule + 10,
                  v.settings.alert == static_cast<uint8_t>(AlertMode::Beep) ? "BEEP ON" : "BEEP OFF",
                  2, Ink::White);
}

void Screens::trend(const View &v) {
    char b[32];
    header(v, "24H", static_cast<int>(Page::Trend));
    const uint32_t epoch = std::max(v.model.estimated_epoch(Provider::Codex, v.now_ms),
                                    v.model.estimated_epoch(Provider::Claude, v.now_ms));
    // Match the sampling buckets while allowing the time axis to advance offline.
    const auto sample_age = [epoch](const TrendPoint &point) -> uint32_t {
        if (epoch < kMinTrendEpoch || point.epoch > epoch)
            return kTrendPoints;
        return epoch / kTrendIntervalSeconds - point.epoch / kTrendIntervalSeconds;
    };
    // 48 slots across the content width, oldest on the left; each bar leaves a 2 px gap, so bars
    // are 5 or 6 px wide.
    constexpr int kHeight = 76;
    const int x0 = kLeft, span = c_.width() - 2 * kLeft;
    const auto slot_x = [span](int slot) { return kLeft + slot * span / kTrendPoints; };
    for (int service = 0; service < 2; ++service) {
        const int label = service ? 154 : 44, base = label + 22 + kHeight;
        int peak = -1;
        for (int k = 0; k < v.trend_count; ++k) {
            const uint8_t value = service ? v.trend[k].claude : v.trend[k].codex;
            if (value <= 100 && sample_age(v.trend[k]) < kTrendPoints)
                peak = std::max<int>(peak, value);
        }
        c_.text(x0, label, kNames[service], 2);
        if (peak >= 0)
            snprintf(b, sizeof(b), "MAX %d%%", peak);
        else
            snprintf(b, sizeof(b), "MAX --");
        c_.text_right(x0 + span, label, b, 2);
        c_.fill(x0, base, span, 2);
        if (peak < 0) {
            const char *empty = !v.sd ? "NO TF CARD"
                                : epoch < kMinTrendEpoch ? "WAITING FOR TIME"
                                                         : "NO DATA YET";
            c_.text((c_.width() - Canvas::text_width(empty, 2)) / 2, base - 45, empty, 2);
            continue;
        }
        // Dotted guides at 100 % and 50 %; missing samples leave gaps.
        c_.dotted(x0, base - kHeight, span);
        c_.dotted(x0, base - kHeight / 2, span);
        for (int k = 0; k < v.trend_count; ++k) {
            const uint8_t value = service ? v.trend[k].claude : v.trend[k].codex;
            const uint32_t age = sample_age(v.trend[k]);
            if (value > 100 || age >= kTrendPoints)
                continue;
            const int slot = kTrendPoints - 1 - static_cast<int>(age);
            const int height = std::max(2, round_px(kHeight * value / 100.f));
            c_.fill(slot_x(slot), base - height, slot_x(slot + 1) - slot_x(slot) - 2, height);
        }
    }
    c_.text(x0, 262, "-24H", 2);
    c_.text((c_.width() - Canvas::text_width("-12H", 2)) / 2, 262, "-12H", 2);
    c_.text_right(x0 + span, 262, "NOW", 2);
}

void Screens::clock(const View &v) {
    const int right = c_.width() - kLeft;
    battery(v, right - 32);
    char b[32];
    if (v.clock.valid)
        snprintf(b, sizeof(b), "%s %02u/%02u", kDays[v.clock.weekday % 7], v.clock.month,
                 v.clock.day);
    else
        snprintf(b, sizeof(b), "NO DATE");
    c_.text(kLeft, 14, b, 2);

    // Landscape: 116 px digits with seconds at the right; portrait: 112 px condensed digits.
    const int h = portrait_ ? 112 : 116, w = portrait_ ? 44 : 64, t = portrait_ ? 11 : 12;
    const int top = portrait_ ? 44 : 42;
    if (v.clock.valid)
        snprintf(b, sizeof(b), "%02u:%02u", v.clock.hour, v.clock.minute);
    else
        snprintf(b, sizeof(b), "--:--");
    const int width = c_.segments(kLeft, top, b, w, h, t);
    if (v.settings.seconds && v.clock.valid) {
        snprintf(b, sizeof(b), "%02u", v.clock.second);
        const int sh = portrait_ ? 36 : 40;
        c_.segments(kLeft + width + 10, top + h - sh, b, sh * 5 / 9, sh, std::max(3, sh / 8));
    }

    // The second column starts where the second usage bar starts.
    const int column = portrait_ ? 156 : kLeft + 193;
    const int rule = top + h + (portrait_ ? 10 : 8);
    c_.dotted(kLeft, rule, c_.width() - 2 * kLeft);
    c_.text(kLeft, rule + 8, "ROOM", 2);
    c_.text(column, rule + 8, "HUMIDITY", 2);
    const int value = rule + 28;
    if (v.environment_valid)
        snprintf(b, sizeof(b), "%.1f", v.temperature);
    else
        snprintf(b, sizeof(b), "--");
    int x = kLeft + c_.segments(kLeft, value, b, 22, 40, 5);
    c_.text(x + 6, value, "°C", 2);
    if (v.environment_valid)
        snprintf(b, sizeof(b), "%.0f", v.humidity);
    else
        snprintf(b, sizeof(b), "--");
    x = column + c_.segments(column, value, b, 22, 40, 5);
    c_.text(x + 6, value, "%", 2);

    const int lower = value + (portrait_ ? 52 : 48);
    c_.dotted(kLeft, lower, c_.width() - 2 * kLeft);
    for (int i = 0; i < 2; ++i) {
        // The clock has no OFFLINE header, so retained values are cleared instead of shown.
        const int percent = v.link.encrypted ? compact_percent(v, i) : -1;
        // Landscape places the two services side by side; portrait stacks them in 60 px rows.
        const int bx = portrait_ ? kLeft : kLeft + i * 193;
        const int by = portrait_ ? lower + 10 + i * 60 : lower + 8;
        const int bw = portrait_ ? right - kLeft : 179;
        c_.text(bx, by, kNames[i], 2);
        if (percent >= 0)
            snprintf(b, sizeof(b), "%d%%", percent);
        else
            snprintf(b, sizeof(b), "--");
        c_.text_right(bx + bw, by, b, 2);
        bar(bx, by + 20, bw, portrait_ ? 20 : 14, percent);
    }
}

void Screens::settings(const View &v) {
    header(v, "SETTINGS");
    struct Row {
        const char *label;
        const char *options[4];
    };
    static const Row rows[kSettingRows] = {
        {"AUTO CYCLE", {"OFF", "30S", "1M", "5M"}},
        {"ALERT 95%", {"OFF", "PAGE", "+BEEP"}},
        {"SECONDS", {"ON", "OFF"}},
        {"ROTATION", {"LAND", "PORT", "FLIP"}},
    };
    const auto &s = v.settings;
    const int right = c_.width() - kLeft;
    const int selected[kSettingRows] = {s.cycle, s.alert, s.seconds ? 0 : 1, s.rotation};
    for (int r = 0; r < kSettingRows; ++r) {
        // Landscape puts the options beside the label; portrait puts them below it.
        const int y = portrait_ ? 44 + 74 * r : kContentTop + 48 * r;
        const int label_top = portrait_ ? y + 4 : y + 11;
        // The focused row's label is inverted in place.
        if (r == v.focus)
            chip(kLeft, label_top, rows[r].label);
        else
            c_.text(kLeft, label_top, rows[r].label, 2);
        const int n = setting_options(r);
        const int left = portrait_ ? kLeft : 148, top = portrait_ ? y + 30 : y;
        const float w = (right - left - 4.f * (n - 1)) / n;
        for (int k = 0; k < n; ++k) {
            const int x0 = left + round_px(k * (w + 4)), x1 = left + round_px(k * (w + 4) + w);
            const bool on = k == selected[r];
            if (on)
                c_.fill(x0, top, x1 - x0, 36);
            else
                c_.frame(x0, top, x1 - x0, 36, 2);
            const int tw = Canvas::text_width(rows[r].options[k], 2);
            c_.text(x0 + (x1 - x0 - tw) / 2, top + 11, rows[r].options[k], 2,
                    on ? Ink::White : Ink::Black);
        }
    }
    const int rule = portrait_ ? kPortraitFooterRule : kFooterRule;
    c_.fill(kLeft, rule, right - kLeft, 2);
    if (portrait_) {
        c_.text(kLeft, rule + 10, v.save_error ? "SAVE FAILED" : "KEY NEXT", 2);
        c_.text_right(right, rule + 10, v.save_error ? "" : "BOOT SET", 2);
        c_.text(kLeft, rule + 30, v.save_error ? "BOOT RETRY" : "HOLD EXIT", 2);
    } else if (v.save_error) {
        c_.text(kLeft, rule + 10, "SAVE FAILED / BOOT RETRY", 2);
    } else {
        c_.text(kLeft, rule + 10, "KEY NEXT", 2);
        c_.text((c_.width() - Canvas::text_width("BOOT SET", 2)) / 2, rule + 10, "BOOT SET", 2);
        c_.text_right(right, rule + 10, "HOLD EXIT", 2);
    }
}

void Screens::pairing(const View &v) {
    header(v, nullptr);
    const int right = c_.width() - kLeft;
    c_.text(kLeft, kContentTop, "PAIRING CODE", 2);
    char b[8];
    // Two groups of three digits; landscape 80 px digits, portrait 64 px.
    const int h = portrait_ ? 64 : 80, w = portrait_ ? 36 : 46, t = portrait_ ? 7 : 9;
    snprintf(b, sizeof(b), "%03lu", static_cast<unsigned long>(v.link.passkey / 1000 % 1000));
    const int first = c_.segments(kLeft, 68, b, w, h, t);
    snprintf(b, sizeof(b), "%03lu", static_cast<unsigned long>(v.link.passkey % 1000));
    c_.segments(kLeft + first + w / 2, 68, b, w, h, t);
    const int text = 68 + h + 20;
    c_.text(kLeft, text, "ENTER THIS CODE", 2);
    c_.text(kLeft, text + 20, "ON THE DESKTOP BRIDGE", 2);
    if (portrait_) {
        c_.fill(kLeft, kPortraitFooterRule, right - kLeft, 2);
        c_.text(kLeft, kPortraitFooterRule + 10, v.device_name, 2);
        c_.text(kLeft, kPortraitFooterRule + 30, "KEY NEW CODE", 2);
        return;
    }
    c_.fill(kLeft, kFooterRule, right - kLeft, 2);
    c_.text(kLeft, kFooterRule + 10, v.device_name, 2);
    c_.text_right(right, kFooterRule + 10, "KEY NEW CODE", 2);
}

void Screens::ota(const View &v) {
    header(v, nullptr);
    const int right = c_.width() - kLeft;
    c_.text(kLeft, kContentTop, "FIRMWARE", 2);
    const int percent =
        v.ota.size ? static_cast<int>(std::min<uint64_t>(100, v.ota.offset * 100ULL / v.ota.size)) : 0;
    const int shown = percent / 5 * 5, figure = big_percent_width(72);
    const int available = portrait_ ? right - kLeft : right - kLeft - figure - 12;
    const int version_scale = Canvas::text_width(v.ota.version, 3) <= available ? 3
                              : Canvas::text_width(v.ota.version, 2) <= available ? 2 : 1;
    c_.text(kLeft, 66, v.ota.version, version_scale);
    if (portrait_)
        big_percent(kLeft, 104, shown, 72);
    else
        big_percent(right - figure, kContentTop, shown, 72);
    const int y = portrait_ ? 200 : 142;
    bar(kLeft, y, right - kLeft, 16, percent);
    const char *phase = v.ota.stage == OtaStage::Confirming  ? "CONFIRM UPDATE"
                        : v.ota.stage == OtaStage::Rebooting ? "RESTARTING"
                        : v.ota.stage == OtaStage::Verifying ? "VERIFYING"
                                                             : "WRITING";
    char b[64];
    snprintf(b, sizeof(b), "%s %.1f/%.1f MB", phase, v.ota.offset / 1e6, v.ota.size / 1e6);
    c_.text(kLeft, y + 28, b, 2);
    const bool confirming = v.ota.stage == OtaStage::Confirming;
    if (portrait_) {
        c_.fill(kLeft, kPortraitFooterRule, right - kLeft, 2);
        c_.text(kLeft, kPortraitFooterRule + 10, confirming ? "KEY CONFIRM" : "DO NOT UNPLUG", 2);
        c_.text(kLeft, kPortraitFooterRule + 30, confirming ? "HOLD DENY" : "OR POWER OFF", 2);
        return;
    }
    c_.fill(kLeft, kFooterRule, right - kLeft, 2);
    c_.text(kLeft, kFooterRule + 10,
            confirming ? "KEY CONFIRM / HOLD DENY" : "DO NOT UNPLUG OR POWER OFF", 2);
}

void Screens::render(const View &v) {
    const uint8_t rotation = std::min<uint8_t>(v.settings.rotation, 2);
    portrait_ = rotation == static_cast<uint8_t>(Rotation::Portrait);
    // A retained OTA failure banner takes precedence; the usage alert returns when it expires.
    const bool show_alert = v.alert >= 0 && v.alert < 2 && v.ota.stage == OtaStage::Idle &&
                            !v.ota.error && !v.link.has_passkey && v.page != Page::Settings;
    c_.begin(rotation, show_alert);
    // A 2 px frame 4 px inside the panel edge, like the printed border of a segment LCD.
    c_.frame(kBezel, kBezel, c_.width() - 2 * kBezel, c_.height() - 2 * kBezel, 2,
             show_alert ? Ink::White : Ink::Black);
    if (v.ota.stage != OtaStage::Idle)
        ota(v);
    else if (v.link.has_passkey)
        pairing(v);
    else if (show_alert)
        alert(v);
    else
        switch (v.page) {
        case Page::Home:
            portrait_ ? home_portrait(v) : home(v);
            break;
        case Page::Codex:
        case Page::Claude:
            portrait_ ? home_portrait(v) : focus(v, v.page == Page::Claude);
            break;
        case Page::Trend:
            portrait_ ? home_portrait(v) : trend(v);
            break;
        case Page::Clock:
            clock(v);
            break;
        case Page::Settings:
            settings(v);
            break;
        }
    if (v.ota.error && v.ota.stage == OtaStage::Idle && !show_alert) {
        c_.fill(kLeft, c_.height() - 40, c_.width() - 2 * kLeft, kLine + 10);
        c_.text(kLeft + 6, c_.height() - 35, v.ota.error, 2, Ink::White);
    }
}
} // namespace usage_panel::rlcd
