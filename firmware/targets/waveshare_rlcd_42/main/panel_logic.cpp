#include "panel_logic.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
namespace usage_panel::rlcd {
Pace compute_pace(const UsageWindow &window, uint32_t now_epoch, uint32_t window_s) {
    Pace pace;
    if (!window.present || !window.has_reset || !now_epoch || window.reset_at <= now_epoch)
        return pace;
    const uint32_t remaining = window.reset_at - now_epoch;
    if (remaining > window_s)
        return pace;
    pace.valid = true;
    pace.window_s = window_s;
    pace.elapsed_s = window_s - remaining;
    const int elapsed_percent =
        static_cast<int>((static_cast<uint64_t>(pace.elapsed_s) * 100 + window_s / 2) / window_s);
    pace.delta = window.used_percent - elapsed_percent;
    return pace;
}

float pace_fraction(const Pace &pace) {
    return pace.valid ? static_cast<float>(pace.elapsed_s) / pace.window_s : 0.f;
}

namespace {
bool countdown_special(const UsageWindow &window, uint32_t now_epoch, char *out, size_t size) {
    if (!window.has_reset || !now_epoch) {
        snprintf(out, size, "--");
        return true;
    }
    if (window.reset_at <= now_epoch) {
        snprintf(out, size, "WAIT");
        return true;
    }
    return false;
}
} // namespace

void format_short_countdown(const UsageWindow &window, uint32_t now_epoch, bool seconds, char *out,
                            size_t size) {
    if (countdown_special(window, now_epoch, out, size))
        return;
    const uint32_t s = window.reset_at - now_epoch;
    if (seconds)
        snprintf(out, size, "%lu:%02lu:%02lu", static_cast<unsigned long>(s / 3600),
                 static_cast<unsigned long>(s / 60 % 60), static_cast<unsigned long>(s % 60));
    else
        snprintf(out, size, "%lu:%02lu", static_cast<unsigned long>(s / 3600),
                 static_cast<unsigned long>(s / 60 % 60));
}

void format_week_countdown(const UsageWindow &window, uint32_t now_epoch, char *out, size_t size) {
    if (countdown_special(window, now_epoch, out, size))
        return;
    const uint32_t s = window.reset_at - now_epoch;
    snprintf(out, size, "%luD %02lu:%02lu", static_cast<unsigned long>(s / 86400),
             static_cast<unsigned long>(s / 3600 % 24), static_cast<unsigned long>(s / 60 % 60));
}

void format_elapsed(uint32_t seconds, char *out, size_t size) {
    if (seconds >= 86400)
        snprintf(out, size, "%luD %02luH", static_cast<unsigned long>(seconds / 86400),
                 static_cast<unsigned long>(seconds / 3600 % 24));
    else
        snprintf(out, size, "%luH%02luM", static_cast<unsigned long>(seconds / 3600),
                 static_cast<unsigned long>(seconds / 60 % 60));
}

void format_age(uint32_t seconds, char *out, size_t size) {
    if (seconds < 60)
        snprintf(out, size, "NOW");
    else if (seconds < 100 * 60)
        snprintf(out, size, "%luM AGO", static_cast<unsigned long>(seconds / 60));
    else
        snprintf(out, size, "%luH AGO", static_cast<unsigned long>(seconds / 3600));
}

// Offline panels keep showing the last values; online panels show only windows carried by
// the latest publication.
bool short_present(const ProviderSnapshot &s, bool encrypted) {
    return s.has_valid_data && s.short_window.present && (s.latest_short_present || !encrypted);
}
bool week_present(const ProviderSnapshot &s, bool encrypted) {
    return s.has_valid_data && s.week_window.present && (s.latest_week_present || !encrypted);
}

uint32_t cycle_interval_ms(uint8_t cycle) {
    constexpr uint32_t intervals[] = {0, 30000, 60000, 300000};
    return cycle < 4 ? intervals[cycle] : 0;
}

int setting_options(int row) {
    constexpr int options[kSettingRows] = {4, 3, 2, 3};
    return row >= 0 && row < kSettingRows ? options[row] : 0;
}

Page Navigator::next(Page page, bool portrait) {
    if (portrait)
        return page == Page::Home ? Page::Clock : Page::Home;
    switch (page) {
    case Page::Home:
        return Page::Codex;
    case Page::Codex:
        return Page::Claude;
    case Page::Claude:
        return Page::Trend;
    case Page::Trend:
        return Page::Clock;
    default:
        return Page::Home;
    }
}

Page Navigator::previous(Page page, bool portrait) {
    Page p = page;
    for (int i = 0; i < kRingPages; ++i) {
        if (next(p, portrait) == page)
            return p;
        p = next(p, portrait);
    }
    return Page::Home;
}

bool Navigator::normalize(View &v) {
    const bool portrait = v.settings.rotation == static_cast<uint8_t>(Rotation::Portrait);
    if (portrait && (v.page == Page::Codex || v.page == Page::Claude || v.page == Page::Trend)) {
        v.page = Page::Home;
        return true;
    }
    return false;
}

NavResult Navigator::input(Input in, View &v, uint64_t now_ms) {
    NavResult r;
    if (in == Input::None)
        return r;
    last_step_ms_ = now_ms;
    r.redraw = true;
    const bool portrait = v.settings.rotation == static_cast<uint8_t>(Rotation::Portrait);
    if (v.alert >= 0 && v.page != Page::Settings) {
        if (in == Input::Key) {
            dismissed_[v.alert] = true;
            v.alert = -1;
        }
        return r;
    }
    if (v.page == Page::Settings) {
        if (in == Input::Key)
            v.focus = (v.focus + 1) % kSettingRows;
        else if (in == Input::KeyHold) {
            v.page = return_page_;
            normalize(v);
        } else if (v.save_error) {
            // Retries the rejected value; the footer shows BOOT RETRY in this state.
            r.save = true;
        } else {
            auto &s = v.settings;
            uint8_t *values[kSettingRows] = {&s.cycle, &s.alert, &s.seconds, &s.rotation};
            uint8_t &value = *values[v.focus];
            // SECONDS lists ON before OFF, so its option index is the inverse of the flag.
            if (v.focus == 2)
                value = value ? 0 : 1;
            else
                value = static_cast<uint8_t>((value + 1) % setting_options(v.focus));
            r.save = true;
        }
        return r;
    }
    if (in == Input::KeyHold) {
        return_page_ = v.page;
        v.page = Page::Settings;
        v.focus = 0;
    } else if (in == Input::Key)
        v.page = previous(v.page, portrait);
    else
        v.page = next(v.page, portrait);
    return r;
}

bool Navigator::update_alert(View &v) {
    int best = -1;
    for (int i = 0; i < 2; ++i) {
        if (v.warning[i] < 2) {
            dismissed_[i] = false;
            announced_[i] = false;
        }
        const auto &s = v.model.snapshot(i ? Provider::Claude : Provider::Codex);
        if (v.warning[i] == 2 && !dismissed_[i] && v.link.encrypted &&
            (best < 0 || s.short_window.used_percent >
                             v.model.snapshot(best ? Provider::Claude : Provider::Codex)
                                 .short_window.used_percent))
            best = i;
    }
    if (v.settings.alert == static_cast<uint8_t>(AlertMode::Off))
        best = -1;
    const bool raised = best >= 0 && !announced_[best];
    if (best >= 0)
        announced_[best] = true;
    v.alert = best;
    return raised;
}

bool Navigator::auto_cycle(View &v, uint64_t now_ms) {
    const uint32_t interval = cycle_interval_ms(v.settings.cycle);
    if (!interval || v.page == Page::Settings || v.alert >= 0) {
        last_step_ms_ = now_ms;
        return false;
    }
    if (now_ms - last_step_ms_ < interval)
        return false;
    last_step_ms_ = now_ms;
    v.page = next(v.page, v.settings.rotation == static_cast<uint8_t>(Rotation::Portrait));
    return true;
}
} // namespace usage_panel::rlcd
