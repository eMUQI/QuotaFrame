#pragma once

#include <cstdint>

#include "usage_core/usage_state.hpp"

namespace usage_panel {

/**
 * Brightness tier of a screensaver underline.
 *
 * The screensaver never introduces hue: severity only raises brightness, so
 * the clock stays the brightest element on the panel.
 */
enum class ScreensaverUsageLevel : uint8_t { Base, Warn, Bad };

/**
 * One underline below the screensaver clock.
 *
 * A row without data draws an empty track and hides its percentage rather than
 * keeping the last known number on screen.
 */
struct ScreensaverUsageRow {
    bool has_data = false;
    // Fill, label and brightness refer to the same reported window.
    uint8_t percent = 0;
    bool show_percent = false;  // Both valid rows show labels during an alert.
    ScreensaverUsageLevel level = ScreensaverUsageLevel::Base;

    bool operator==(const ScreensaverUsageRow& other) const;
};

/** Both underlines plus the hint text they imply. */
struct ScreensaverUsageView {
    // Indexed like the other provider arrays: 0 is CODEX, 1 is CLAUDE.
    ScreensaverUsageRow rows[2]{};
    bool offline = false;  // True when neither row has data.

    bool operator==(const ScreensaverUsageView& other) const;
};

/** Maps a percentage to its brightness tier at the 80% and 95% thresholds. */
ScreensaverUsageLevel screensaver_usage_level(uint8_t percent);

/**
 * Derives one underline from the cached model.
 *
 * The short window takes precedence; the weekly window is the fallback.
 * Connected providers retain the last valid sample across refresh failures.
 * Only a disconnected link or a provider without valid data clears its row.
 */
ScreensaverUsageRow make_screensaver_usage_row(const UsageModel& model, Provider provider, bool connected);

/** Derives both underlines, and whether the wake hint becomes OFFLINE. */
ScreensaverUsageView make_screensaver_usage_view(const UsageModel& model, bool connected);

}  // namespace usage_panel
