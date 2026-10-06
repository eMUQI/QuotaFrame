#include "demo_view.hpp"
#include <cstdio>
namespace usage_panel::read_pico {
View demo_view(char command, const View &live) {
    View demo{};
    demo.settings = live.settings;
    demo.now_ms = live.now_ms;
    demo.battery = 75;
    demo.sd = true;
    snprintf(demo.device_name, sizeof(demo.device_name), "%s", live.device_name);
    demo.link = {true, true, false, 0};
    demo.clock_valid = true;
    demo.calendar.year = 2026;
    demo.calendar.month = 8;
    demo.calendar.day = 12;
    demo.calendar.weekday = 3;
    demo.calendar.hour = 14;
    demo.calendar.minute = 32;
    demo.calendar.utc_offset_minutes = 480;
    const bool alarm = command == '2';
    const uint8_t short_used[2] = {static_cast<uint8_t>(alarm ? 96 : 42),
                                 static_cast<uint8_t>(alarm ? 87 : 70)};
    for (int i = 0; i < 2; ++i) {
        if (command == '8' && i)
            break;
        UsageUpdate u;
        u.provider = i ? Provider::Claude : Provider::Codex;
        u.state = SourceState::Ok;
        u.sampled_at = 1786516320;
        u.sent_at = u.sampled_at;
        u.short_window = {true, short_used[i], true,
                          u.sent_at + (i ? 2820 : 8040)};
        u.week_window = {true, static_cast<uint8_t>(i ? 54 : 68), true, u.sent_at + 86400};
        demo.model.apply(u, demo.now_ms);
        demo.warning[i] = alarm ? (i ? 1 : 2) : 0;
    }
    demo.trend_count = 48;
    for (int i = 0; i < 48; ++i)
        demo.trend[i] = {static_cast<uint32_t>(1786516200 - (47 - i) * 1800),
                         static_cast<uint8_t>(8 + i * (short_used[0] - 8) / 47),
                         static_cast<uint8_t>(5 + i * (short_used[1] - 5) / 47), {}};
    if (command == '3')
        demo.page = Page::Clock;
    if (command == '4')
        demo.orientation = 1;
    if (command == '5')
        demo.link = {true, false, true, 418502};
    if (command == '6' || command == '9') {
        demo.ota.phase = command == '6' ? OtaPhase::Receiving : OtaPhase::Confirming;
        demo.ota.version = "v1.2.0";
        demo.ota.size = 6500000;
        demo.ota.offset = command == '6' ? 4225000 : 0;
    }
    if (command == '7')
        demo.page = Page::Settings;
    if (command == '8')
        demo.link = {};
    demo.test_card = command == 't';
    return demo;
}
} // namespace usage_panel::read_pico
