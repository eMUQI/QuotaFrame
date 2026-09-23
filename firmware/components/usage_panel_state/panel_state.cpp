#include "usage_panel_state/panel_state.hpp"

#include <cstring>

namespace usage_panel {
namespace {

constexpr const char* kWeekdays[] = {
    "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT",
};
constexpr const char* kMonths[] = {
    "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
    "JUL", "AUG", "SEP", "OCT", "NOV", "DEC",
};

// Percentage at which each battery segment count is entered on the way up.
constexpr uint8_t kBandFourEnterPercent = 75;
constexpr uint8_t kBandThreeEnterPercent = 50;
constexpr uint8_t kBandTwoEnterPercent = 25;

// A band is held until the reading falls this far below its entry threshold.
// Without the margin, normal sample jitter at the entry percentages makes the
// segment count flicker.
constexpr uint8_t kBandHysteresisPoints = 3;

// Transient power-source read failures reuse the last good view for this long.
// A longer failure must present Unavailable rather than a stale battery.
constexpr uint64_t kInvalidHoldoverMs = 15000;

// Charge-done can briefly assert around termination, so it must stay asserted
// this long before the UI commits to the four-segment FULL presentation.
constexpr uint64_t kFullConfirmMs = 5000;

}  // namespace

bool ClockView::operator==(const ClockView& other) const
{
    return available == other.available &&
           std::strcmp(time, other.time) == 0 &&
           std::strcmp(weekday, other.weekday) == 0 &&
           std::strcmp(month_day, other.month_day) == 0;
}

bool BatteryView::operator==(const BatteryView& other) const
{
    return available == other.available && band == other.band &&
           charging == other.charging && full == other.full &&
           low_on_battery == other.low_on_battery;
}

ClockView format_clock_view(const LocalCalendarTime* calendar)
{
    ClockView view{};
    std::memcpy(view.time, "--:--", sizeof(view.time));
    if (!calendar || !is_valid_calendar(*calendar)) return view;

    view.available = true;
    view.time[0] = static_cast<char>('0' + calendar->hour / 10);
    view.time[1] = static_cast<char>('0' + calendar->hour % 10);
    view.time[3] = static_cast<char>('0' + calendar->minute / 10);
    view.time[4] = static_cast<char>('0' + calendar->minute % 10);
    std::memcpy(view.weekday, kWeekdays[calendar->weekday], 3);
    std::memcpy(view.month_day, kMonths[calendar->month - 1], 3);
    view.month_day[3] = ' ';
    view.month_day[4] = static_cast<char>('0' + calendar->day / 10);
    view.month_day[5] = static_cast<char>('0' + calendar->day % 10);
    return view;
}

BatteryBand PowerStateFilter::band_for(uint8_t percent) const
{
    if (percent >= kBandFourEnterPercent) return BatteryBand::Four;
    if (band_ == BatteryBand::Four &&
        percent >= kBandFourEnterPercent - kBandHysteresisPoints) {
        return BatteryBand::Four;
    }
    if (percent >= kBandThreeEnterPercent) return BatteryBand::Three;
    if ((band_ == BatteryBand::Four || band_ == BatteryBand::Three) &&
        percent >= kBandThreeEnterPercent - kBandHysteresisPoints) {
        return BatteryBand::Three;
    }
    if (percent >= kBandTwoEnterPercent) return BatteryBand::Two;
    if (band_ != BatteryBand::One && band_ != BatteryBand::Unavailable &&
        percent >= kBandTwoEnterPercent - kBandHysteresisPoints) {
        return BatteryBand::Two;
    }
    return BatteryBand::One;
}

BatteryView PowerStateFilter::update(
    const RawPowerSample& sample, uint64_t now_ms)
{
    if (!sample.valid || sample.percent > 100) {
        has_candidate_ = false;
        if (has_last_good_ && now_ms - last_good_ms_ <= kInvalidHoldoverMs) {
            return last_good_;
        }
        has_last_good_ = false;
        band_ = BatteryBand::Unavailable;
        stable_full_ = false;
        return {};
    }

    if (!sample.battery_present) {
        has_last_good_ = false;
        band_ = BatteryBand::Unavailable;
        has_candidate_ = false;
        stable_full_ = false;
        return {};
    }

    const bool target_full = sample.vbus_present && sample.charge_done;
    if (!target_full) {
        stable_full_ = false;
        has_candidate_ = false;
    } else if (stable_full_) {
        has_candidate_ = false;
    } else if (!has_candidate_) {
        candidate_since_ms_ = now_ms;
        has_candidate_ = true;
    } else if (now_ms - candidate_since_ms_ >= kFullConfirmMs) {
        stable_full_ = true;
        has_candidate_ = false;
    }

    const bool charging = sample.vbus_present &&
        (sample.charging || sample.charge_done);
    band_ = band_for(sample.percent);
    if (stable_full_) band_ = BatteryBand::Four;
    BatteryView view{
        .available = true,
        .band = band_,
        .charging = charging,
        .full = stable_full_,
        .low_on_battery = sample.percent < kBandTwoEnterPercent && !charging,
        .percent = sample.percent,
    };
    last_good_ = view;
    last_good_ms_ = now_ms;
    has_last_good_ = true;
    return view;
}

OtaPowerReading derive_power_reading(
    const BatteryView& view, const RawPowerSample& sample)
{
    const bool external_power = sample.valid && sample.vbus_present;
    OtaPowerReading reading{};
    reading.external_power = external_power;
    reading.valid = view.available || external_power;
    reading.percent = view.percent;
    return reading;
}

}  // namespace usage_panel
