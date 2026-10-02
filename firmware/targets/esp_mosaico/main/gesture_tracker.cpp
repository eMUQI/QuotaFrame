#include "gesture_tracker.hpp"

#include <algorithm>
#include <cmath>

namespace usage_panel::mosaico {
namespace {
// Dropouts up to this long keep a swipe or OK hold alive. A longer gap restarts
// the track from the next usable sample; it does not require the hand to leave.
// Frames arrive about every 125 ms and slower while the OK classifier or the
// preview runs, so the limit must exceed one frame interval at that cadence.
constexpr uint64_t kMaxSampleGapMs = 400;
// Samples this soon after a swipe belong to the same stroke and cannot repeat it.
constexpr uint64_t kSwipeCooldownMs = 500;
// A swipe opposite to the previous one within this time is the hand returning.
constexpr uint64_t kReturnStrokeMs = 1500;

GestureAction opposite(GestureAction action)
{
    switch (action) {
    case GestureAction::SwipeLeft: return GestureAction::SwipeRight;
    case GestureAction::SwipeRight: return GestureAction::SwipeLeft;
    case GestureAction::SwipeUp: return GestureAction::SwipeDown;
    case GestureAction::SwipeDown: return GestureAction::SwipeUp;
    default: return GestureAction::None;
    }
}
}

GestureEffect route_gesture(GestureAction action, bool allowed, bool asleep)
{
    if (!allowed || action == GestureAction::None) return GestureEffect::None;
    if (action == GestureAction::EnterClock) return asleep ? GestureEffect::None : GestureEffect::Clock;
    if (asleep) return GestureEffect::Wake;
    return action == GestureAction::SwipeRight || action == GestureAction::SwipeDown
        ? GestureEffect::Next : GestureEffect::Previous;
}

void GestureTracker::reset(uint64_t now)
{
    const uint64_t last_action = last_action_;
    const GestureAction last_swipe = last_swipe_;
    *this = GestureTracker{};
    last_action_ = last_action;
    last_swipe_ = last_swipe;
    // A page or screensaver change within the cooldown is the result of that swipe.
    waiting_release_ = last_swipe == GestureAction::None || now - last_action > kSwipeCooldownMs;
}

GestureAction GestureTracker::update(const HandObservation& h, uint64_t now)
{
    if (h.count > 1) {
        // Several hands make the command owner ambiguous; wait for the view to clear.
        absent_ = false;
        tracking_ = holding_ok_ = false;
        waiting_release_ = true;
        progress_ = 0;
        return GestureAction::None;
    }
    const bool usable = h.count == 1 && std::isfinite(h.x) && std::isfinite(h.y) &&
        std::isfinite(h.width) && std::isfinite(h.height) && std::isfinite(h.score) &&
        h.score >= 0.65F && h.x >= 0 && h.x <= 1 && h.y >= 0 && h.y <= 1 &&
        h.width >= 0.06F && h.height >= 0.06F && h.width <= 0.9F && h.height <= 0.95F;
    if (!usable) {
        // Missed and low-confidence frames are dropouts, not new input.
        if (h.count == 0) {
            if (!absent_) { absent_ = true; absent_since_ = now; }
            if (now - absent_since_ >= 300 && now - last_action_ >= 500)
                waiting_release_ = false;
        } else {
            // A weakly detected hand is still present and cannot count as a release.
            absent_ = false;
        }
        if (tracking_ && now - last_sample_ > kMaxSampleGapMs) tracking_ = holding_ok_ = false;
        if (!holding_ok_) progress_ = 0;
        return GestureAction::None;
    }
    absent_ = false;
    progress_ = 0;
    if (waiting_release_) return GestureAction::None;
    if (last_swipe_ != GestureAction::None && now - last_action_ < kSwipeCooldownMs)
        return GestureAction::None;

    if (tracking_ && (now < last_sample_ || now - last_sample_ > kMaxSampleGapMs ||
        std::fabs(h.x - previous_.x) > std::max(0.18F, (h.width + previous_.width) * 0.75F) ||
        std::fabs(h.y - previous_.y) > std::max(0.18F, (h.height + previous_.height) * 0.75F))) {
        tracking_ = holding_ok_ = false;
    }
    if (!tracking_) {
        tracking_ = true;
        started_ = now;
        samples_ = 0;
        start_x_ = h.x;
        start_y_ = h.y;
        path_x_ = path_y_ = max_dx_ = max_dy_ = 0;
        previous_ = h;
    }
    ++samples_;
    path_x_ += std::fabs(h.x - previous_.x);
    path_y_ += std::fabs(h.y - previous_.y);
    max_dx_ = std::max(max_dx_, std::fabs(h.x - start_x_));
    max_dy_ = std::max(max_dy_, std::fabs(h.y - start_y_));
    const float dx = h.x - start_x_;
    const float dy = h.y - start_y_;
    previous_ = h;
    last_sample_ = now;

    GestureAction action = GestureAction::None;
    if (samples_ >= 3 && now - started_ >= 150 && now - started_ <= 900) {
        if (std::fabs(dx) >= 0.25F && std::fabs(dx) >= 2 * max_dy_ &&
            max_dy_ <= 0.15F && std::fabs(dx) >= path_x_ * 0.75F) {
            action = dx < 0 ? GestureAction::SwipeLeft : GestureAction::SwipeRight;
        } else if (std::fabs(dy) >= 0.25F && std::fabs(dy) >= 2 * max_dx_ &&
                   max_dx_ <= 0.15F && std::fabs(dy) >= path_y_ * 0.75F) {
            action = dy < 0 ? GestureAction::SwipeUp : GestureAction::SwipeDown;
        }
    }
    if (action != GestureAction::None && action == opposite(last_swipe_) &&
        now - last_action_ < kReturnStrokeMs) {
        tracking_ = holding_ok_ = false;
        return GestureAction::None;
    }

    if (std::isfinite(h.ok_score) && h.ok_score >= 0.85F) {
        if (!holding_ok_ || std::fabs(h.x - ok_x_) > 0.08F || std::fabs(h.y - ok_y_) > 0.08F) {
            holding_ok_ = true;
            ok_since_ = now;
            ok_x_ = h.x;
            ok_y_ = h.y;
        }
        progress_ = static_cast<uint8_t>(std::min<uint64_t>(100, (now - ok_since_) / 5));
        if (now - ok_since_ >= 500 && samples_ >= 3) action = GestureAction::EnterClock;
    } else {
        holding_ok_ = false;
    }
    if (action != GestureAction::None) {
        // Lowering the hand after OK must not wake the clock, so only OK waits for release.
        waiting_release_ = action == GestureAction::EnterClock;
        last_swipe_ = waiting_release_ ? GestureAction::None : action;
        last_action_ = now;
        tracking_ = holding_ok_ = false;
        progress_ = 0;
    } else if (now - started_ > 900 && !holding_ok_) {
        // A stationary hand may begin a later deliberate swipe without disappearing.
        tracking_ = false;
    }
    return action;
}

}  // namespace usage_panel::mosaico
