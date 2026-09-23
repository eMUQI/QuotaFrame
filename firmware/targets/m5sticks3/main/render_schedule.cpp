#include "render_schedule.hpp"

#include <cstring>

namespace usage_panel {

bool TimedRenderKey::operator==(const TimedRenderKey& other) const
{
    return page == other.page &&
           first_state == other.first_state &&
           second_state == other.second_state &&
           ota_confirm_seconds == other.ota_confirm_seconds &&
           std::strcmp(short_countdown, other.short_countdown) == 0 &&
           std::strcmp(week_countdown, other.week_countdown) == 0 &&
           battery == other.battery;
}

TimedRenderKey make_timed_render_key(
    const UsageModel& model, Page page, bool connected, uint64_t now_ms,
    const usage_panel::m5::BatteryView& battery)
{
    TimedRenderKey key{};
    key.page = page;
    key.battery = battery;

    if (page == Page::Overview) {
        key.first_state = model.display_state(Provider::Codex, connected);
        key.second_state = model.display_state(Provider::Claude, connected);
        return key;
    }

    const Provider provider =
        page == Page::Codex ? Provider::Codex : Provider::Claude;
    key.first_state = model.display_state(provider, connected);

    const auto& snapshot = model.snapshot(provider);
    if (!snapshot.has_valid_data) return key;

    const uint32_t epoch = model.estimated_epoch(provider, now_ms);
    format_countdown(
        snapshot.short_window.present && snapshot.short_window.has_reset,
        snapshot.short_window.reset_at, epoch,
        key.short_countdown, sizeof(key.short_countdown));
    format_countdown(
        snapshot.week_window.present && snapshot.week_window.has_reset,
        snapshot.week_window.reset_at, epoch,
        key.week_countdown, sizeof(key.week_countdown));
    return key;
}

}  // namespace usage_panel
