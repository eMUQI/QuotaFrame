#include "usage_panel_state/screensaver_state.hpp"

#include <cmath>

namespace usage_panel {
namespace {

constexpr uint64_t kHintHoldMs = 3000;
constexpr uint64_t kHintFadeMs = 400;
constexpr uint64_t kShiftIntervalMs = 60000;
constexpr uint64_t kTouchGuardMs = 300;
// Pressing the PWR key jolts the panel hard enough to trip shake-to-wake, so a
// deliberate lock ignores incidental wake input for this long.
constexpr uint64_t kWakeGuardMs = 500;
constexpr uint64_t kShakeSampleWindowMs = 150;
constexpr uint64_t kShakeEventWindowMs = 1500;
constexpr float kDegreesToRadians = 3.14159265358979F / 180.0F;

}  // namespace

bool ScreensaverView::operator==(const ScreensaverView& other) const
{
    return active == other.active && hint_opacity == other.hint_opacity &&
           offset_x == other.offset_x && offset_y == other.offset_y &&
           consume_touch == other.consume_touch;
}

ScreensaverController::ScreensaverController(uint64_t idle_delay_ms)
    : idle_delay_ms_(idle_delay_ms)
{
}

void ScreensaverController::reset(uint64_t now_ms, Page page)
{
    active_ = false;
    consume_touch_ = false;
    last_activity_ms_ = now_ms;
    active_since_ms_ = 0;
    next_shift_ms_ = 0;
    ignore_touch_until_ms_ = 0;
    ignore_wake_until_ms_ = 0;
    restore_page_ = page;
    offset_x_ = 0;
    offset_y_ = 0;
}

void ScreensaverController::enter(uint64_t now_ms, Page current_page)
{
    active_ = true;
    active_since_ms_ = now_ms;
    next_shift_ms_ = now_ms + kShiftIntervalMs;
    restore_page_ = current_page;
    offset_x_ = 0;
    offset_y_ = 0;
    consume_touch_ = false;
    ignore_wake_until_ms_ = 0;
}

void ScreensaverController::exit(uint64_t now_ms)
{
    active_ = false;
    last_activity_ms_ = now_ms;
    offset_x_ = 0;
    offset_y_ = 0;
}

void ScreensaverController::note_remote_toggle(uint64_t now_ms, Page current_page)
{
    if (active_) {
        exit(now_ms);
        return;
    }
    enter(now_ms, current_page);
}

void ScreensaverController::note_power_key(uint64_t now_ms, Page current_page)
{
    note_remote_toggle(now_ms, current_page);
    if (active_) ignore_wake_until_ms_ = now_ms + kWakeGuardMs;
}

void ScreensaverController::note_touch_down(uint64_t now_ms)
{
    if (active_) {
        // Holding the panel to reach the PWR key can land a touch of its own.
        // Input is already disabled while active, so ignoring it is enough.
        if (now_ms < ignore_wake_until_ms_) return;
        exit(now_ms);
        consume_touch_ = true;
        ignore_touch_until_ms_ = now_ms + kTouchGuardMs;
        return;
    }
    if (now_ms < ignore_touch_until_ms_) {
        consume_touch_ = true;
        return;
    }
    consume_touch_ = false;
    last_activity_ms_ = now_ms;
}

void ScreensaverController::note_shake(uint64_t now_ms)
{
    if (active_) {
        if (now_ms < ignore_wake_until_ms_) return;
        exit(now_ms);
    }
    last_activity_ms_ = now_ms;
}

uint32_t ScreensaverController::next_random()
{
    random_state_ ^= random_state_ << 13;
    random_state_ ^= random_state_ >> 17;
    random_state_ ^= random_state_ << 5;
    return random_state_;
}

void ScreensaverController::choose_offset()
{
    const int8_t old_x = offset_x_;
    const int8_t old_y = offset_y_;
    do {
        offset_x_ = static_cast<int8_t>(next_random() % 21U) - 10;
        offset_y_ = static_cast<int8_t>(next_random() % 21U) - 10;
    } while (offset_x_ == old_x && offset_y_ == old_y);
}

void ScreensaverController::update(
    uint64_t now_ms, Page current_page, bool pairing, bool ota)
{
    if (pairing || ota) {
        if (active_) exit(now_ms);
        last_activity_ms_ = now_ms;
        consume_touch_ = false;
        return;
    }

    if (!active_) {
        if (idle_delay_ms_ != 0 && now_ms - last_activity_ms_ >= idle_delay_ms_) {
            enter(now_ms, current_page);
        }
        return;
    }

    while (now_ms >= next_shift_ms_) {
        choose_offset();
        next_shift_ms_ += kShiftIntervalMs;
    }
}

ScreensaverView ScreensaverController::view(uint64_t now_ms) const
{
    uint8_t opacity = 0;
    if (active_) {
        const uint64_t elapsed = now_ms - active_since_ms_;
        if (elapsed <= kHintHoldMs) {
            opacity = 255;
        } else if (elapsed < kHintHoldMs + kHintFadeMs) {
            opacity = static_cast<uint8_t>(
                255U * (kHintHoldMs + kHintFadeMs - elapsed) /
                kHintFadeMs);
        }
    }
    return {
        .active = active_,
        .hint_opacity = opacity,
        .offset_x = offset_x_,
        .offset_y = offset_y_,
        .consume_touch =
            consume_touch_ && now_ms < ignore_touch_until_ms_,
    };
}

bool ScreensaverController::touch_allowed(uint64_t now_ms) const
{
    return now_ms >= ignore_touch_until_ms_;
}

Page ScreensaverController::restore_page() const
{
    return restore_page_;
}

ShakeDetector::ShakeDetector(float excursion_threshold_g)
    : excursion_threshold_g_(excursion_threshold_g)
{
}

bool ShakeDetector::update(
    float ax_g, float ay_g, float az_g, uint64_t now_ms)
{
    if (has_sample_ && now_ms - sample_ms_ > kShakeSampleWindowMs) {
        has_sample_ = false;
    }
    if (has_event_ && now_ms - event_ms_ > kShakeEventWindowMs) {
        has_event_ = false;
    }

    const float magnitude =
        std::sqrt(ax_g * ax_g + ay_g * ay_g + az_g * az_g);
    if (std::fabs(magnitude - 1.0F) <= excursion_threshold_g_) {
        has_sample_ = false;
        return false;
    }

    if (!has_sample_) {
        has_sample_ = true;
        sample_ms_ = now_ms;
        return false;
    }

    has_sample_ = false;
    if (!has_event_) {
        has_event_ = true;
        event_ms_ = now_ms;
        return false;
    }

    has_event_ = false;
    return true;
}

TiltWakeDetector::TiltWakeDetector(float wake_degrees)
    : wake_cos_(std::cos(wake_degrees * kDegreesToRadians))
{
}

bool TiltWakeDetector::update(float ax_g, float ay_g, float az_g)
{
    if (!armed_) {
        rest_x_ = ax_g;
        rest_y_ = ay_g;
        rest_z_ = az_g;
        armed_ = true;
        return false;
    }

    const float sample_mag =
        std::sqrt(ax_g * ax_g + ay_g * ay_g + az_g * az_g);
    const float rest_mag =
        std::sqrt(rest_x_ * rest_x_ + rest_y_ * rest_y_ + rest_z_ * rest_z_);
    if (sample_mag < 1e-4F || rest_mag < 1e-4F) {
        above_frames_ = 0;
        return false;
    }
    const float cosine = (ax_g * rest_x_ + ay_g * rest_y_ + az_g * rest_z_) /
                         (sample_mag * rest_mag);

    if (cosine < wake_cos_) {
        if (++above_frames_ >= kConfirmFrames) {
            above_frames_ = 0;
            // Snap the reference to the current pose so a completed pick-up
            // fires only once; the next wake requires a new reorientation.
            rest_x_ = ax_g;
            rest_y_ = ay_g;
            rest_z_ = az_g;
            return true;
        }
    } else {
        above_frames_ = 0;
    }

    // Keep the reference slowly tracking slow pose drift while the device is
    // at rest, so gentle sliding does not accumulate into a false tilt.
    rest_x_ += kSlowAlpha * (ax_g - rest_x_);
    rest_y_ += kSlowAlpha * (ay_g - rest_y_);
    rest_z_ += kSlowAlpha * (az_g - rest_z_);
    return false;
}

}  // namespace usage_panel
