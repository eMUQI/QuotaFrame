#include "canvas.hpp"
#include "view.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
namespace usage_panel::rlcd {
namespace {
// 5 x 7 glyph rows, bit 4 = leftmost column. ASCII 32-95; lowercase maps to uppercase.
// Punctuation dots are one column wide in the centre column, so the space before and after
// them is equal inside the 6-column cell.
constexpr uint8_t kAscii[64][7] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // space
    {0x04,0x04,0x04,0x04,0x04,0x00,0x04}, // !
    {0x0a,0x0a,0x00,0x00,0x00,0x00,0x00}, // "
    {0x0a,0x0a,0x1f,0x0a,0x1f,0x0a,0x0a}, // #
    {0x04,0x0f,0x14,0x0e,0x05,0x1e,0x04}, // $
    {0x18,0x19,0x02,0x04,0x08,0x13,0x03}, // %
    {0x0c,0x12,0x14,0x08,0x15,0x12,0x0d}, // &
    {0x04,0x04,0x00,0x00,0x00,0x00,0x00}, // '
    {0x02,0x04,0x08,0x08,0x08,0x04,0x02}, // (
    {0x08,0x04,0x02,0x02,0x02,0x04,0x08}, // )
    {0x00,0x04,0x15,0x0e,0x15,0x04,0x00}, // *
    {0x00,0x04,0x04,0x1f,0x04,0x04,0x00}, // +
    {0x00,0x00,0x00,0x00,0x06,0x04,0x08}, // ,
    {0x00,0x00,0x00,0x1f,0x00,0x00,0x00}, // -
    {0x00,0x00,0x00,0x00,0x00,0x00,0x04}, // .
    {0x01,0x02,0x02,0x04,0x08,0x08,0x10}, // /
    {0x0e,0x11,0x13,0x15,0x19,0x11,0x0e}, // 0
    {0x04,0x0c,0x04,0x04,0x04,0x04,0x0e}, // 1
    {0x0e,0x11,0x01,0x02,0x04,0x08,0x1f}, // 2
    {0x1f,0x02,0x04,0x02,0x01,0x11,0x0e}, // 3
    {0x02,0x06,0x0a,0x12,0x1f,0x02,0x02}, // 4
    {0x1f,0x10,0x1e,0x01,0x01,0x11,0x0e}, // 5
    {0x06,0x08,0x10,0x1e,0x11,0x11,0x0e}, // 6
    {0x1f,0x01,0x02,0x04,0x08,0x08,0x08}, // 7
    {0x0e,0x11,0x11,0x0e,0x11,0x11,0x0e}, // 8
    {0x0e,0x11,0x11,0x0f,0x01,0x02,0x0c}, // 9
    {0x00,0x04,0x04,0x00,0x04,0x04,0x00}, // :
    {0x00,0x04,0x04,0x00,0x04,0x04,0x08}, // ;
    {0x02,0x04,0x08,0x10,0x08,0x04,0x02}, // <
    {0x00,0x00,0x1f,0x00,0x1f,0x00,0x00}, // =
    {0x08,0x04,0x02,0x01,0x02,0x04,0x08}, // >
    {0x0e,0x11,0x01,0x02,0x04,0x00,0x04}, // ?
    {0x0e,0x11,0x17,0x15,0x17,0x10,0x0e}, // @
    {0x0e,0x11,0x11,0x1f,0x11,0x11,0x11}, // A
    {0x1e,0x11,0x11,0x1e,0x11,0x11,0x1e}, // B
    {0x0e,0x11,0x10,0x10,0x10,0x11,0x0e}, // C
    {0x1e,0x11,0x11,0x11,0x11,0x11,0x1e}, // D
    {0x1f,0x10,0x10,0x1e,0x10,0x10,0x1f}, // E
    {0x1f,0x10,0x10,0x1e,0x10,0x10,0x10}, // F
    {0x0e,0x11,0x10,0x17,0x11,0x11,0x0f}, // G
    {0x11,0x11,0x11,0x1f,0x11,0x11,0x11}, // H
    {0x0e,0x04,0x04,0x04,0x04,0x04,0x0e}, // I
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0c}, // J
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, // K
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1f}, // L
    {0x11,0x1b,0x15,0x15,0x11,0x11,0x11}, // M
    {0x11,0x11,0x19,0x15,0x13,0x11,0x11}, // N
    {0x0e,0x11,0x11,0x11,0x11,0x11,0x0e}, // O
    {0x1e,0x11,0x11,0x1e,0x10,0x10,0x10}, // P
    {0x0e,0x11,0x11,0x11,0x15,0x12,0x0d}, // Q
    {0x1e,0x11,0x11,0x1e,0x14,0x12,0x11}, // R
    {0x0f,0x10,0x10,0x0e,0x01,0x01,0x1e}, // S
    {0x1f,0x04,0x04,0x04,0x04,0x04,0x04}, // T
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0e}, // U
    {0x11,0x11,0x11,0x11,0x11,0x0a,0x04}, // V
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0a}, // W
    {0x11,0x11,0x0a,0x04,0x0a,0x11,0x11}, // X
    {0x11,0x11,0x0a,0x04,0x04,0x04,0x04}, // Y
    {0x1f,0x01,0x02,0x04,0x08,0x10,0x1f}, // Z
    {0x0e,0x08,0x08,0x08,0x08,0x08,0x0e}, // [
    {0x10,0x08,0x08,0x04,0x02,0x02,0x01}, // backslash
    {0x0e,0x02,0x02,0x02,0x02,0x02,0x0e}, // ]
    {0x04,0x0a,0x11,0x00,0x00,0x00,0x00}, // ^
    {0x00,0x00,0x00,0x00,0x00,0x00,0x1f}, // _
};
constexpr uint8_t kDegree[7] = {0x0c,0x12,0x12,0x0c,0x00,0x00,0x00};

constexpr int kCell = 6, kRows = 7;

uint32_t next_codepoint(const unsigned char *&p) {
    uint32_t c = *p++;
    int extra = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
    if (extra)
        c &= 0x3f >> extra;
    while (extra-- && (*p & 0xc0) == 0x80)
        c = (c << 6) | (*p++ & 0x3f);
    if (c >= 'a' && c <= 'z')
        c -= 32;
    return c;
}

const uint8_t *glyph_for(uint32_t c) {
    if (c >= 32 && c < 96)
        return kAscii[c - 32];
    if (c == 0xb0)
        return kDegree;
    return nullptr;
}

int glyph_count(const char *s) {
    int n = 0;
    for (auto *p = reinterpret_cast<const unsigned char *>(s); *p;)
        n += glyph_for(next_codepoint(p)) != nullptr;
    return n;
}

// Segment bits a-g: top, upper right, lower right, bottom, lower left, upper left, middle.
constexpr uint8_t kDigitSegments[10] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};

int digit_gap(int t) { return t / 2 + 2; }

/** Width of one seven-segment character; half digits and dots are one segment wide. */
int char_width(char c, int w, int t) {
    return c == 'h' || c == 'H' || c == ':' || c == '.' ? t : w;
}
} // namespace

void Canvas::begin(uint8_t rotation, bool black_background) {
    rotation_ = rotation;
    const bool portrait = rotation == static_cast<uint8_t>(Rotation::Portrait);
    width_ = portrait ? kNativeHeight : kNativeWidth;
    height_ = portrait ? kNativeWidth : kNativeHeight;
    memset(bits_, black_background ? 0xff : 0x00, sizeof(bits_));
}

void Canvas::pixel(int x, int y, Ink ink) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_)
        return;
    const bool black = ink == Ink::Black;
    int nx = x, ny = y;
    if (rotation_ == static_cast<uint8_t>(Rotation::Portrait)) {
        // Portrait is the landscape panel turned 90 degrees clockwise.
        nx = kNativeWidth - 1 - y;
        ny = x;
    } else if (rotation_ == static_cast<uint8_t>(Rotation::Flipped)) {
        nx = kNativeWidth - 1 - x;
        ny = kNativeHeight - 1 - y;
    }
    const int n = ny * kNativeWidth + nx;
    const uint8_t mask = 0x80 >> (n & 7);
    if (black)
        bits_[n >> 3] |= mask;
    else
        bits_[n >> 3] &= ~mask;
}

void Canvas::fill(int x, int y, int w, int h, Ink ink) {
    for (int j = std::max(0, y); j < std::min(height_, y + h); ++j)
        for (int i = std::max(0, x); i < std::min(width_, x + w); ++i)
            pixel(i, j, ink);
}

void Canvas::frame(int x, int y, int w, int h, int t, Ink ink) {
    fill(x, y, w, t, ink);
    fill(x, y + h - t, w, t, ink);
    fill(x, y, t, h, ink);
    fill(x + w - t, y, t, h, ink);
}

void Canvas::dotted(int x, int y, int w, int t, Ink ink) {
    for (int i = x; i + t <= x + w; i += 2 * t)
        fill(i, y, t, t, ink);
}

int Canvas::text_width(const char *s, int scale) {
    const int n = glyph_count(s);
    return n ? (n * kCell - 1) * scale : 0;
}

int Canvas::text(int x, int top, const char *s, int scale, Ink ink) {
    int pen = x;
    for (auto *p = reinterpret_cast<const unsigned char *>(s); *p;) {
        const uint8_t *g = glyph_for(next_codepoint(p));
        if (!g)
            continue;
        for (int row = 0; row < kRows; ++row)
            for (int col = 0; col < 5; ++col)
                if (g[row] & (0x10 >> col))
                    fill(pen + col * scale, top + row * scale, scale, scale, ink);
        pen += kCell * scale;
    }
    return pen - x;
}

int Canvas::text_right(int right, int top, const char *s, int scale, Ink ink) {
    return text(right - text_width(s, scale), top, s, scale, ink);
}

void Canvas::segment(int left, int top, int w, int h, int t, int index, Ink ink) {
    // Segments are hexagons with 45-degree tips and a 1 px gap to their neighbours. A segment
    // spans rows (or columns) c - half to c - half + t - 1 around its centre line c, so the
    // outer segments are placed to fill the cell exactly to its last row and column.
    const int half = t / 2;
    const int top_c = top + half, bottom_c = top + h - t + half, mid = (top_c + bottom_c) / 2;
    const int left_c = left + half, right_c = left + w - t + half;
    const bool horizontal = index == 0 || index == 3 || index == 6;
    if (horizontal) {
        const int cy = index == 0 ? top_c : index == 6 ? mid : bottom_c;
        const int x0 = left_c + 1, x1 = right_c - 1;
        for (int dy = -half; dy < t - half; ++dy) {
            const int inset = std::abs(dy);
            fill(x0 + inset, cy + dy, x1 - x0 + 1 - 2 * inset, 1, ink);
        }
        return;
    }
    const bool right = index == 1 || index == 2, upper = index == 1 || index == 5;
    const int cx = right ? right_c : left_c;
    const int y0 = upper ? top_c + 1 : mid + 1, y1 = upper ? mid - 1 : bottom_c - 1;
    for (int dx = -half; dx < t - half; ++dx) {
        const int inset = std::abs(dx);
        fill(cx + dx, y0 + inset, 1, y1 - y0 + 1 - 2 * inset, ink);
    }
}

int Canvas::segments_width(const char *s, int w, int t) {
    int width = 0;
    for (const char *p = s; *p; ++p)
        width += char_width(*p, w, t) + (p[1] ? digit_gap(t) : 0);
    return width;
}

int Canvas::segments(int x, int top, const char *s, int w, int h, int t, Ink ink) {
    int pen = x;
    for (const char *p = s; *p; ++p) {
        const char c = *p;
        if (c == ':') {
            fill(pen, top + h / 3 - t / 2, t, t, ink);
            fill(pen, top + h * 2 / 3 - t / 2, t, t, ink);
        } else if (c == '.') {
            fill(pen, top + h - t, t, t, ink);
        } else if (c == 'H') {
            // The right-hand segment pair of a digit box shifted so that the pair starts at pen.
            segment(pen - w + t, top, w, h, t, 1, ink);
            segment(pen - w + t, top, w, h, t, 2, ink);
        } else if (c != 'h') {
            const uint8_t lit = c >= '0' && c <= '9' ? kDigitSegments[c - '0']
                                : c == '-'             ? 0x40
                                                       : 0;
            for (int k = 0; k < 7; ++k)
                if (lit & (1 << k))
                    segment(pen, top, w, h, t, k, ink);
        }
        pen += char_width(c, w, t) + (p[1] ? digit_gap(t) : 0);
    }
    return pen - x;
}
} // namespace usage_panel::rlcd
