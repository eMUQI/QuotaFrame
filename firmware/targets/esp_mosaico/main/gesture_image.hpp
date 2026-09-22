#pragma once

#include <cstddef>
#include <cstdint>

namespace usage_panel::mosaico {

constexpr unsigned kGestureImageWidth = 320;
constexpr unsigned kGestureImageHeight = 240;
constexpr size_t kGestureImageBytes = kGestureImageWidth * kGestureImageHeight * 3;

/** Downsamples packed UYVY to RGB888, mirrors horizontally, then rotates clockwise.
 * Output capacity must be at least kGestureImageBytes. Odd rotations yield 240x320.
 */
bool prepare_gesture_image(const uint8_t* src, size_t size, unsigned width,
                           unsigned height, unsigned stride, uint8_t* dst,
                           unsigned quarter_turns, bool mirror);

}  // namespace usage_panel::mosaico
