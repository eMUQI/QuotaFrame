#pragma once

#include <cstdint>

#include "usage_ota/power_gate.hpp"
#include "usage_protocol/time_sync.hpp"

namespace usage_panel {

enum class BatteryBand : uint8_t { Unavailable, One, Two, Three, Four };

/** Preformatted clock strings used as part of the render key. */
struct ClockView {
    bool available = false;
    char time[6] = "--:--";
    char weekday[4]{};
    char month_day[7]{};

    bool operator==(const ClockView& other) const;
};

/** Stable battery presentation after filtering noisy battery samples. */
struct BatteryView {
    bool available = false;
    BatteryBand band = BatteryBand::Unavailable;
    bool charging = false;
    bool full = false;
    bool low_on_battery = false;
    uint8_t percent = 0;

    bool operator==(const BatteryView& other) const;
};

/** One unfiltered battery observation from the target's power source. */
struct RawPowerSample {
    bool valid = false;
    bool battery_present = false;
    bool vbus_present = false;
    bool charging = false;
    bool charge_done = false;
    uint8_t percent = 0;
};

/** Formats a validated local calendar for the fixed 24-hour panel UI. */
ClockView format_clock_view(const LocalCalendarTime* calendar);

/**
 * Converts battery samples into a stable four-band battery UI.
 *
 * Band boundaries use hysteresis so a percentage hovering around 25/50/75%
 * does not flicker between adjacent segment counts. Brief invalid samples reuse
 * the last good view, and the charge-done signal must remain stable before
 * the UI declares FULL.
 */
class PowerStateFilter {
public:
    /** Updates the filtered view; `now_ms` is monotonic milliseconds. */
    BatteryView update(const RawPowerSample& sample, uint64_t now_ms);

private:
    BatteryBand band_for(uint8_t percent) const;

    BatteryView last_good_{};
    BatteryBand band_ = BatteryBand::Unavailable;
    bool has_last_good_ = false;
    uint64_t last_good_ms_ = 0;

    bool stable_full_ = false;
    bool has_candidate_ = false;
    uint64_t candidate_since_ms_ = 0;
};

/** Derives the OTA power-gate verdict from the loop's filtered view and its
 *  latest raw sample. External power comes only from the raw sample because
 *  BatteryView deliberately folds battery-less boards into unavailable. */
OtaPowerReading derive_power_reading(
    const BatteryView& view, const RawPowerSample& sample);

}  // namespace usage_panel
