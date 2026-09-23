#include "orientation.hpp"

#include <cmath>

namespace usage_panel::amoled {
namespace {

// Reject low-magnitude or near-diagonal samples before choosing an axis. The
// absolute threshold requires a sufficiently strong dominant X/Y component;
// the dominance ratio creates a stateless angular dead band around diagonals.
// Neither test distinguishes static tilt from motion on its own.
constexpr float kMinimumDominantAccelerationG = 0.65F;
constexpr float kDominanceRatio = 1.15F;

}  // namespace

std::optional<ScreenRotation> rotation_from_acceleration(float accel_x_g, float accel_y_g)
{
    if (!std::isfinite(accel_x_g) || !std::isfinite(accel_y_g)) {
        return std::nullopt;
    }

    const float abs_x = std::fabs(accel_x_g);
    const float abs_y = std::fabs(accel_y_g);
    const float dominant = abs_x >= abs_y ? abs_x : abs_y;
    const float other = abs_x >= abs_y ? abs_y : abs_x;

    if (dominant < kMinimumDominantAccelerationG || dominant < other * kDominanceRatio) {
        return std::nullopt;
    }

    if (abs_x >= abs_y) {
        // The board's physical mounting requires a counterclockwise 90-degree
        // offset from the raw BSP rotation reference for every direction.
        return accel_x_g >= 0.0F ? ScreenRotation::Deg0 : ScreenRotation::Deg180;
    }
    return accel_y_g >= 0.0F ? ScreenRotation::Deg270 : ScreenRotation::Deg90;
}

TouchMapping touch_mapping(ScreenRotation rotation)
{
    switch (rotation) {
    case ScreenRotation::Deg90:
        return TouchMapping{true, true, false};
    case ScreenRotation::Deg180:
        return TouchMapping{false, true, true};
    case ScreenRotation::Deg270:
        return TouchMapping{true, false, true};
    case ScreenRotation::Deg0:
    default:
        return TouchMapping{false, false, false};
    }
}

std::optional<SwipeDirection> navigation_swipe(
    TouchDirection detected_direction)
{
    if (detected_direction == TouchDirection::Left) {
        return SwipeDirection::Next;
    }
    if (detected_direction == TouchDirection::Right) {
        return SwipeDirection::Previous;
    }
    return std::nullopt;
}

OrientationTracker::OrientationTracker(uint8_t stable_samples)
    : stable_samples_(stable_samples == 0 ? 1 : stable_samples)
{
}

bool OrientationTracker::observe(float accel_x_g, float accel_y_g)
{
    const auto candidate = rotation_from_acceleration(accel_x_g, accel_y_g);
    if (!candidate.has_value()) {
        // Ambiguous samples break the streak rather than preserving it across
        // motion; otherwise two separated stable readings could trigger a
        // rotation even though the panel was never stable between them.
        pending_rotation_.reset();
        pending_samples_ = 0;
        return false;
    }

    if (!pending_rotation_.has_value() || *pending_rotation_ != *candidate) {
        pending_rotation_ = candidate;
        pending_samples_ = 1;
    } else if (pending_samples_ < stable_samples_) {
        ++pending_samples_;
    }

    if (pending_samples_ < stable_samples_) {
        return false;
    }

    // Commit only after consecutive agreement. This temporal debounce is
    // layered on the classifier's angular dead band and prevents LVGL/touch
    // remapping from oscillating on brief ambiguous or changing samples.
    const bool changed = !has_committed_rotation_ || rotation_ != *candidate;
    rotation_ = *candidate;
    has_committed_rotation_ = true;
    pending_rotation_.reset();
    pending_samples_ = 0;
    return changed;
}

}  // namespace usage_panel::amoled
