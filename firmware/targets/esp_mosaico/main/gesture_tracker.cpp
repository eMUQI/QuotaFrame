#include "gesture_tracker.hpp"

#include <algorithm>
#include <cmath>

namespace usage_panel::mosaico {
namespace {
// Short detection gaps preserve the current swipe or pose hold. Longer gaps restart
// tracking at the next usable sample. The limit allows for classification and preview latency.
constexpr uint64_t kMaxSampleGapMs = 400;
// After a swipe or wave, motion must cease for this long before the next swipe or wave, so the
// follow-through, an immediate return and the rest of a longer wave cannot repeat it.
constexpr uint64_t kSettleMs = 400;
// An opposite swipe within this interval is treated as the return stroke.
constexpr uint64_t kReturnStrokeMs = 1500;
constexpr uint64_t kOkHoldMs = 500;
// A completed hold requires at least three samples because each gap is shorter than the hold.
static_assert(kMaxSampleGapMs < kOkHoldMs);
// Frame-to-frame movement up to this distance is treated as detector jitter.
constexpr float kRestStep = 0.03F;
// Larger frame-to-frame movement skips pose classification to preserve motion sampling cadence.
constexpr float kSteadyStep = 0.05F;
// Each of the three alternating horizontal wave legs must cover this fraction of the image width.
constexpr float kWaveLeg = 0.12F;

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
    // Swipes and waves share forward-only navigation.
    return GestureEffect::Next;
}

bool hand_is_steady(const HandObservation& previous, const HandObservation& current)
{
    return previous.count == 1 && current.count == 1 &&
        std::fabs(current.x - previous.x) <= kSteadyStep &&
        std::fabs(current.y - previous.y) <= kSteadyStep;
}

void GestureTracker::reset(bool accepted_gesture)
{
    const uint64_t last_action = last_action_;
    const uint64_t last_motion = last_motion_;
    const GestureAction last_swipe = last_swipe_;
    const bool waiting_release = waiting_release_;
    const bool settling = settling_;
    *this = GestureTracker{};
    if (accepted_gesture) {
        last_action_ = last_action;
        last_motion_ = last_motion;
        last_swipe_ = last_swipe;
        waiting_release_ = waiting_release;
        settling_ = settling;
    }
}

void GestureTracker::begin_stroke(const HandObservation& h, uint64_t now)
{
    started_ = now;
    samples_ = 1;
    start_x_ = h.x;
    start_y_ = h.y;
    path_x_ = path_y_ = max_dx_ = max_dy_ = 0;
}

GestureAction GestureTracker::update(const HandObservation& h, uint64_t now)
{
    if (h.count > 1) {
        // Multiple hands invalidate ownership and require release before another swipe or pose.
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
            // A weakly detected hand is still present and cannot count as a release. Motion
            // blur lowers the score, so it does not count as rest either.
            absent_ = false;
            last_motion_ = now;
        }
        if (tracking_ && now - last_sample_ > kMaxSampleGapMs) tracking_ = holding_ok_ = false;
        if (!holding_ok_) progress_ = 0;
        return GestureAction::None;
    }
    absent_ = false;
    progress_ = 0;

    const bool continuous = tracking_ && now >= last_sample_ && now - last_sample_ <= kMaxSampleGapMs &&
        std::fabs(h.x - previous_.x) <= std::max(0.18F, (h.width + previous_.width) * 0.75F) &&
        std::fabs(h.y - previous_.y) <= std::max(0.18F, (h.height + previous_.height) * 0.75F);
    const float step_x = continuous ? h.x - previous_.x : 0;
    const float step_y = continuous ? h.y - previous_.y : 0;
    tracking_ = true;
    previous_ = h;
    last_sample_ = now;
    if (std::fabs(step_x) > kRestStep || std::fabs(step_y) > kRestStep) last_motion_ = now;
    const bool rested = now - last_motion_ >= kSettleMs;

    if (!continuous) holding_ok_ = false;
    if (!continuous || rested) {
        leg_x_ = 0;
        legs_ = 0;
    }
    if (settling_ && rested) settling_ = false;
    if (!continuous || rested) {
        begin_stroke(h, now);
    } else {
        ++samples_;
        path_x_ += std::fabs(step_x);
        path_y_ += std::fabs(step_y);
        max_dx_ = std::max(max_dx_, std::fabs(h.x - start_x_));
        max_dy_ = std::max(max_dy_, std::fabs(h.y - start_y_));
        // A hand still at its origin has not started a stroke. The origin follows it, so the
        // stroke window opens when the hand starts to move rather than when it appeared.
        if (max_dx_ <= kRestStep && max_dy_ <= kRestStep) begin_stroke(h, now);
    }
    if (std::fabs(step_x) > kRestStep) {
        if (leg_x_ * step_x < 0) {
            if (std::fabs(leg_x_) >= kWaveLeg) ++legs_;
            leg_x_ = 0;
        }
        leg_x_ += step_x;
    }
    const float dx = h.x - start_x_;
    const float dy = h.y - start_y_;

    GestureAction action = GestureAction::None;
    if (!settling_) {
        // Hand withdrawal after a pose or touch can resemble a swipe, so swipes require release.
        // Waves use alternating horizontal motion as their confirmation criterion.
        if (!waiting_release_ && samples_ >= 3 && now - started_ >= 150 && now - started_ <= 900) {
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
            begin_stroke(h, now);
            holding_ok_ = false;
            return GestureAction::None;
        }
        if (action == GestureAction::None && legs_ >= 2 && std::fabs(leg_x_) >= kWaveLeg &&
            path_x_ >= 2 * path_y_) {
            action = GestureAction::Wave;
        }
    }

    if (!waiting_release_ && std::isfinite(h.ok_score) && h.ok_score >= 0.85F) {
        if (!holding_ok_ || std::fabs(h.x - ok_x_) > 0.08F || std::fabs(h.y - ok_y_) > 0.08F) {
            holding_ok_ = true;
            ok_since_ = now;
            ok_x_ = h.x;
            ok_y_ = h.y;
        }
        progress_ = static_cast<uint8_t>(std::min<uint64_t>(100, (now - ok_since_) * 100 / kOkHoldMs));
        if (now - ok_since_ >= kOkHoldMs) action = GestureAction::EnterClock;
    } else {
        holding_ok_ = false;
    }
    if (action != GestureAction::None) {
        // Clock poses require release before another swipe or pose to suppress withdrawal-triggered wakeups.
        waiting_release_ = action == GestureAction::EnterClock;
        last_swipe_ = waiting_release_ ? GestureAction::None : action;
        last_action_ = last_motion_ = now;
        settling_ = !waiting_release_;
        holding_ok_ = false;
        progress_ = 0;
        leg_x_ = 0;
        legs_ = 0;
    } else if (now - started_ > 900) {
        // Restart expired candidates so subsequent motion can form a new stroke.
        begin_stroke(h, now);
    }
    return action;
}

}  // namespace usage_panel::mosaico
