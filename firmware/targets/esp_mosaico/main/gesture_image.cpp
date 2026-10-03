#include "gesture_image.hpp"

#include <algorithm>

namespace usage_panel::mosaico {
namespace {
uint8_t channel(int value)
{
    return static_cast<uint8_t>(std::clamp(value / 256, 0, 255));
}
}

GestureImageSize gesture_image_size(unsigned width, unsigned height, unsigned turns)
{
    if (width < 2 || width > 4096 || (width & 1) || height == 0 || height > 4096 || turns > 3)
        return {};
    GestureImageSize size{kGestureImageWidth, kGestureImageHeight};
    if (width * kGestureImageHeight >= height * kGestureImageWidth)
        size.height = std::max(1U, height * kGestureImageWidth / width);
    else
        size.width = std::max(1U, width * kGestureImageHeight / height);
    if (turns & 1) std::swap(size.width, size.height);
    return size;
}

bool prepare_gesture_image(const uint8_t* src, size_t size, unsigned width,
                           unsigned height, unsigned stride, uint8_t* dst,
                           unsigned turns, bool mirror)
{
    if (!src || !dst || width < 2 || width > 4096 || height == 0 || height > 4096 ||
        (width & 1) || stride < width * 2 || size < size_t(stride) * height || turns > 3)
        return false;
    const auto size_out = gesture_image_size(width, height, 0);
    const unsigned out_width = turns & 1 ? size_out.height : size_out.width;
    for (unsigned y = 0; y < size_out.height; ++y) {
        for (unsigned x = 0; x < size_out.width; ++x) {
            const unsigned sx = x * width / size_out.width;
            const unsigned sy = y * height / size_out.height;
            const uint8_t* p = src + size_t(sy) * stride + (sx & ~1U) * 2;
            const int luma = std::max(0, int(p[(sx & 1) ? 3 : 1]) - 16);
            const int u = int(p[0]) - 128;
            const int v = int(p[2]) - 128;
            const unsigned mx = mirror ? size_out.width - 1 - x : x;
            unsigned ox = mx, oy = y;
            if (turns == 1) { ox = size_out.height - 1 - y; oy = mx; }
            if (turns == 2) { ox = size_out.width - 1 - mx; oy = size_out.height - 1 - y; }
            if (turns == 3) { ox = y; oy = size_out.width - 1 - mx; }
            uint8_t* rgb = dst + (oy * out_width + ox) * 3;
            // UYVY is converted with limited-range BT.601 YCbCr coefficients.
            rgb[0] = channel(298 * luma + 409 * v + 128);
            rgb[1] = channel(298 * luma - 100 * u - 208 * v + 128);
            rgb[2] = channel(298 * luma + 516 * u + 128);
        }
    }
    return true;
}

}  // namespace usage_panel::mosaico
