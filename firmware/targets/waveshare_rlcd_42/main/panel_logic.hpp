#pragma once
#include "view.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
namespace usage_panel::rlcd {
/**
 * Window lengths used for pace markers. usage.v1 publishes only reset times, so the
 * lengths are fixed: Codex and Claude both use a 5-hour short window and a 7-day week.
 */
constexpr uint32_t kShortWindowSeconds = 5 * 3600;
constexpr uint32_t kWeekWindowSeconds = 7 * 86400;

/** Usage compared with the fraction of the window that has elapsed. */
struct Pace {
    bool valid = false;
    uint32_t elapsed_s = 0;
    uint32_t window_s = 0;
    int delta = 0; // Used percent minus elapsed percent; positive is over pace.
};

/**
 * Computes the pace of one window at `now_epoch`.
 * Invalid when the window has no reset time, the reset has passed, or the remaining
 * time exceeds `window_s` (the fixed length does not match the provider's window).
 */
Pace compute_pace(const UsageWindow &window, uint32_t now_epoch, uint32_t window_s);

/** Elapsed fraction of the window in [0, 1]; 0 for an invalid pace. */
float pace_fraction(const Pace &pace);

/** Short-window countdown "H:MM:SS" (or "H:MM" without seconds); "WAIT" once due; "--" without a reset. */
void format_short_countdown(const UsageWindow &window, uint32_t now_epoch, bool seconds, char *out,
                            size_t size);

/** Week countdown "5D 02:03"; "WAIT" once due; "--" without a reset. */
void format_week_countdown(const UsageWindow &window, uint32_t now_epoch, char *out, size_t size);

/** Elapsed duration as "3H36M", or "2D 04H" from one day. */
void format_elapsed(uint32_t seconds, char *out, size_t size);

/** Sample age: "NOW" below a minute, then "12M AGO", then "3H AGO" from 100 minutes. */
void format_age(uint32_t seconds, char *out, size_t size);

/**
 * Warning level with hysteresis: 2 from 95 % (held down to 93 %), 1 from 80 % (held down
 * to 78 %), otherwise 0.
 */
constexpr uint8_t warning_level(uint8_t previous, uint8_t used) {
    if (used >= 95 || (previous == 2 && used >= 93))
        return 2;
    if (used >= 80 || (previous >= 1 && used >= 78))
        return 1;
    return 0;
}

/** Whether the short window of a provider has a value that may be displayed. */
bool short_present(const ProviderSnapshot &s, bool encrypted);
/** Whether the week window of a provider has a value that may be displayed. */
bool week_present(const ProviderSnapshot &s, bool encrypted);

enum class Input : uint8_t { None, Key, KeyHold, Boot };

/** Effects of one navigation step that the application loop must carry out. */
struct NavResult {
    bool redraw = false;
    bool save = false; // Settings changed and must be persisted.
};

/** Page ring, alert dismissal and auto cycling for the KEY / BOOT button pair. */
class Navigator {
  public:
    /** Next ring page; portrait orientation has a Home / Clock ring. */
    static Page next(Page page, bool portrait);
    /** Previous ring page; returns Home when `page` is outside the current ring. */
    static Page previous(Page page, bool portrait);

    /** Applies one button input to `view`. `now_ms` restarts the auto-cycle timer. */
    NavResult input(Input input, View &view, uint64_t now_ms);

    /**
     * Updates the alert shown in `view.alert` from the warning levels.
     * A dismissed alert stays hidden until its provider drops below level 2.
     * @return true on the first presentation for a provider at level 2; reconnects do not re-arm it.
     */
    bool update_alert(View &view);

    /** Advances the ring when the configured auto-cycle interval has elapsed. */
    bool auto_cycle(View &view, uint64_t now_ms);

    /** Moves to Home when the current page does not exist in the current orientation. */
    static bool normalize(View &view);

  private:
    Page return_page_ = Page::Home;
    std::array<bool, 2> dismissed_{};
    std::array<bool, 2> announced_{};
    uint64_t last_step_ms_ = 0;
};

/** Auto-cycle interval in milliseconds for a Settings::cycle value; 0 disables cycling. */
uint32_t cycle_interval_ms(uint8_t cycle);

/** Number of options on a Settings row. */
int setting_options(int row);
} // namespace usage_panel::rlcd
