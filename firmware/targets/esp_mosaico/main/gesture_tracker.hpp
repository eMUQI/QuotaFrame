#pragma once

#include <cstdint>

namespace usage_panel::mosaico {

enum class GestureAction : uint8_t { None, SwipeLeft, SwipeRight, EnterClock, SwipeUp, SwipeDown };
enum class GestureEffect : uint8_t { None, Wake, Previous, Next, Clock };

/** Applies application input policy before any page or screensaver mutation. */
GestureEffect route_gesture(GestureAction action, bool allowed, bool asleep);

/** A detection in normalized, upright screen coordinates; count includes all hands. */
struct HandObservation {
    unsigned count = 0;
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;
    float score = 0;
    float ok_score = 0;
};

/** Time-based single-hand recognition. A completed action requires release before reuse. */
class GestureTracker {
public:
    GestureAction update(const HandObservation& hand, uint64_t now_ms);
    /** Invalidates prior observations and requires fresh, sustained absence of hands. */
    void reset();
    uint8_t hold_progress() const { return progress_; }

private:
    bool waiting_release_ = true;
    bool absent_ = false;
    bool tracking_ = false;
    bool holding_ok_ = false;
    uint64_t absent_since_ = 0;
    uint64_t last_action_ = 0;
    uint64_t started_ = 0;
    uint64_t last_sample_ = 0;
    uint64_t ok_since_ = 0;
    unsigned samples_ = 0;
    uint8_t progress_ = 0;
    HandObservation previous_{};
    float start_x_ = 0;
    float start_y_ = 0;
    float path_x_ = 0;
    float path_y_ = 0;
    float max_dx_ = 0;
    float max_dy_ = 0;
    float ok_x_ = 0;
    float ok_y_ = 0;
};

}  // namespace usage_panel::mosaico
