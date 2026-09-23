#include "usage_panel_state/render_schedule.hpp"

#include <cstring>

namespace usage_panel {

bool TimedRenderKey::operator==(const TimedRenderKey& other) const
{
    return page == other.page &&
           first_state == other.first_state &&
           second_state == other.second_state &&
           std::strcmp(overview_countdowns[0], other.overview_countdowns[0]) == 0 &&
           std::strcmp(overview_countdowns[1], other.overview_countdowns[1]) == 0 &&
           std::strcmp(short_countdown, other.short_countdown) == 0 &&
           std::strcmp(week_countdown, other.week_countdown) == 0 &&
           presentation == other.presentation;
}

TimedRenderKey make_timed_render_key(
    const UsageModel& model, Page page, bool connected, uint64_t now_ms,
    const PanelPresentation& presentation)
{
    TimedRenderKey key{};
    key.page = page;
    key.presentation = presentation;

    // Both providers remain visible in the overview and screensaver, even
    // when the screensaver was entered from a single-provider detail page.
    if (page == Page::Overview || presentation.screensaver.active) {
        key.first_state = model.display_state(Provider::Codex, connected);
        key.second_state = model.display_state(Provider::Claude, connected);
        if (!presentation.screensaver.active) {
            const Provider providers[] = {Provider::Codex, Provider::Claude};
            const DisplayState states[] = {key.first_state, key.second_state};
            for (size_t i = 0; i < 2; ++i) {
                // Non-online cards display status text instead of a countdown.
                if (states[i] != DisplayState::Online) continue;
                const auto& window = model.snapshot(providers[i]).short_window;
                format_countdown(window.has_reset, window.reset_at,
                    model.estimated_epoch(providers[i], now_ms),
                    key.overview_countdowns[i], sizeof(key.overview_countdowns[i]));
            }
        }
        return key;
    }

    const Provider provider =
        page == Page::Codex ? Provider::Codex : Provider::Claude;
    key.first_state = model.display_state(provider, connected);

    const auto& snapshot = model.snapshot(provider);
    if (!snapshot.has_valid_data) {
        return key;
    }

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
