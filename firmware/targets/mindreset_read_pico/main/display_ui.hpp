#pragma once
#include "epd_highlevel.h"
#include "usage_ble/app_events.hpp"
#include "usage_core/usage_state.hpp"
#include "usage_ota/session.hpp"
#include "usage_protocol/time_sync.hpp"
#include <array>
#include <cstdint>
namespace usage_panel::read_pico {
enum class Page { Home, Clock, Settings };
struct Settings {
    uint8_t refresh = 1, idle = 2, rotation = 0;
};
struct TrendPoint {
    uint32_t epoch;
    uint8_t codex, claude;
    uint8_t reserved[2]{};
};
struct View {
    UsageModel model;
    LinkUpdate link{};
    LocalCalendarTime calendar{};
    bool clock_valid = false, power_valid = false, charging = false;
    int battery = -1;
    Page page = Page::Home;
    // Replaces the page with the gray-level test card; set only by diagnostic views.
    bool test_card = false;
    Settings settings{};
    // Quarter turns from the upright portrait view; odd values are landscape.
    uint8_t orientation = 0;
    bool sd = false, save_error = false;
    std::array<uint8_t, 2> warning{};
    std::array<TrendPoint, 48> trend{};
    int trend_count = 0;
    OtaSnapshot ota;
    uint64_t now_ms = 0;
    // Current UTC from the board clock; zero until a valid clock is available.
    uint32_t utc_epoch = 0;
    char device_name[24]{};
};
struct TapTarget {
    enum Kind { None, Option, Done, OtaConfirm, OtaDeny } kind = None;
    int row = 0, index = 0;
};
class DisplayUi {
  public:
    /** Binds the epdiy state owned by the board; the first render clears the panel. */
    void begin(EpdiyHighlevelState *state);
    /**
     * Draws the view and refreshes the panel when the frame changed. The call blocks for the
     * refresh. baseline requests a flashing GC16 refresh even when the scene is unchanged.
     */
    void render(const View &view, bool baseline = false);
    /** Maps a touch in the panel's upright portrait coordinates to a control of the view. */
    TapTarget hit(const View &view, int touch_x, int touch_y) const;
    /** Powers the high-voltage rails down once no refresh has run for the idle timeout. */
    void tick(uint64_t now_ms);
    /** True after a successful frame refresh, until a subsequent refresh fails. */
    bool ready() const { return rendered_scene_ >= 0; }

  private:
    EpdiyHighlevelState *state_ = nullptr;
    uint8_t *pixels_ = nullptr;
    int width_ = 684, height_ = 1216;
    int rendered_scene_ = -1, rendered_orientation_ = -1;
    unsigned soft_refreshes_ = 0;
    uint64_t rails_deadline_ms_ = 0;
    void pixel(int x, int y, uint8_t color);
    void rect(int x, int y, int w, int h, uint8_t color);
    void frame(int x, int y, int w, int h, uint8_t color);
    void line(int x, int y, int x2, int y2, uint8_t color, int thickness = 1, bool dashed = false);
    // family: 0 Archivo 700, 1 JetBrains Mono 500, 2 JetBrains Mono 700.
    int text(int x, int y, const char *value, int size, uint8_t color, int family = 1,
             int spacing = 1, bool uppercase = true);
    void text_right(int right, int y, const char *value, int size, uint8_t color, int family = 1,
                    int spacing = 1);
    void text_center(int x, int w, int y, const char *value, int size, uint8_t color,
                     int family = 1, int spacing = 1);
    struct WindowSummary;
    struct ServiceSummary;
    static ServiceSummary summarize(const View &v, int index);
    void bar(int x, int y, int w, int h, int percent, uint8_t fill, bool hatch, bool notches);
    /** Draws a filled or outlined label vertically centered on center_y; returns its width. */
    int badge(int x, int center_y, const char *label, int size, bool filled, bool align_right);
    int value(int x, int bottom, const char *digits, int size, int unit_size, uint8_t color,
              int spacing, const char *unit = "%");
    void battery(const View &v, int y, uint8_t ink);
    void status(const View &v, int y);
    void header(const View &v, const char *title = nullptr);
    void service_title(const ServiceSummary &s, int x, int w, int top, int size);
    void window_column(const ServiceSummary &s, bool primary, int x, int w, int bar_y);
    int service_portrait(const View &v, int index, int y, bool rule);
    void service_landscape(const View &v, int index, int x, int w, int top, int bottom);
    struct TrendValue {
        uint32_t epoch = 0;
        int percent = -1;
    };
    /** Latest stored short-window sample within the last 24 hours. */
    TrendValue trend_last(const View &v, int provider) const;
    void trend_swatch(int x, int y, int provider);
    void chart(const View &v, int x, int y, int w, int h);
    void home(const View &v);
    int clock_row(const View &v, int index, int x, int w, int y, bool rule);
    void clock(const View &v);
    void settings(const View &v);
    void pairing(const View &v);
    void ota(const View &v);
    void test_card();
};
} // namespace usage_panel::read_pico
