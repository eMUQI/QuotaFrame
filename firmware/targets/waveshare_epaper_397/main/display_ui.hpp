#pragma once
#include "usage_ble/app_events.hpp"
#include "usage_core/usage_state.hpp"
#include "usage_ota/session.hpp"
#include "usage_protocol/time_sync.hpp"
#include <array>
#include <cstdint>
namespace usage_panel::epaper {
enum class Page { Home, Trend, Clock, Settings };
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
    bool clock_valid = false, environment_valid = false, power_valid = false, charging = false;
    float temperature = 0, humidity = 0;
    int battery = -1;
    Page page = Page::Home;
    Settings settings{};
    int focus = 0;
    uint8_t orientation = 0;
    bool portrait = false, sd = false, save_error = false;
    std::array<uint8_t, 2> warning{};
    std::array<TrendPoint, 48> trend{};
    int trend_count = 0;
    OtaSnapshot ota;
    uint64_t now_ms = 0;
    char device_name[24]{};
};
class DisplayUi {
  public:
    bool begin();
    // baseline requests a full refresh even when the scene is unchanged.
    void render(const View &view, bool force = false, bool baseline = false);
    void probe();
    // Diagnostic commands run on the application task; display transfers remain serialized.
    bool partial_demo(char command);
    bool partial_demo_active() const { return demo_ != 0; }
    void end_partial_demo() { demo_ = 0; }
    void dump();
    bool ready() const;
    /** True when every submitted frame has finished refreshing. */
    bool idle() const;

  private:
    uint8_t *pixels_ = nullptr;
    char demo_ = 0;
    unsigned demo_step_ = 0;
    uint8_t demo_orientation_ = 0;
    void submit(bool monochrome, bool force, bool full = false, int fixed_box = -1, bool clock_page = false);
    uint8_t orientation_ = 0;
    int rendered_scene_ = -1;
    int width_ = 800, height_ = 480;
    void pixel(int x, int y, uint8_t color);
    void rect(int x, int y, int w, int h, uint8_t color);
    void line(int x, int y, int x2, int y2, uint8_t color, int thickness = 1, bool dashed = false);
    int text(int x, int y, const char *value, int size = 16, uint8_t color = 1, int family = 1,
             int spacing = 1);
    void frame(int x, int y, int w, int h, uint8_t color = 0);
    void battery(const View &v);
    void header(const View &v, const char *title = nullptr);
    void home(const View &v);
    void clock(const View &v);
    void trend(const View &v);
    void settings(const View &v);
    void pairing(const View &v);
    void ota(const View &v);
    void bar(int x, int y, int w, int h, int percent, uint8_t ink, uint8_t track, int warning = 0);
};
} // namespace usage_panel::epaper
