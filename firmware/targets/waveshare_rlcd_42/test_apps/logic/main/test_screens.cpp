#include "canvas.hpp"
#include "screens.hpp"
#include "unity.h"
#include <cstring>

using namespace usage_panel;
using namespace usage_panel::rlcd;

namespace {
Canvas canvas;

bool black(int x, int y) {
    const int n = y * Canvas::kNativeWidth + x;
    return canvas.data()[n >> 3] & (0x80 >> (n & 7));
}

int black_pixels() {
    int count = 0;
    for (int i = 0; i < Canvas::kBytes; ++i)
        count += __builtin_popcount(canvas.data()[i]);
    return count;
}
} // namespace

TEST_CASE("bezel, header rule and battery outline land on the design coordinates", "[render]")
{
    Screens screens(canvas);
    View v;
    v.link = {true, true, false, 0};
    screens.render(v);
    // 2 px bezel from 4 px inside the edge; 2 px rule at y 34-35 from x 14 to 385;
    // 28 x 18 battery frame from x 354, y 12.
    TEST_ASSERT_TRUE(black(4, 4));
    TEST_ASSERT_TRUE(black(395, 295));
    TEST_ASSERT_FALSE(black(3, 3));
    TEST_ASSERT_TRUE(black(14, 34));
    TEST_ASSERT_TRUE(black(385, 35));
    TEST_ASSERT_FALSE(black(13, 34));
    TEST_ASSERT_FALSE(black(386, 34));
    TEST_ASSERT_TRUE(black(354, 12));
    TEST_ASSERT_TRUE(black(381, 29));
}

TEST_CASE("alert inverts the whole frame", "[render]")
{
    Screens screens(canvas);
    View v;
    v.link = {true, true, false, 0};
    v.alert = 1;
    screens.render(v);
    TEST_ASSERT_TRUE(black(0, 0));
    TEST_ASSERT_TRUE(black(399, 299));
    TEST_ASSERT_GREATER_THAN(Canvas::kNativeWidth * Canvas::kNativeHeight / 2, black_pixels());
}

TEST_CASE("portrait and flipped frames map into the native buffer", "[render]")
{
    canvas.begin(uint8_t(Rotation::Portrait));
    TEST_ASSERT_EQUAL_INT(300, canvas.width());
    canvas.pixel(0, 0, Ink::Black);
    TEST_ASSERT_TRUE(black(399, 0));
    canvas.begin(uint8_t(Rotation::Flipped));
    canvas.pixel(0, 0, Ink::Black);
    TEST_ASSERT_TRUE(black(399, 299));
}

TEST_CASE("pixel font and seven-segment widths follow the cell grid", "[render]")
{
    // 6 px cells with the trailing blank column dropped, times the scale.
    TEST_ASSERT_EQUAL_INT(10, Canvas::text_width("A", 2));
    TEST_ASSERT_EQUAL_INT(34, Canvas::text_width("ABC", 2));
    TEST_ASSERT_EQUAL_INT(Canvas::text_width("ABC", 2), Canvas::text_width("abc", 2));
    // Characters without a glyph are skipped.
    TEST_ASSERT_EQUAL_INT(Canvas::text_width("AB", 2), Canvas::text_width("A\xe2\x80\xa2" "B", 2));
    // Every digit has a fixed cell, a 1 included; half digits are one segment wide; digits are
    // separated by t / 2 + 2 px.
    TEST_ASSERT_EQUAL_INT(8 + 6 + 40 + 6 + 40, Canvas::segments_width("h88", 40, 8));
    TEST_ASSERT_EQUAL_INT(Canvas::segments_width("88", 40, 8), Canvas::segments_width("11", 40, 8));
    canvas.begin(uint8_t(Rotation::Landscape));
    canvas.text(0, 0, "I", 2);
    // The I glyph's top row is columns 1-3, drawn 2 px per font pixel.
    TEST_ASSERT_FALSE(black(1, 0));
    TEST_ASSERT_TRUE(black(2, 0));
    TEST_ASSERT_TRUE(black(7, 1));
    TEST_ASSERT_FALSE(black(8, 0));
}

TEST_CASE("colons have equal space on both sides", "[render]")
{
    // Pixel font at 2x: 8 covers x 0-9, the colon dot x 16-17, the next 8 starts at x 24.
    canvas.begin(uint8_t(Rotation::Landscape));
    canvas.text(0, 0, "8:8", 2);
    for (int y = 0; y < 14; ++y) {
        for (int x = 10; x < 16; ++x)
            TEST_ASSERT_FALSE(black(x, y));
        for (int x = 18; x < 24; ++x)
            TEST_ASSERT_FALSE(black(x, y));
    }
    TEST_ASSERT_TRUE(black(16, 2));
    TEST_ASSERT_TRUE(black(24, 2));
    // Segment digits with an even stroke fill their 64 x 116 cell to its last column and row, so
    // the 8 px gaps before and after the colon are equal.
    canvas.begin(uint8_t(Rotation::Landscape));
    canvas.segments(0, 0, "8:8", 64, 116, 12);
    TEST_ASSERT_TRUE(black(63, 30));
    TEST_ASSERT_TRUE(black(30, 115));
    TEST_ASSERT_TRUE(black(64 + 8, 38));
    TEST_ASSERT_FALSE(black(64 + 7, 38));
    TEST_ASSERT_FALSE(black(64 + 8 + 12, 38));
    TEST_ASSERT_TRUE(black(64 + 8 + 12 + 8, 30));
}

TEST_CASE("trend advances offline and excludes expired or future samples", "[render]")
{
    Screens screens(canvas);
    View v;
    v.page = Page::Trend;
    v.sd = true;
    UsageUpdate u;
    u.provider = Provider::Codex;
    u.state = SourceState::Ok;
    u.sent_at = u.sampled_at = 1800000000;
    u.short_window = {true, 100, false, 0};
    u.week_window = {true, 50, false, 0};
    TEST_ASSERT_TRUE(v.model.apply(u, 0));
    v.trend_count = 1;
    v.trend[0] = {u.sent_at, 100, 50, {}};
    // 48 slots span x 14-385: the newest bar covers x 378-383, the one before it x 370-375.
    screens.render(v);
    TEST_ASSERT_TRUE(black(380, 90));
    v.now_ms = 1800000;
    screens.render(v);
    TEST_ASSERT_FALSE(black(380, 90));
    TEST_ASSERT_TRUE(black(372, 90));
    v.now_ms = 48ULL * 3600 * 1000;
    v.trend_count = 0;
    screens.render(v);
    static uint8_t empty[Canvas::kBytes];
    memcpy(empty, canvas.data(), sizeof(empty));
    v.trend_count = 1;
    screens.render(v);
    TEST_ASSERT_EQUAL_MEMORY(empty, canvas.data(), sizeof(empty));
    v.trend[0].epoch = u.sent_at + 49 * 3600;
    screens.render(v);
    TEST_ASSERT_EQUAL_MEMORY(empty, canvas.data(), sizeof(empty));
}

TEST_CASE("full usage stays clear of the neighbouring column", "[render]")
{
    Screens screens(canvas);
    View v;
    v.page = Page::Codex;
    v.link = {true, true, false, 0};
    UsageUpdate u;
    u.provider = Provider::Codex;
    u.state = SourceState::Ok;
    u.sent_at = u.sampled_at = 1800000000;
    u.short_window = {true, 100, false, 0};
    u.week_window = {true, 50, false, 0};
    TEST_ASSERT_TRUE(v.model.apply(u, 0));
    // The percent field ends at x 222 on the detail page, before the column at x 240, and at
    // x 130 on Home, before the bar column at x 148.
    screens.render(v);
    for (int y = 70; y < 186; ++y)
        for (int x = 223; x < 240; ++x)
            TEST_ASSERT_FALSE_MESSAGE(black(x, y), "Usage digits cross the column gutter");
    v.page = Page::Home;
    screens.render(v);
    for (int y = 68; y < 132; ++y)
        for (int x = 131; x < 148; ++x)
            TEST_ASSERT_FALSE_MESSAGE(black(x, y), "Usage digits reach the Home bar column");
}
