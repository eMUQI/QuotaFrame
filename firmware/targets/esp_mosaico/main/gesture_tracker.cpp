#include "gesture_tracker.hpp"

#include <algorithm>
#include <cmath>

namespace usage_panel::mosaico {

GestureEffect route_gesture(GestureAction action, bool allowed, bool asleep)
{
    if (!allowed || action == GestureAction::None) return GestureEffect::None;
    if (action == GestureAction::EnterClock) return asleep ? GestureEffect::None : GestureEffect::Clock;
    if (asleep) return GestureEffect::Wake;
    return action == GestureAction::SwipeLeft ? GestureEffect::Next : GestureEffect::Previous;
}

void GestureTracker::reset()
{
    const uint64_t last_action = last_action_;
    *this = GestureTracker{};
    last_action_ = last_action;
}

GestureAction GestureTracker::update(const HandObservation& h, uint64_t now)
{
    progress_ = 0;
    if (h.count == 0) {
        if (!absent_) { absent_ = true; absent_since_ = now; }
        if (now - absent_since_ >= 300 && now - last_action_ >= 500)
            waiting_release_ = false;
        tracking_ = false;
        holding_ok_ = false;
        return GestureAction::None;
    }
    absent_ = false;
    const bool valid = h.count == 1 && std::isfinite(h.x) && std::isfinite(h.y) &&
        std::isfinite(h.width) && std::isfinite(h.height) && std::isfinite(h.score) &&
        h.score >= 0.65F && h.x >= 0 && h.x <= 1 && h.y >= 0 && h.y <= 1 &&
        h.width >= 0.06F && h.height >= 0.06F && h.width <= 0.9F && h.height <= 0.95F;
    if (!valid) {
        tracking_ = false;
        holding_ok_ = false;
        waiting_release_ = true;
    }
    if (waiting_release_) return GestureAction::None;

    if (tracking_ && (now < last_sample_ || now - last_sample_ > 250 ||
        std::fabs(h.x - previous_.x) > std::max(0.18F, (h.width + previous_.width) * 0.75F) ||
        std::fabs(h.y - previous_.y) > std::max(0.18F, (h.height + previous_.height) * 0.75F))) {
        reset();
        return GestureAction::None;
    }
    if (!tracking_) {
        tracking_ = true;
        started_ = now;
        samples_ = 0;
        start_x_ = h.x;
        start_y_ = h.y;
        path_x_ = max_dy_ = 0;
        previous_ = h;
    }
    ++samples_;
    path_x_ += std::fabs(h.x - previous_.x);
    max_dy_ = std::max(max_dy_, std::fabs(h.y - start_y_));
    const float dx = h.x - start_x_;
    previous_ = h;
    last_sample_ = now;

    GestureAction action = GestureAction::None;
    if (samples_ >= 3 && now - started_ >= 150 && now - started_ <= 900 &&
        std::fabs(dx) >= 0.25F && std::fabs(dx) >= 2 * max_dy_ &&
        max_dy_ <= 0.15F && std::fabs(dx) >= path_x_ * 0.75F) {
        action = dx < 0 ? GestureAction::SwipeLeft : GestureAction::SwipeRight;
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
        waiting_release_ = true;
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
