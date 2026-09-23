#pragma once

#include <cstdint>

namespace usage_panel::m5 {

enum class BatteryStatus : uint8_t { Battery, Usb, Charging, Full };

/** Stable battery presentation for the corner icon. The icon renders only
 *  available/status/band, so equality deliberately ignores percent: the raw
 *  telemetry jitters inside a band and a changed render key would trigger
 *  pointless full-screen redraws. */
struct BatteryView {
    bool available = false;
    BatteryStatus status = BatteryStatus::Battery;
    uint8_t percent = 0;
    uint8_t band = 0;  // unset or 1..4 filled segments

    bool operator==(const BatteryView& other) const
    {
        return available == other.available && status == other.status &&
               band == other.band;
    }
};

/** Filters raw M5Unified readings into a stable battery display state.
 *
 *  StickS3 telemetry is noisy: getBatteryLevel() maps a raw voltage whose
 *  single bad samples swing tens of percent, and isCharging() mirrors the
 *  LGS4056 status pin, which bounces every few seconds. The filter takes a
 *  median over recent samples for the displayed level, debounces the charge
 *  flag on both edges, rides the stable VBUS reading for USB presence, and
 *  requires consecutive agreeing samples before showing Full. */
class BatteryStateFilter {
public:
    BatteryView update(int level_percent, bool charging, int vbus_mv);

private:
    static constexpr int kVbusPresentMv = 4000;
    /** Band 0 marks "never committed since boot or expiry"; 1..4 are drawn. */
    static constexpr uint8_t kBandUnset = 0;
    static constexpr uint8_t kMedianWindow = 5;
    static constexpr uint8_t kChargeEnterSamples = 2;
    // Observed status-pin bursts stay under ~4 samples; a real charge-end
    // holds the released level indefinitely, so require a longer run to drop
    // the bolt than to raise it.
    static constexpr uint8_t kChargeExitSamples = 6;
    static constexpr uint8_t kFullStableSamples = 3;
    static constexpr uint8_t kBandConfirmSamples = 5;
    // Percentage at which each segment count is entered on the way up. A band
    // is held until the reading falls kBandHysteresisPoints below its entry
    // threshold, so telemetry jitter at the entry percentages cannot flicker
    // the segment count. Matches the ESP32-S3-Touch-AMOLED-2.16 target's
    // PowerStateFilter.
    static constexpr uint8_t kBandFourEnterPercent = 75;
    static constexpr uint8_t kBandThreeEnterPercent = 50;
    static constexpr uint8_t kBandTwoEnterPercent = 25;
    static constexpr uint8_t kBandHysteresisPoints = 3;
    // Mirrors the ESP32-S3-Touch-AMOLED-2.16 filter's holdover: transient
    // read failures keep the
    // last good level, a longer run must expire it instead of presenting a
    // stale battery forever.
    static constexpr uint8_t kInvalidHoldoverSamples = 15;

    void push_level(uint8_t percent);
    int median_level() const;
    uint8_t band_for(uint8_t percent) const;
    void reset_charge_state();
    void settle_band(uint8_t desired);

    BatteryView view_{};
    uint8_t history_[kMedianWindow]{};
    uint8_t history_len_ = 0;
    uint8_t history_pos_ = 0;
    uint8_t charge_assert_ = 0;
    uint8_t charge_release_ = 0;
    bool charge_shown_ = false;
    uint8_t full_stable_ = 0;
    uint8_t band_pending_ = 0;
    uint8_t band_pending_count_ = 0;
    uint8_t invalid_streak_ = 0;
};

}  // namespace usage_panel::m5
