#include "gesture_image.hpp"

#include <algorithm>

namespace usage_panel::mosaico {
namespace {
uint8_t channel(int value)
{
    return static_cast<uint8_t>(std::clamp(value / 256, 0, 255));
}
}

bool prepare_gesture_image(const uint8_t* src, size_t size, unsigned width,
                           unsigned height, unsigned stride, uint8_t* dst,
                           unsigned turns, bool mirror)
{
    if (!src || !dst || width < 2 || width > 4096 || height == 0 || height > 4096 ||
        (width & 1) || stride < width * 2 || size < size_t(stride) * height || turns > 3)
        return false;
    const unsigned out_width = turns & 1 ? kGestureImageHeight : kGestureImageWidth;
    for (unsigned y = 0; y < kGestureImageHeight; ++y) {
        for (unsigned x = 0; x < kGestureImageWidth; ++x) {
            const unsigned sx = x * width / kGestureImageWidth;
            const unsigned sy = y * height / kGestureImageHeight;
            const uint8_t* p = src + size_t(sy) * stride + (sx & ~1U) * 2;
            const int luma = std::max(0, int(p[(sx & 1) ? 3 : 1]) - 16);
            const int u = int(p[0]) - 128;
            const int v = int(p[2]) - 128;
            const unsigned mx = mirror ? kGestureImageWidth - 1 - x : x;
            unsigned ox = mx, oy = y;
            if (turns == 1) { ox = kGestureImageHeight - 1 - y; oy = mx; }
            if (turns == 2) { ox = kGestureImageWidth - 1 - mx; oy = kGestureImageHeight - 1 - y; }
            if (turns == 3) { ox = y; oy = kGestureImageWidth - 1 - mx; }
            uint8_t* rgb = dst + (oy * out_width + ox) * 3;
            // OV3640 UYVY uses the limited-range BT.601 YCbCr conversion.
            rgb[0] = channel(298 * luma + 409 * v + 128);
            rgb[1] = channel(298 * luma - 100 * u - 208 * v + 128);
            rgb[2] = channel(298 * luma + 516 * u + 128);
        }
    }
    return true;
}

}  // namespace usage_panel::mosaico
