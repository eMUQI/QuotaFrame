#pragma once

#include <cstdint>
#include <optional>

#include "usage_panel_state/page_state.hpp"

namespace usage_panel::mosaico {

enum class ScreenRotation : uint8_t {
    Deg0 = 0,
    Deg90,
    Deg180,
    Deg270,
};

enum class TouchDirection : uint8_t { Left, Right, Up, Down };

/**
 * Infers a discrete panel rotation from gravity on X/Y.
 * Returns no value while the reading is weak, non-finite, or too diagonal to
 * choose a dominant axis reliably.
 */
std::optional<ScreenRotation> rotation_from_acceleration(float accel_x_g, float accel_y_g);

/** Maps horizontal touch gestures onto page navigation; vertical gestures are ignored. */
std::optional<SwipeDirection> navigation_swipe(
    TouchDirection detected_direction);

/** Debounces orientation changes by requiring repeated identical candidates. */
class OrientationTracker {
public:
    explicit OrientationTracker(uint8_t stable_samples = 3);

    /** Returns true only when a stable rotation is committed or first established. */
    bool observe(float accel_x_g, float accel_y_g);
    ScreenRotation rotation() const { return rotation_; }

private:
    uint8_t stable_samples_;
    uint8_t pending_samples_ = 0;
    ScreenRotation rotation_ = ScreenRotation::Deg0;
    bool has_committed_rotation_ = false;
    std::optional<ScreenRotation> pending_rotation_;
};

}  // namespace usage_panel::mosaico
