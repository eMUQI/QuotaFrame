#include "battery_view.hpp"

namespace usage_panel::m5 {

void BatteryStateFilter::push_level(uint8_t percent)
{
    history_[history_pos_] = percent;
    history_pos_ = (history_pos_ + 1) % kMedianWindow;
    if (history_len_ < kMedianWindow) ++history_len_;
}

int BatteryStateFilter::median_level() const
{
    if (history_len_ == 0) return -1;
    uint8_t sorted[kMedianWindow];
    for (uint8_t i = 0; i < history_len_; ++i) sorted[i] = history_[i];
    for (uint8_t i = 1; i < history_len_; ++i) {
        const uint8_t key = sorted[i];
        int j = i - 1;
        while (j >= 0 && sorted[j] > key) {
            sorted[j + 1] = sorted[j];
            --j;
        }
        sorted[j + 1] = key;
    }
    return sorted[history_len_ / 2];  // lower median: one bad sample cannot shift it
}

uint8_t BatteryStateFilter::band_for(uint8_t percent) const
{
    if (percent >= kBandFourEnterPercent) return 4;
    if (view_.band == 4 &&
        percent >= kBandFourEnterPercent - kBandHysteresisPoints) {
        return 4;
    }
    if (percent >= kBandThreeEnterPercent) return 3;
    if ((view_.band == 4 || view_.band == 3) &&
        percent >= kBandThreeEnterPercent - kBandHysteresisPoints) {
        return 3;
    }
    if (percent >= kBandTwoEnterPercent) return 2;
    if (view_.band >= 2 && view_.band <= 4 &&
        percent >= kBandTwoEnterPercent - kBandHysteresisPoints) {
        return 2;
    }
    return 1;
}

void BatteryStateFilter::reset_charge_state()
{
    charge_assert_ = 0;
    charge_release_ = 0;
    charge_shown_ = false;
    full_stable_ = 0;
}

void BatteryStateFilter::settle_band(uint8_t desired)
{
    // Counts consecutive raw samples asking for a different band (the very
    // first reading commits outright), so read bursts cannot walk the
    // segments while genuine drift still lands within five polls.
    if (view_.band == kBandUnset || view_.band == desired) {
        band_pending_ = desired;
        band_pending_count_ = 0;
        view_.band = desired;
        return;
    }
    if (band_pending_ != desired) {
        band_pending_ = desired;
        band_pending_count_ = 0;
    }
    if (++band_pending_count_ >= kBandConfirmSamples) {
        band_pending_count_ = 0;
        view_.band = desired;
    }
}

BatteryView BatteryStateFilter::update(int level_percent, bool charging, int vbus_mv)
{
    // VBUS presence is the trustworthy external-power signal (same philosophy
    // as the ESP32-S3-Touch-AMOLED-2.16 target's AXP2101 path); the LGS4056
    // status pin only
    // refines the charging glyph. When no VBUS reading exists at all, fall
    // back to the status pin for boards without VBUS sensing.
    const bool vbus_valid = vbus_mv > 0;
    const bool vbus_present = vbus_valid && vbus_mv > kVbusPresentMv;
    const bool authoritative_unplug = vbus_valid && !vbus_present;

    // A single normalised sample feeds every consumer below: the median
    // window, the band evidence, and the Full countdown all measure the same
    // clamped value.
    const bool readable = level_percent >= 0;
    const uint8_t sample =
        readable
            ? static_cast<uint8_t>(level_percent > 100 ? 100 : level_percent)
            : 0;

    if (!readable) {
        // Keep the last good level through transient read failures, then
        // expire it so persistent telemetry loss cannot masquerade as a
        // healthy battery.
        if (invalid_streak_ <= kInvalidHoldoverSamples) ++invalid_streak_;
        if (invalid_streak_ > kInvalidHoldoverSamples) {
            history_len_ = 0;
            history_pos_ = 0;
            view_.band = kBandUnset;  // unset: the next valid sample commits afresh
            band_pending_ = kBandUnset;
            band_pending_count_ = 0;
        }
    } else {
        push_level(sample);
        invalid_streak_ = 0;
    }

    if (authoritative_unplug) {
        // A valid VBUS reading below the threshold means the cable is out,
        // whatever the bouncing status pin claims: drop every debounce.
        reset_charge_state();
    } else {
        // Debounce the charge flag on both edges; a single sample on either
        // side of the pin never changes what the icon shows. This also guards
        // the VBUS-unreadable fallback against single bounced releases.
        if (charging) {
            charge_release_ = 0;
            if (charge_assert_ < kChargeEnterSamples) ++charge_assert_;
        } else {
            charge_assert_ = 0;
            if (charge_release_ < kChargeExitSamples) ++charge_release_;
        }
        if (!charge_shown_ && charge_assert_ >= kChargeEnterSamples) {
            charge_shown_ = true;
        } else if (charge_shown_ && charge_release_ >= kChargeExitSamples) {
            charge_shown_ = false;
        }
    }

    const bool external =
        vbus_present || (!vbus_valid && charge_shown_);

    const int median = median_level();
    view_.available = external || median >= 0;
    view_.percent = median < 0 ? 0 : static_cast<uint8_t>(median);
    if (readable) {
        // Band evidence comes straight from the raw clamped sample so the
        // confirmation window measures real threshold crossings.
        settle_band(band_for(sample));
    }

    if (!external) {
        view_.status = BatteryStatus::Battery;
        return view_;
    }
    if (charge_shown_) {
        view_.status = BatteryStatus::Charging;
        full_stable_ = 0;
        return view_;
    }
    // External power without a confirmed charge request. The status pin may
    // still be settling, so Full keeps requiring consecutive confirmation,
    // advanced only by fresh valid full-level samples; an unreadable reading
    // interrupts the countdown rather than coasting on the cached median.
    if (!readable) {
        full_stable_ = 0;
        if (view_.status != BatteryStatus::Full) view_.status = BatteryStatus::Usb;
        return view_;
    }
    // Gate on the current clamped sample, not the cached median: a fresh
    // 99% reading must drop the countdown even while old 100% samples still
    // dominate the median window.
    if (sample >= 100) {
        if (full_stable_ < kFullStableSamples) ++full_stable_;
        if (full_stable_ >= kFullStableSamples) {
            view_.status = BatteryStatus::Full;
            return view_;
        }
        if (view_.status == BatteryStatus::Charging ||
            view_.status == BatteryStatus::Full) {
            return view_;
        }
    } else {
        full_stable_ = 0;
    }
    view_.status = BatteryStatus::Usb;
    return view_;
}

}  // namespace usage_panel::m5
