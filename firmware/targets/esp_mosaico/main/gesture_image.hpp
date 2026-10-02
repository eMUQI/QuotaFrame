#pragma once

#include <cstddef>
#include <cstdint>

namespace usage_panel::mosaico {

constexpr unsigned kGestureImageWidth = 320;
constexpr unsigned kGestureImageHeight = 240;
constexpr size_t kGestureImageBytes = kGestureImageWidth * kGestureImageHeight * 3;

struct GestureImageSize {
    unsigned width;
    unsigned height;
};

/**
 * Fits the complete frame within 320x240 before rotation, preserving its aspect ratio
 * to integer-pixel precision. Odd quarter-turn counts swap the output dimensions.
 * @param width Source width in pixels; must be even and within 2..4096.
 * @param height Source height in pixels; must be within 1..4096.
 * @param quarter_turns Clockwise rotation in 90-degree steps; valid range is 0..3.
 * @return Output dimensions, or zero dimensions if any argument is invalid.
 */
GestureImageSize gesture_image_size(unsigned width, unsigned height, unsigned quarter_turns);

/**
 * Converts the complete UYVY frame to RGB888 using gesture_image_size() dimensions,
 * then applies horizontal mirroring and clockwise rotation. Output has no row padding.
 * Both buffers remain caller-owned and must not overlap; neither pointer is retained.
 * @param src Non-null source buffer containing packed UYVY rows with optional row padding.
 * @param size Source buffer size in bytes; must be at least stride * height.
 * @param width Source width in pixels; must be even and within 2..4096.
 * @param height Source height in pixels; must be within 1..4096.
 * @param stride Source row stride in bytes; must be at least width * 2.
 * @param dst Non-null output buffer with at least kGestureImageBytes capacity; capacity is not checked.
 * @param quarter_turns Clockwise rotation in 90-degree steps; valid range is 0..3.
 * @param mirror Whether to mirror horizontally before rotation.
 * @return True after conversion; false for null pointers, invalid dimensions or rotation,
 *         insufficient source size, or an invalid stride. Validation failure leaves dst unchanged.
 */
bool prepare_gesture_image(const uint8_t* src, size_t size, unsigned width,
                           unsigned height, unsigned stride, uint8_t* dst,
                           unsigned quarter_turns, bool mirror);

}  // namespace usage_panel::mosaico
