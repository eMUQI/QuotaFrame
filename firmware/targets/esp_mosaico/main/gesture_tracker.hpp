#pragma once

#include <cstdint>

namespace usage_panel::mosaico {

enum class GestureAction : uint8_t { None, SwipeLeft, SwipeRight, EnterClock, SwipeUp, SwipeDown, Wave };
enum class GestureEffect : uint8_t { None, Wake, Next, Clock };

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
    float ok_score = 0;  // Confidence of a clock pose: OK or thumbs-up.
};

/**
 * Returns whether consecutive single-hand observations satisfy the pose-classification
 * motion threshold. Skipping classification during motion preserves trajectory sampling cadence.
 */
bool hand_is_steady(const HandObservation& previous, const HandObservation& current);

/**
 * Time-based single-hand recognition. A straight stroke is a swipe; a horizontal
 * back-and-forth motion too small for a swipe is a Wave. One burst of motion issues one
 * command: the next requires the hand to rest or leave the view, and a stroke opposite to
 * the previous swipe is ignored briefly as the hand returning.
 * After EnterClock, several visible hands or reset(), swipes and poses require sustained
 * absence of hands first; a wave does not.
 */
class GestureTracker {
public:
    GestureAction update(const HandObservation& hand, uint64_t now_ms);
    /**
     * Invalidates prior observations; swipes and poses then require sustained absence of hands.
     * An accepted gesture's context change may preserve its settling and release state.
     */
    void reset(bool accepted_gesture = false);
    uint8_t hold_progress() const { return progress_; }

private:
    void begin_stroke(const HandObservation& hand, uint64_t now_ms);

    bool waiting_release_ = true;
    bool absent_ = false;
    bool tracking_ = false;  // previous_ and last_sample_ describe the same hand.
    bool holding_ok_ = false;
    bool settling_ = false;  // A swipe or wave was issued and its motion has not ceased.
    uint64_t absent_since_ = 0;
    uint64_t last_action_ = 0;
    uint64_t last_motion_ = 0;
    GestureAction last_swipe_ = GestureAction::None;  // None after EnterClock.
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
    float leg_x_ = 0;    // Signed horizontal travel since the last reversal.
    unsigned legs_ = 0;  // Completed horizontal legs long enough to belong to a wave.
    float ok_x_ = 0;
    float ok_y_ = 0;
};

}  // namespace usage_panel::mosaico
