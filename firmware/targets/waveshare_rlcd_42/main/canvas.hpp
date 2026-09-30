#pragma once
#include <cstdint>
namespace usage_panel::rlcd {
/** How a primitive or glyph sets its pixels. */
enum class Ink : uint8_t { Black, White };

/**
 * One-bit drawing surface for the 400 x 300 panel.
 *
 * Coordinates are logical: 400 x 300 in landscape and flipped landscape, 300 x 400 in portrait.
 * The buffer is stored in native landscape order, row-major, MSB first, 1 = black.
 *
 * Text uses a built-in 5 x 7 monospaced pixel font drawn at an integer `scale`, so every stroke
 * is `scale` pixels wide and falls on the pixel grid. Each character occupies a 6 x 7 cell
 * (one blank column); the line box is 7 * scale pixels high.
 */
class Canvas {
  public:
    static constexpr int kNativeWidth = 400, kNativeHeight = 300;
    static constexpr int kBytes = kNativeWidth * kNativeHeight / 8;

    /** Clears to black or white and selects the logical orientation for later drawing. */
    void begin(uint8_t rotation, bool black_background = false);
    int width() const { return width_; }
    int height() const { return height_; }
    const uint8_t *data() const { return bits_; }

    void pixel(int x, int y, Ink ink);
    void fill(int x, int y, int w, int h, Ink ink = Ink::Black);
    void frame(int x, int y, int w, int h, int thickness, Ink ink = Ink::Black);
    /** Horizontal dotted rule: `t` x `t` squares separated by `t` pixels. */
    void dotted(int x, int y, int w, int t = 2, Ink ink = Ink::Black);

    /**
     * Draws pixel-font text with its cell top at `top`. ASCII letters are drawn in uppercase;
     * characters without a glyph are skipped. @return advance width in pixels.
     */
    int text(int x, int top, const char *s, int scale, Ink ink = Ink::Black);
    /** Draws text right-aligned so its last glyph column ends at `right` - 1. */
    int text_right(int right, int top, const char *s, int scale, Ink ink = Ink::Black);
    /** Width of the drawn text without the trailing blank column. */
    static int text_width(const char *s, int scale);

    /**
     * Draws seven-segment digits in a `w` x `h` box per digit with `t`-pixel segments.
     *
     * Accepted characters: `0`-`9`, `-` (middle segment), space (blank digit), `h` (blank half
     * digit), `H` (half digit showing 1), `:` and `.` (square dots `t` wide). As on a printed
     * segment LCD every digit occupies a fixed cell, so a `1` lights the right-hand segments of
     * its cell; half digits and dots are one segment wide. Digits are separated by `t / 2 + 2`
     * pixels. Unlit segments leave the background unchanged.
     * @return total width in pixels.
     */
    int segments(int x, int top, const char *s, int w, int h, int t, Ink ink = Ink::Black);
    static int segments_width(const char *s, int w, int t);

  private:
    uint8_t bits_[kBytes]{};
    uint8_t rotation_ = 0;
    int width_ = kNativeWidth, height_ = kNativeHeight;
    void segment(int left, int top, int w, int h, int t, int index, Ink ink);
};
} // namespace usage_panel::rlcd
