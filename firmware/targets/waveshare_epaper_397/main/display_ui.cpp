#include "display_ui.hpp"
#include "driver/usb_serial_jtag.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "fonts.hpp"
#include "freertos/semphr.h"
#include "usage_ota/presentation.hpp"
#include "vendor/epaper_port.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
namespace usage_panel::epaper {
namespace {
uint8_t *pending = nullptr;
uint8_t *previous = nullptr;
bool pending_mono = false;
bool pending_clock = false;
uint8_t pending_orientation = 0;
struct DemoBox {
    int x, y, w, h;
};
DemoBox demo_box(int index, int width, int height) {
    const DemoBox boxes[] = {{32, 112, 64, 32}, {width - 96, 144, 64, 32},
                             {(width / 2 - 32) / 8 * 8, height / 2 - 16, 64, 32},
                             {64, height - 88, 64, 32}, {width - 112, height - 64, 64, 32}};
    return boxes[index];
}
std::array<DemoBox, 5> clock_windows(uint8_t orientation) {
    const bool portrait = orientation & 1;
    const int width = portrait ? 480 : 800, height = portrait ? 800 : 480;
    const int x = portrait ? 24 : 566, y = portrait ? 370 : 108;
    std::array<DemoBox, 5> windows = {{{width - 96, 20, 80, 32},
        {16, 100, portrait ? 448 : 512, portrait ? 156 : 188},
        {x, y + 28, width - x - 16, 80},
        {x, y + 149, width - x - 16, 80}, {24, height - 72, width - 48, 16}}};
    for (auto &box : windows) {
        if (portrait)
            box = {800 - box.y - box.h, box.x, box.h, box.w};
        if (orientation >= 2)
            box = {800 - box.x - box.w, 480 - box.y - box.h, box.w, box.h};
        const int end = (box.x + box.w + 7) / 8 * 8;
        box.x = box.x / 8 * 8;
        box.w = end - box.x;
    }
    return windows;
}
const Font *font_for(int size, int family) {
    const Font *f = &fonts[0];
    int distance = 10000;
    for (int i = 0; i < font_count; ++i) {
        int d = abs(fonts[i].size - size) + (fonts[i].family != family ? 1000 : 0);
        if (d < distance) {
            distance = d;
            f = &fonts[i];
        }
    }
    return f;
}
// Text always renders in uppercase.
const Glyph *glyph_for(const Font *f, unsigned char c) {
    const unsigned char upper = c >= 'a' && c <= 'z' ? c - 32 : c;
    for (int i = 0; i < f->count; ++i)
        if (f->glyphs[i].character == upper)
            return &f->glyphs[i];
    return nullptr;
}
int text_width(const char *s, int size, int family = 1, int spacing = 1) {
    const Font *f = font_for(size, family);
    int width = 0;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(s); *p; ++p)
        if (const Glyph *g = glyph_for(f, *p))
            width += g->advance + spacing;
    return width;
}
// Partial updates accumulate ghosting; a full refresh renews the optical and RAM baseline.
// The interval of 20 exceeds Waveshare's recommendation of a full refresh after 5 partial updates.
constexpr unsigned kPartialsPerBaseline = 20;
int gray_pixel(const uint8_t *image, int x, int y) {
    const int pixel = y * 800 + x;
    return (image[pixel / 4] >> (6 - pixel % 4 * 2)) & 3;
}
// Gray levels 2 and 3 become white monochrome pixels.
void pack_mono(const uint8_t *image, uint8_t *mono) {
    for (int i = 0; i < 48000; ++i) {
        uint8_t value = 0;
        for (int bit = 0; bit < 8; ++bit)
            value = (value << 1) | (gray_pixel(image, (i * 8 + bit) % 800, i / 100) >= 2);
        mono[i] = value;
    }
}
bool pending_full = false;
int pending_fixed_box = -1;
char pending_demo = 0;
unsigned pending_step = 0;
std::atomic<unsigned> submitted{0}, completed{0};
SemaphoreHandle_t lock = nullptr;
TaskHandle_t worker = nullptr;
std::atomic<bool> first_frame{false};
void transfer(void *) {
    auto *image = static_cast<uint8_t *>(heap_caps_malloc(96000, MALLOC_CAP_SPIRAM));
    ESP_ERROR_CHECK(image ? ESP_OK : ESP_ERR_NO_MEM);
    auto *mono = static_cast<uint8_t *>(heap_caps_malloc(48000, MALLOC_CAP_SPIRAM));
    auto *last = static_cast<uint8_t *>(heap_caps_malloc(48000, MALLOC_CAP_SPIRAM));
    auto *region = static_cast<uint8_t *>(heap_caps_malloc(48000, MALLOC_CAP_SPIRAM));
    ESP_ERROR_CHECK(mono && last && region ? ESP_OK : ESP_ERR_NO_MEM);
    auto *last_gray = static_cast<uint8_t *>(heap_caps_malloc(96000, MALLOC_CAP_SPIRAM));
    ESP_ERROR_CHECK(last_gray ? ESP_OK : ESP_ERR_NO_MEM);
    bool clock_active = false;
    bool mono_active = false;
    uint8_t last_orientation = 0;
    unsigned partial_count = 0;
    char last_demo = 0;
    epaper_port_init();
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        xSemaphoreTake(lock, portMAX_DELAY);
        const unsigned sequence = submitted.load();
        // One notification can carry a frame published for a later one; skip the duplicate.
        if (sequence == completed.load()) {
            xSemaphoreGive(lock);
            continue;
        }
        memcpy(image, pending, 96000);
        const bool monochrome = pending_mono;
        const bool clock_page = pending_clock;
        const uint8_t orientation = pending_orientation;
        const bool full = pending_full;
        pending_full = false;
        const int fixed_box = pending_fixed_box;
        const char demo = pending_demo;
        const unsigned step = pending_step;
        xSemaphoreGive(lock);
        int64_t start = esp_timer_get_time();
        const char *mode = "gray full";
        if (clock_page) {
            const auto windows = clock_windows(orientation);
            std::array<bool, 5> changed{};
            bool baseline = !clock_active || full || orientation != last_orientation;
            if (!baseline) {
                for (int y = 0; y < 480; ++y)
                    for (int x = 0; x < 800; ++x) {
                        if (gray_pixel(image, x, y) == gray_pixel(last_gray, x, y))
                            continue;
                        bool covered = false;
                        for (size_t i = 0; i < windows.size(); ++i) {
                            const auto &b = windows[i];
                            if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) {
                                changed[i] = true;
                                covered = true;
                            }
                        }
                        if (!covered)
                            baseline = true;
                    }
                // Alignment margins must also be black-white in both displayed and new frames.
                for (size_t i = 0; i < windows.size(); ++i)
                    if (changed[i]) {
                        const auto &b = windows[i];
                        for (int y = b.y; y < b.y + b.h; ++y)
                            for (int x = b.x; x < b.x + b.w; ++x)
                                if ((gray_pixel(image, x, y) % 3) || (gray_pixel(last_gray, x, y) % 3))
                                    baseline = true;
                    }
                // The whole batch has to fit the budget; one window at a time would overshoot it.
                if (partial_count + static_cast<unsigned>(std::count(changed.begin(), changed.end(), true)) >
                    kPartialsPerBaseline)
                    baseline = true;
            }
            if (baseline) {
                EPD_Init_4GRAY();
                EPD_Display_4Gray(image);
                pack_mono(image, mono);
                EPD_PrepareMonoRam(mono);
                partial_count = 0;
                mode = "clock gray baseline";
            } else {
                bool refreshed = false;
                for (size_t i = 0; i < windows.size(); ++i)
                    if (changed[i]) {
                        const auto &b = windows[i];
                        for (int y = 0; y < b.h; ++y)
                            for (int x = 0; x < b.w; x += 8) {
                                uint8_t value = 0;
                                for (int bit = 0; bit < 8; ++bit)
                                    value = (value << 1) | (gray_pixel(image, b.x + x + bit, b.y + y) == 3);
                                region[y * (b.w / 8) + x / 8] = value;
                            }
                        EPD_Display_Partial(region, b.x, b.y, b.x + b.w, b.y + b.h);
                        ++partial_count;
                        refreshed = true;
                    }
                mode = refreshed ? "clock mono partial" : "clock unchanged";
            }
            memcpy(last_gray, image, 96000);
        } else if (demo == 'g') {
            if (full || last_demo != 'g') {
                EPD_Init_4GRAY();
                EPD_Display_4Gray(image);
                pack_mono(image, mono);
                EPD_PrepareMonoRam(mono);
                mode = "gray baseline / mono RAM";
            } else {
                // This fixed black-white window excludes every optical gray patch.
                for (int y = 160; y < 288; ++y)
                    for (int x = 448; x < 704; x += 8) {
                        uint8_t value = 0;
                        for (int bit = 0; bit < 8; ++bit) {
                            const int pixel = y * 800 + x + bit;
                            const int color = (image[pixel / 4] >> (6 - pixel % 4 * 2)) & 3;
                            assert(color == 0 || color == 3);
                            value = (value << 1) | (color == 3);
                        }
                        region[(y - 160) * 32 + (x - 448) / 8] = value;
                    }
                EPD_Display_Partial(region, 448, 160, 704, 288);
                mode = "gray background / mono partial";
            }
        } else if (monochrome) {
            pack_mono(image, mono);
            // Partial updates require a monochrome baseline in both controller RAM planes.
            if (!mono_active || full || demo != last_demo || orientation != last_orientation ||
                partial_count >= kPartialsPerBaseline) {
                EPD_Init();
                EPD_Display_Base(mono);
                partial_count = 0;
                mode = "mono full";
            } else {
                int left = 100, right = 0, top = 480, bottom = 0;
                for (int y = 0; y < 480; ++y)
                    for (int x = 0; x < 100; ++x)
                        if (mono[y * 100 + x] != last[y * 100 + x]) {
                            left = std::min(left, x);
                            right = std::max(right, x + 1);
                            top = std::min(top, y);
                            bottom = std::max(bottom, y + 1);
                        }
                if (fixed_box >= 0) {
                    const auto box = demo_box(fixed_box, 800, 480);
                    left = box.x / 8;
                    right = (box.x + box.w) / 8;
                    top = box.y;
                    bottom = box.y + box.h;
                }
                if (left == 100) {
                    if (demo)
                        ESP_LOGI("ws397", "DEMO unchanged sequence=%u", sequence);
                    completed = sequence;
                    continue;
                }
                if (demo)
                    ESP_LOGI("ws397", "DEMO window native x=%d y=%d w=%d h=%d bytes=%d",
                             left * 8, top, (right - left) * 8, bottom - top,
                             (right - left) * (bottom - top));
                const int stride = right - left;
                if (fixed_box >= 0) {
                    // A known 64x32 payload isolates driver addressing from framebuffer extraction.
                    memset(region, 0xff, 256);
                    if (step & 1) {
                        memset(region, 0, 64);
                        for (int y = 8; y < 32; ++y) region[y * 8] = 0;
                        for (int y = 28; y < 32; ++y) region[y * 8 + 7] = 0;
                    }
                } else {
                    for (int y = top; y < bottom; ++y)
                        memcpy(region + (y - top) * stride, mono + y * 100 + left, stride);
                }
                EPD_Display_Partial(region, left * 8, top, right * 8, bottom);
                ++partial_count;
                mode = "mono partial";
            }
            memcpy(last, mono, 48000);
        } else {
            // Grayscale content requires the grayscale waveform across the entire panel.
            EPD_Init_4GRAY();
            EPD_Display_4Gray(image);
        }
        // Deep sleep does not retain the RAM baseline required by partial updates.
        if (!monochrome && !clock_page && demo != 'g')
            EPD_Sleep();
        clock_active = clock_page;
        mono_active = monochrome;
        last_orientation = orientation;
        last_demo = demo;
        first_frame = true;
        ESP_LOGI("ws397", "REFRESH %s %lld ms", mode, (esp_timer_get_time() - start) / 1000);
        if (demo)
            ESP_LOGI("ws397", "DEMO done test=%c step=%u orientation=%u sequence=%u mode=%s",
                     demo, step, orientation, sequence, mode);
        completed = sequence;
    }
}
} // namespace
bool DisplayUi::begin() {
    pixels_ = static_cast<uint8_t *>(heap_caps_malloc(96000, MALLOC_CAP_SPIRAM));
    pending = static_cast<uint8_t *>(heap_caps_malloc(96000, MALLOC_CAP_SPIRAM));
    previous = static_cast<uint8_t *>(heap_caps_malloc(96000, MALLOC_CAP_SPIRAM));
    lock = xSemaphoreCreateMutex();
    if (!pixels_ || !pending || !previous || !lock)
        return false;
    memset(previous, 0, 96000);
    memset(pixels_, 0xff, 96000);
    return xTaskCreate(transfer, "epaper", 4096, nullptr, 3, &worker) == pdPASS;
}
bool DisplayUi::ready() const { return first_frame.load(); }
bool DisplayUi::idle() const { return submitted.load() == completed.load(); }
void DisplayUi::pixel(int x, int y, uint8_t c) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_)
        return;
    int px = width_ == 800 ? x : 799 - y, py = width_ == 800 ? y : x;
    if (orientation_ >= 2) {
        px = 799 - px;
        py = 479 - py;
    }
    int n = py * 800 + px, shift = 6 - (n % 4) * 2;
    pixels_[n / 4] = (pixels_[n / 4] & ~(3 << shift)) | ((c & 3) << shift);
}
void DisplayUi::rect(int x, int y, int w, int h, uint8_t c) {
    for (int j = std::max(0, y); j < std::min(height_, y + h); ++j)
        for (int i = std::max(0, x); i < std::min(width_, x + w); ++i)
            pixel(i, j, c);
}
void DisplayUi::frame(int x, int y, int w, int h, uint8_t c) {
    rect(x, y, w, 2, c);
    rect(x, y + h - 2, w, 2, c);
    rect(x, y, 2, h, c);
    rect(x + w - 2, y, 2, h, c);
}
void DisplayUi::line(int x, int y, int x2, int y2, uint8_t c, int thick, bool dash) {
    int dx = abs(x2 - x), sx = x < x2 ? 1 : -1, dy = -abs(y2 - y), sy = y < y2 ? 1 : -1,
        e = dx + dy, n = 0;
    for (;;) {
        if (!dash || n % 16 < 9)
            rect(x, y, thick, thick, c);
        if (x == x2 && y == y2)
            break;
        int e2 = 2 * e;
        if (e2 >= dy) {
            e += dy;
            x += sx;
        }
        if (e2 <= dx) {
            e += dx;
            y += sy;
        }
        ++n;
    }
}
int DisplayUi::text(int x, int y, const char *s, int size, uint8_t c, int family, int spacing) {
    const Font *f = font_for(size, family);
    int start = x;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(s); *p; ++p) {
        const Glyph *g = glyph_for(f, *p);
        if (!g)
            continue;
        for (int j = 0; j < g->height; ++j)
            for (int i = 0; i < g->width; ++i) {
                int n = j * g->width + i;
                if (f->bitmap[g->offset + n / 8] & (128 >> (n % 8)))
                    pixel(x + g->left + i, y + g->top + j, c);
            }
        x += g->advance + spacing;
    }
    return x - start;
}
void DisplayUi::battery(const View &v) {
    int x = width_ - 67;
    frame(x, 26, 40, 19);
    rect(x + 40, 31, 3, 9, 0);
    int segments = v.battery < 0 ? 0 : (v.battery + 24) / 25;
    for (int i = 0; i < 4; ++i)
        rect(x + 4 + i * 8, 30, 5, 11, i < segments ? 0 : 2);
    if (v.charging) {
        line(x - 15, 25, x - 21, 35, 0, 2);
        line(x - 21, 35, x - 12, 35, 0, 2);
        line(x - 12, 35, x - 18, 45, 0, 2);
    }
}
void DisplayUi::header(const View &v, const char *title) {
    char time[16];
    snprintf(time, sizeof(time), "%02u:%02u", v.calendar.hour, v.calendar.minute);
    text(24, 24, title ? title : v.clock_valid ? time : "--:--", 28, 0, 0, -1);
    const char *status = v.link.has_passkey              ? "PAIRING"
                         : v.ota.phase != OtaPhase::Idle ? "UPDATING"
                         : v.link.encrypted              ? "LINKED"
                                                         : "OFFLINE";
    // Status text ends 12 px before the charging slot, which stays reserved while not charging.
    const int x = width_ - 109 - text_width(status, 16);
    if (!v.link.encrypted && !v.link.has_passkey && v.ota.phase == OtaPhase::Idle) {
        rect(x - 9, 23, text_width(status, 16) + 18, 26, 0);
        text(x, 28, status, 16, 3);
    } else
        text(x, 28, status, 16, 0);
    battery(v);
    rect(24, 71, width_ - 48, 3, 0);
}
void DisplayUi::bar(int x, int y, int w, int h, int p, uint8_t ink, uint8_t track, int warn) {
    rect(x, y, w, h, track);
    if (p < 0)
        return;
    int fill = w * std::clamp(p, 0, 100) / 100;
    if (warn == 1) {
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < fill; ++i)
                pixel(x + i, y + j, (i + j) % 6 < 3 ? ink : 3);
    } else
        rect(x, y, fill, h, ink);
    rect(x + w * 80 / 100, y, 2, h, 1);
    rect(x + w * 95 / 100, y, 2, h, 1);
}
void DisplayUi::home(const View &v) {
    header(v);
    if (v.environment_valid) {
        char environment[24];
        snprintf(environment, sizeof(environment), "%.1fC  %.0f%%", v.temperature, v.humidity);
        // A fixed position keeps minute changes of the time from moving this text.
        text(130, 28, environment, 16, 1);
    }
    for (int i = 0; i < 2; ++i) {
        Provider provider = i ? Provider::Claude : Provider::Codex;
        const auto &s = v.model.snapshot(provider);
        int x = v.portrait ? 24
                : i        ? 427
                           : 24,
            w = v.portrait ? 432 : 349, base = v.portrait ? 92 + i * 316 : 89;
        // Offline values use the light-gray checker, which remains distinct in monochrome mode.
        uint8_t ink = v.link.encrypted ? 0 : 2;
        char b[80];
        text(x, base, i ? "CLAUDE" : "CODEX", v.portrait ? 34 : 30, ink, 0, -1);
        uint32_t epoch = v.model.estimated_epoch(provider, v.now_ms);
        uint32_t age = epoch > s.sampled_at ? epoch - s.sampled_at : 0;
        // Minute granularity keeps the label from forcing a partial refresh on every redraw.
        if (s.has_valid_data && age < 60)
            snprintf(b, sizeof(b), "NOW");
        else if (s.has_valid_data)
            snprintf(b, sizeof(b), "%luM AGO", static_cast<unsigned long>(age / 60));
        else
            snprintf(b, sizeof(b), "NO DATA");
        text(x + w - 105, base + 8, b, 15, 1, 1, 0);
        bool present = s.has_valid_data && s.short_window.present &&
                       (s.latest_short_present || !v.link.encrypted);
        bool critical = present && v.warning[i] == 2 && v.link.encrypted;
        if (critical)
            rect(x - 8, base + 43, w + 16, 172, 0);
        text(x + (critical ? 10 : 0), base + 46, "SHORT", 16, critical ? 3 : 1);
        if (critical) {
            text(x + w - 130, base + 46, "EXHAUSTED", 16, 3, 1, 0);
        } else {
            char reset[40];
            format_countdown(s.short_window.has_reset, s.short_window.reset_at, epoch, reset,
                             sizeof(reset));
            snprintf(b, sizeof(b), v.link.encrypted ? "RESETS %s" : "RESET WAIT", reset);
            text(x + w - 175, base + 46, b, 15, 1, 1, 0);
        }
        if (present)
            snprintf(b, sizeof(b), "%u", s.short_window.used_percent);
        else
            snprintf(b, sizeof(b), "--");
        int advance = text(x, base + 68, b, v.portrait ? 136 : 120,
                           present ? (critical ? 3 : ink) : 1, 0, -4);
        if (present)
            text(x + advance + 6, base + 119, "%", 50, critical ? 3 : ink, 0, -1);
        // Bars stay black: a checker fill would be indistinguishable from the checker track.
        bar(x, base + 182, w, 18, present ? s.short_window.used_percent : -1, critical ? 3 : 0,
            critical ? 1 : 2, v.link.encrypted ? v.warning[i] : 0);
        if (v.warning[i] == 1 && present && v.link.encrypted) {
            rect(x + w - 82, base + 145, 82, 30, 0);
            text(x + w - 70, base + 153, "WARN", 16, 3);
        }
        text(x, base + 241, "WEEK", 16, 1);
        bool week = s.has_valid_data && s.week_window.present &&
                    (s.latest_week_present || !v.link.encrypted);
        if (week)
            snprintf(b, sizeof(b), "%u%%", s.week_window.used_percent);
        else
            snprintf(b, sizeof(b), "--");
        if (v.portrait) {
            text(x + 94, base + 222, b, 42, week ? ink : 1, 0, -1);
            bar(x + 205, base + 235, 227, 10, week ? s.week_window.used_percent : -1, 0, 2);
        } else {
            text(x, base + 260, b, 42, week ? ink : 1, 0, -1);
            bar(x, 400, w, 10, week ? s.week_window.used_percent : -1, 0, 2);
        }
        if (!v.portrait) {
            char reset[40];
            format_countdown(s.week_window.has_reset, s.week_window.reset_at, epoch, reset,
                             sizeof(reset));
            snprintf(b, sizeof(b), v.link.encrypted ? "RESETS %s" : "RESET WAIT", reset);
            text(x + w - 175, base + 241, b, 15, 1, 1, 0);
        }
    }
    if (!v.portrait)
        line(400, 89, 400, 410, 2, 1, true);
    else
        line(24, 391, 456, 391, 2);
    int bottom = height_ - 54;
    line(24, bottom, width_ - 24, bottom, 2, 1, true);
    if (v.portrait) {
        text(24, bottom + 12, "PRESS REFRESH / HOLD DISPLAY", 15, 1, 1, 0);
    } else {
        text(24, bottom + 12, "UP TREND", 15, 1);
        text(209, bottom + 12, "DOWN IDLE", 15, 1);
        text(403, bottom + 12, "PRESS REFRESH", 15, 1);
        text(610, bottom + 12, "HOLD DISPLAY", 15, 1);
    }
}
void DisplayUi::clock(const View &v) {
    battery(v);
    char b[64];
    snprintf(b, sizeof(b), "%02u:%02u", v.calendar.hour, v.calendar.minute);
    text(24, v.portrait ? 120 : 118, v.clock_valid ? b : "--:--", v.portrait ? 136 : 184, 0, 0, -9);
    static const char *days[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    snprintf(b, sizeof(b), "%s / %02u / %02u", days[v.calendar.weekday % 7], v.calendar.month,
             v.calendar.day);
    text(30, v.portrait ? 260 : 293, v.clock_valid ? b : "DATE UNAVAILABLE", 20, 1);
    int x = v.portrait ? 24 : 566, y = v.portrait ? 370 : 108;
    if (!v.portrait)
        line(536, 97, 536, 352, 2);
    text(x, y, "ROOM", 16, 1);
    if (v.environment_valid)
        snprintf(b, sizeof(b), "%.1f C", v.temperature);
    else
        snprintf(b, sizeof(b), "-- C");
    text(x, y + 30, b, 56, 0, 0, -2);
    text(x, y + 121, "HUMIDITY", 16, 1);
    if (v.environment_valid)
        snprintf(b, sizeof(b), "%.0f%%", v.humidity);
    else
        snprintf(b, sizeof(b), "--%%");
    text(x, y + 151, b, 56, 0, 0, -2);
    line(24, height_ - 92, width_ - 24, height_ - 92, 2);
    int percentages[2] = {-1, -1};
    for (int i = 0; i < 2; ++i) {
        const auto provider = i ? Provider::Claude : Provider::Codex;
        const auto state = v.model.display_state(provider, v.link.connected);
        const auto &s = v.model.snapshot(provider);
        if (state == DisplayState::Online || state == DisplayState::Partial) {
            if (s.latest_short_present)
                percentages[i] = s.short_window.used_percent;
            else if (s.latest_week_present)
                percentages[i] = s.week_window.used_percent;
        }
    }
    for (int i = 0; i < 2; ++i) {
        const int w = (width_ - 72) / 2;
        bar(24 + i * (w + 24), height_ - 71, w, 8, percentages[i], 0, 2);
    }
    text(24, height_ - 36, "USAGE / PRESS DIAL TO WAKE", 15, 1, 1, 0);
}
void DisplayUi::trend(const View &v) {
    header(v, "LAST 24H");
    if (!v.sd || !v.trend_count) {
        text(170, 215, "NO TREND DATA", 30, 0, 0);
        text(24, height_ - 42, "PRESS TO RETURN", 16, 1);
        return;
    }
    text(24, 94, "SHORT WINDOW / %", 16, 1);
    text(456, 94, "CODEX --- CLAUDE - -", 15, 1, 1, 0);
    for (int p = 25; p <= 100; p += 25) {
        int y = 366 - static_cast<int>(2.33f * p);
        line(24, y, 776, y, 2);
        char b[8];
        snprintf(b, sizeof(b), "%d", p);
        text(30, y + 7, b, 15, 1);
    }
    line(24, 366, 776, 366, 0, 2);
    text(24, 382, "-24H", 16, 1);
    text(372, 382, "-12H", 16, 1);
    text(740, 382, "NOW", 16, 1);
    uint32_t latest = v.trend[v.trend_count - 1].epoch;
    for (int service = 0; service < 2; ++service)
        for (int i = 1; i < v.trend_count; ++i) {
            auto &a = v.trend[i - 1];
            auto &b = v.trend[i];
            int av = service ? a.claude : a.codex, bv = service ? b.claude : b.codex;
            if (av > 100 || bv > 100 || b.epoch - a.epoch > 3600 || latest - a.epoch > 86400)
                continue;
            int x1 = 776 - static_cast<int>((latest - a.epoch) * 752ULL / 86400),
                x2 = 776 - static_cast<int>((latest - b.epoch) * 752ULL / 86400);
            line(x1, 366 - static_cast<int>(2.33f * av), x2, 366 - static_cast<int>(2.33f * bv), 0,
                 3, service);
        }
    line(24, 426, 776, 426, 2);
    text(24, 440, "UP / DOWN HOME", 16, 1);
}
void DisplayUi::settings(const View &v) {
    header(v, "DISPLAY");
    const char *labels[] = {"AUTO UPDATE", "IDLE CLOCK", "ROTATION"};
    const char *subs[] = {"INTERVAL / SEC", "AFTER IDLE / MIN", "1.5S HOLD"};
    const char *values[][4] = {
        {"15", "30", "60", "120"}, {"OFF", "5", "15", "30"}, {"AUTO", "LAND", "PORT", nullptr}};
    int selected[] = {v.settings.refresh, v.settings.idle, v.settings.rotation};
    for (int row = 0; row < 3; ++row) {
        int y = 102 + row * (v.portrait ? 184 : 98);
        if (row == v.focus)
            rect(24, y, 8, 62, 0);
        text(48, y + 3, labels[row], 24, 0, 0, -1);
        text(48, y + 39, subs[row], 15, 1, 1, 0);
        int start = v.portrait ? 48 : 315, cy = v.portrait ? y + 77 : y, n = row == 2 ? 3 : 4,
            w = (width_ - 24 - start - (n - 1) * 8) / n;
        for (int i = 0; i < n; ++i) {
            int x = start + i * (w + 8);
            if (i == selected[row])
                rect(x, cy, w, 56, 0);
            else
                frame(x, cy, w, 56);
            text(x + 10, cy + 18, values[row][i], 20, i == selected[row] ? 3 : 0, 1, 0);
        }
    }
    line(24, height_ - 54, width_ - 24, height_ - 54, 2);
    text(24, height_ - 39,
         v.save_error ? "SAVE FAILED / PRESS TO RETRY"
                      : "UP/DOWN SELECT / PRESS CHANGE / HOLD EXIT",
         15, 1, 1, 0);
}
void DisplayUi::pairing(const View &v) {
    header(v);
    text(24, 113, "PAIRING CODE", 17, 1, 1, 2);
    char b[24];
    snprintf(b, sizeof(b), "%03lu %03lu", static_cast<unsigned long>(v.link.passkey / 1000),
             static_cast<unsigned long>(v.link.passkey % 1000));
    text(24, 169, b, v.portrait ? 56 : 104, 0, 0, -3);
    int x = v.portrait ? 24 : 480, y = v.portrait ? 340 : 147;
    text(x, y, "ENTER THIS CODE", 24, 0, 0);
    text(x, y + 38, "ON THE DESKTOP", 24, 0, 0);
    text(x, y + 76, "BRIDGE", 24, 0, 0);
    line(24, height_ - 70, width_ - 24, height_ - 70, 2);
    text(24, height_ - 47, v.device_name, 16, 1);
    text(v.portrait ? 24 : 520, v.portrait ? height_ - 24 : height_ - 47, "PRESS NEW CODE", 15, 1);
}
void DisplayUi::ota(const View &v) {
    header(v);
    text(24, 106, "FIRMWARE", 17, 1);
    text(24, 148, v.ota.version.c_str(), 38, 0, 0);
    char b[80];
    int percent = ota_progress_percent(v.ota.offset, v.ota.size) / 5 * 5;
    snprintf(b, sizeof(b), "%d%%", percent);
    text(v.portrait ? 24 : 500, v.portrait ? 240 : 119, b, 104, 0, 0, -3);
    int y = v.portrait ? 405 : 281, w = (width_ - 48 - 19 * 4) / 20;
    for (int i = 0; i < 20; ++i)
        rect(24 + i * (w + 4), y, w, 24, i < percent / 5 ? 0 : 2);
    const char *phase = v.ota.phase == OtaPhase::Confirming  ? "CONFIRM UPDATE"
                        : v.ota.phase == OtaPhase::Rebooting ? "RESTARTING"
                        : v.ota.phase == OtaPhase::Verifying ? "VERIFYING"
                                                             : "WRITING";
    snprintf(b, sizeof(b), "%s / %.1f MB / %.1f MB", phase, v.ota.offset / 1000000.,
             v.ota.size / 1000000.);
    text(24, y + 44, b, 16, 1, 1, 0);
    text(24, y + 94,
         v.ota.phase == OtaPhase::Confirming ? "PRESS CONFIRM / HOLD DENY"
                                             : "DO NOT UNPLUG OR POWER OFF",
         24, 0, 0, -1);
}
void DisplayUi::render(const View &v, bool force, bool baseline) {
    demo_ = 0;
    orientation_ = v.orientation;
    width_ = v.portrait ? 480 : 800;
    height_ = v.portrait ? 800 : 480;
    memset(pixels_, 0xff, 96000);
    if (v.ota.phase != OtaPhase::Idle)
        ota(v);
    else if (v.link.has_passkey)
        pairing(v);
    else
        switch (v.page) {
        case Page::Home:
            home(v);
            break;
        case Page::Clock:
            clock(v);
            break;
        case Page::Trend:
            trend(v);
            break;
        case Page::Settings:
            settings(v);
            break;
        }
    if (v.ota.error != OtaError::None && v.ota.phase == OtaPhase::Idle) {
        rect(24, height_ - 55, width_ - 48, 40, 3);
        text(24, height_ - 43, ota_error_presentation(v.ota.error).title, 17, 0);
    }
    const bool clock_visible =
        v.page == Page::Clock && v.ota.phase == OtaPhase::Idle && !v.link.has_passkey;
    if (clock_visible) {
        for (const auto &box : clock_windows(orientation_))
            for (int y = box.y; y < box.y + box.h; ++y)
                for (int x = box.x; x < box.x + box.w; ++x) {
                    const int color = gray_pixel(pixels_, x, y);
                    const int pixel = y * 800 + x, shift = 6 - pixel % 4 * 2;
                    const bool white = color == 3 || (color == 2 && (x % 2 != y % 2));
                    pixels_[pixel / 4] = (pixels_[pixel / 4] & ~(3 << shift)) | ((white ? 3 : 0) << shift);
                }
    }
    const bool monochrome =
        v.ota.phase != OtaPhase::Idle ||
        ((v.page == Page::Home || v.page == Page::Settings) && !v.link.has_passkey);
    if (monochrome) {
        // Fixed native-coordinate stippling preserves light-gray elements in monochrome updates.
        for (int i = 0; i < 96000; ++i)
            for (int shift = 6; shift >= 0; shift -= 2)
                if (((pixels_[i] >> shift) & 3) == 2) {
                    const int pixel = i * 4 + (6 - shift) / 2;
                    const bool white = (pixel % 800) % 2 != (pixel / 800) % 2;
                    pixels_[i] = (pixels_[i] & ~(3 << shift)) | ((white ? 3 : 0) << shift);
                }
    }
    const int scene = v.ota.phase != OtaPhase::Idle ? 4
                      : v.link.has_passkey ? 5 : static_cast<int>(v.page);
    const bool scene_changed = scene != rendered_scene_;
    submit(monochrome, force || scene_changed, baseline || scene_changed, -1, clock_visible);
    rendered_scene_ = scene;
}
void DisplayUi::submit(bool monochrome, bool force, bool full, int fixed_box, bool clock_page) {
    if (monochrome) {
        for (int i = 0; i < 96000; ++i) {
            uint8_t value = 0;
            for (int shift = 6; shift >= 0; shift -= 2)
                value |= (((pixels_[i] >> shift) & 3) >= 2 ? 3 : 0) << shift;
            pixels_[i] = value;
        }
    }
    if (!force && memcmp(pixels_, previous, 96000) == 0)
        return;
    memcpy(previous, pixels_, 96000);
    xSemaphoreTake(lock, portMAX_DELAY);
    pending_mono = monochrome;
    pending_clock = clock_page;
    pending_orientation = orientation_;
    pending_full = pending_full || full;
    pending_fixed_box = fixed_box;
    pending_demo = demo_;
    pending_step = demo_step_;
    memcpy(pending, pixels_, 96000);
    ++submitted;
    xSemaphoreGive(lock);
    xTaskNotifyGive(worker);
}
void DisplayUi::probe() {
    demo_ = 0;
    orientation_ = 0;
    width_ = 800;
    height_ = 480;
    memset(pixels_, 255, 96000);
    for (int i = 0; i < 4; ++i) {
        rect(24 + i * 188, 90, 164, 220, i);
        char b[12];
        snprintf(b, sizeof(b), "GRAY %d", i);
        text(24 + i * 188, 340, b, 24, 0, 0);
    }
    text(24, 24, "WS397 GRAYSCALE PROBE", 30, 0, 0);
    submit(false, true);
}
bool DisplayUi::partial_demo(char command) {
    if (command == 'j' && demo_)
        command = demo_ == 't' ? 'u' : demo_ == 'u' ? 'v' : demo_ == 'v' ? 'w' : 't';
    const bool select = command == 't' || command == 'u' || command == 'v' || command == 'w' || command == 'g';
    if (!select && !(demo_ && (command == 'n' || command == 'b' || command == 'f' || command == 'o')))
        return false;
    if (submitted.load() != completed.load()) {
        ESP_LOGW("ws397", "DEMO busy; wait for refresh completion, then repeat the command");
        return true;
    }
    bool full = select || command == 'b' || command == 'f' || command == 'o';
    if (select) {
        demo_ = command;
        demo_step_ = 0;
        demo_orientation_ = 0;
    } else if (command == 'b')
        demo_step_ = 0;
    else if (command == 'n')
        ++demo_step_;
    else if (command == 'o') {
        if (demo_ != 'v' && demo_ != 'w') {
            ESP_LOGI("ws397", "DEMO rotation is available in v and w only");
            return true;
        }
        demo_orientation_ = (demo_orientation_ + 1) % 4;
        demo_step_ = 0;
    }
    orientation_ = demo_orientation_;
    width_ = (orientation_ & 1) ? 480 : 800;
    height_ = (orientation_ & 1) ? 800 : 480;
    int fixed_box = -1;
    if (command != 'f') {
        memset(pixels_, 0xff, 96000);
        if (demo_ == 'g') {
            text(24, 24, "G / GRAY BACKGROUND + MONO WINDOW", 24, 0, 0);
            for (int i = 0; i < 4; ++i) {
                rect(32 + i * 96, 120, 80, 192, i);
                frame(32 + i * 96, 120, 80, 192, 0);
                char label[12];
                snprintf(label, sizeof(label), "GRAY %d", i);
                text(32 + i * 96, 330, label, 15, 0);
            }
            frame(440, 152, 272, 144, 0);
            text(440, 110, "BLACK / WHITE ONLY", 16, 0);
            if (demo_step_ & 1)
                text(464, 180, (demo_step_ % 4 == 1) ? "12" : "34", 104, 0, 0);
            text(24, 410, "DOWN: 12 / BLANK / 34 / BLANK", 20, 0);
            text(24, 445, "UP FULL / HOLD EXIT", 16, 0);
        } else if (demo_ == 'w') {
            View test{};
            test.page = Page::Settings;
            test.orientation = orientation_;
            test.portrait = (orientation_ & 1) != 0;
            test.battery = 75;
            test.link = {true, true, false, 0};
            test.focus = (demo_step_ / 4) % 3;
            const unsigned choice = demo_step_ % 4;
            if (test.focus == 0)
                test.settings.refresh = choice;
            if (test.focus == 1)
                test.settings.idle = choice;
            if (test.focus == 2)
                test.settings.rotation = choice % 3;
            settings(test);
            rect(0, height_ - 50, width_, 50, 3);
            text(24, height_ - 39, "DEMO / DOWN NEXT / UP FULL / HOLD EXIT", 15, 0, 1, 0);
            ESP_LOGI("ws397", "DEMO settings focus=%d values=%u,%u,%u", test.focus,
                     test.settings.refresh, test.settings.idle, test.settings.rotation);
        } else {
            const char *title = demo_ == 't' ? "T / FIXED NATIVE WINDOW" :
                                demo_ == 'u' ? "U / AUTO DIFF WINDOW" : "V / ROTATED AUTO DIFF";
            text(24, 18, title, 24, 0, 0);
            text(24, 53, "DOWN NEXT / UP FULL / HOLD EXIT", 15, 0, 1, 0);
            // The fixed-window test writes native packed pixels without the UI coordinate transform.
            const auto paint = [&](int x, int y, int w, int h, uint8_t color) {
                if (demo_ == 't') {
                    for (int row = y; row < y + h; ++row)
                        memset(pixels_ + row * 200 + x / 4, color == 0 ? 0 : 0xff, w / 4);
                } else
                    rect(x, y, w, h, color);
            };
            for (int i = 0; i < 5; ++i) {
                const auto box = demo_box(i, width_, height_);
                paint(box.x - 8, box.y - 4, box.w + 16, 2, 0);
                paint(box.x - 8, box.y + box.h + 2, box.w + 16, 2, 0);
                paint(box.x - 8, box.y - 4, 4, box.h + 8, 0);
                paint(box.x + box.w + 4, box.y - 4, 4, box.h + 8, 0);
                char label[40];
                snprintf(label, sizeof(label), "%d (%d,%d)", i + 1, box.x, box.y);
                text(box.x - 8, box.y - 24, label, 15, 0, 1, 0);
            }
            if (demo_step_) {
                const int index = ((demo_step_ - 1) / 2) % 5;
                const auto box = demo_box(index, width_, height_);
                if (demo_step_ & 1) {
                    paint(box.x, box.y, box.w, 8, 0);
                    paint(box.x, box.y + 8, 8, box.h - 8, 0);
                    paint(box.x + box.w - 8, box.y + box.h - 4, 8, 4, 0);
                }
                if (demo_ == 't')
                    fixed_box = index;
                ESP_LOGI("ws397", "DEMO expected box=%d logical x=%d y=%d w=%d h=%d ink=%s",
                         index + 1, box.x, box.y, box.w, box.h,
                         (demo_step_ & 1) ? "L+dot" : "white");
            }
        }
    }
    ESP_LOGI("ws397", "DEMO request test=%c step=%u orientation=%u full=%d", demo_,
             demo_step_, orientation_, full);
    submit(demo_ != 'g', true, full, fixed_box);
    return true;
}
void DisplayUi::dump() {
    const auto send = [](const char *data, size_t length) {
        return usb_serial_jtag_write_bytes(data, length, pdMS_TO_TICKS(5000)) ==
               static_cast<int>(length);
    };
    if (!send("FRAME_BEGIN\n", 12))
        return;
    const char hex[] = "0123456789abcdef";
    char row[129];
    row[128] = '\n';
    for (int i = 0; i < 96000; i += 64) {
        for (int j = 0; j < 64; ++j) {
            row[j * 2] = hex[pixels_[i + j] >> 4];
            row[j * 2 + 1] = hex[pixels_[i + j] & 15];
        }
        if (!send(row, sizeof(row)))
            return;
    }
    send("FRAME_END\n", 10);
}
} // namespace usage_panel::epaper
