// Host preview of the RLCD 4.2 screens. Writes one PBM per state into the output directory.
// Build from the repository root:
//   g++ -std=c++17 -O2 -Ifirmware/components/usage_core/include
//       -Ifirmware/targets/waveshare_rlcd_42/main tools/rlcd42/render_preview.cpp
//       firmware/targets/waveshare_rlcd_42/main/{canvas,panel_logic,screens}.cpp
//       firmware/components/usage_core/src/usage_state.cpp -o rlcd42_preview
#include "canvas.hpp"
#include "panel_logic.hpp"
#include "screens.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
using namespace usage_panel;
using namespace usage_panel::rlcd;
namespace {
constexpr uint32_t kNow = 1790253127; // Fixed epoch for reproducible preview data.

void apply(View &v, Provider p, uint8_t short_used, uint32_t short_reset, uint8_t week_used,
           uint32_t week_reset, uint32_t age) {
    UsageUpdate u;
    u.provider = p;
    u.state = SourceState::Ok;
    u.sampled_at = kNow - age;
    u.sent_at = kNow;
    u.short_window = {true, short_used, true, kNow + short_reset};
    u.week_window = {true, week_used, true, kNow + week_reset};
    v.model.apply(u, 0);
}

View design_view() {
    View v;
    v.link = {true, true, false, 0};
    v.clock = {true, 2026, 9, 24, 4, 14, 32, 7};
    v.environment_valid = true;
    v.temperature = 24.6f;
    v.humidity = 48;
    v.battery = 75;
    v.sd = true;
    snprintf(v.device_name, sizeof(v.device_name), "QF-WS-S3-R42-7A3F");
    apply(v, Provider::Codex, 42, 8045, 18, 281520, 0);
    apply(v, Provider::Claude, 84, 5045, 61, 439320, 120);
    v.warning = {0, 1};
    return v;
}

void write(const Canvas &c, const std::string &dir, const char *name) {
    const std::string path = dir + "/" + name + ".pbm";
    FILE *f = fopen(path.c_str(), "wb");
    if (!f)
        return;
    // The buffer is always native landscape; portrait frames are rotated back for viewing.
    const bool portrait = c.width() == Canvas::kNativeHeight;
    fprintf(f, "P1\n%d %d\n", c.width(), c.height());
    for (int y = 0; y < c.height(); ++y) {
        for (int x = 0; x < c.width(); ++x) {
            const int nx = portrait ? Canvas::kNativeWidth - 1 - y : x, ny = portrait ? x : y;
            const int n = ny * Canvas::kNativeWidth + nx;
            fputc((c.data()[n >> 3] & (0x80 >> (n & 7))) ? '1' : '0', f);
        }
        fputc('\n', f);
    }
    fclose(f);
}
} // namespace

int main(int argc, char **argv) {
    const std::string dir = argc > 1 ? argv[1] : ".";
    static Canvas canvas;
    Screens screens(canvas);
    auto render = [&](const View &v, const char *name) {
        screens.render(v);
        write(canvas, dir, name);
    };
    View v = design_view();
    render(v, "01_home");
    v.page = Page::Claude;
    render(v, "02_focus_claude");
    v.page = Page::Codex;
    render(v, "02_focus_codex");

    View a = design_view();
    apply(a, Provider::Claude, 97, 2892, 74, 400000, 0);
    apply(a, Provider::Codex, 57, 6000, 22, 280000, 0);
    a.warning = {0, 2};
    Navigator nav;
    nav.update_alert(a);
    render(a, "03_alert");

    View t = design_view();
    t.page = Page::Trend;
    t.trend_count = kTrendPoints;
    for (int i = 0; i < kTrendPoints; ++i) {
        auto wave = [&](float seed, int peak) {
            const float s = 0.5f + 0.5f * std::sin(i / 7.f + seed);
            const int value = static_cast<int>(peak * s * ((i % 10) / 20.f + 0.55f) + 0.5f);
            return static_cast<uint8_t>(value < 3 ? 3 : value > 100 ? 100 : value);
        };
        t.trend[i] = {kNow - (kTrendPoints - 1 - i) * kTrendIntervalSeconds, wave(0.4f, 78),
                      wave(2.1f, 100), {}};
    }
    t.trend[20].codex = 255;
    render(t, "04_trend");

    v = design_view();
    v.page = Page::Clock;
    render(v, "05_clock");
    v.page = Page::Settings;
    v.focus = 1;
    v.settings.cycle = 1;
    render(v, "06_settings");

    View off = design_view();
    off.link = {};
    UsageModel empty;
    off.model = empty;
    apply(off, Provider::Codex, 42, 8045, 18, 281520, 18 * 60);
    render(off, "07_offline");

    View pair = design_view();
    pair.link = {true, false, true, 482913};
    render(pair, "08_pairing");

    View ota = design_view();
    ota.ota.stage = OtaStage::Receiving;
    snprintf(ota.ota.version, sizeof(ota.ota.version), "0.9.2");
    ota.ota.size = 1900000;
    ota.ota.offset = 1240000;
    render(ota, "09_ota");
    snprintf(ota.ota.version, sizeof(ota.ota.version), "1.2.3-abcdefghijklmnopq");
    render(ota, "09_ota_long_version");

    View waiting = t;
    waiting.model = UsageModel{};
    waiting.link = {};
    waiting.now_ms = 5000;
    render(waiting, "04_trend_waiting_for_time");

    View p = design_view();
    p.settings.rotation = static_cast<uint8_t>(Rotation::Portrait);
    render(p, "10_portrait_home");
    p.page = Page::Clock;
    render(p, "11_portrait_clock");
    p.page = Page::Settings;
    render(p, "12_portrait_settings");
    a.settings.rotation = static_cast<uint8_t>(Rotation::Portrait);
    render(a, "13_portrait_alert");
    View full = design_view();
    apply(full, Provider::Codex, 100, 8045, 100, 281520, 0);
    render(full, "14_full_home");
    full.page = Page::Codex;
    render(full, "15_full_focus");
    full.alert = 0;
    render(full, "16_full_alert");
    full.settings.rotation = static_cast<uint8_t>(Rotation::Portrait);
    render(full, "17_full_portrait_alert");
    full.alert = -1;
    full.page = Page::Home;
    render(full, "18_full_portrait_home");

    View due = design_view();
    apply(due, Provider::Codex, 42, 0, 18, 0, 0);
    render(due, "19_reset_wait");
    due.settings.rotation = static_cast<uint8_t>(Rotation::Portrait);
    render(due, "20_portrait_reset_wait");

    View partial = design_view();
    UsageUpdate missing;
    missing.provider = Provider::Codex;
    missing.state = SourceState::Partial;
    missing.sampled_at = missing.sent_at = kNow + 1;
    missing.week_window = {true, 18, false, 0};
    partial.model.apply(missing, 0);
    render(partial, "21_partial_home");
    partial.page = Page::Codex;
    render(partial, "22_partial_focus");
    partial.settings.rotation = static_cast<uint8_t>(Rotation::Portrait);
    partial.page = Page::Home;
    render(partial, "23_partial_portrait_home");
    return 0;
}
