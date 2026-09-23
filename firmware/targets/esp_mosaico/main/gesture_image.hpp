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

/** Fits the complete frame within 320x240, preserving aspect ratio before rotation.
 * Returns zero dimensions for invalid input or an unsupported rotation.
 */
GestureImageSize gesture_image_size(unsigned width, unsigned height, unsigned quarter_turns);

/** Downsamples packed UYVY to RGB888, mirrors horizontally, then rotates clockwise.
 * Output capacity must be at least kGestureImageBytes. Actual packed dimensions
 * are returned by gesture_image_size(); no cropping or padding is applied.
 */
bool prepare_gesture_image(const uint8_t* src, size_t size, unsigned width,
                           unsigned height, unsigned stride, uint8_t* dst,
                           unsigned quarter_turns, bool mirror);

}  // namespace usage_panel::mosaico
