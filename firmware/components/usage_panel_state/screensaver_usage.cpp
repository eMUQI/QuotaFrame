#include "usage_panel_state/screensaver_usage.hpp"

#include <initializer_list>

namespace usage_panel {
namespace {
constexpr uint8_t WARN_PERCENT = 80;
constexpr uint8_t BAD_PERCENT  = 95;

size_t row_index(Provider provider)
{
    return provider == Provider::Codex ? 0 : 1;
}
}  // namespace

bool ScreensaverUsageRow::operator==(const ScreensaverUsageRow& other) const
{
    return has_data == other.has_data && percent == other.percent &&
           show_percent == other.show_percent && level == other.level;
}

bool ScreensaverUsageView::operator==(const ScreensaverUsageView& other) const
{
    return rows[0] == other.rows[0] && rows[1] == other.rows[1] &&
           offline == other.offline;
}

ScreensaverUsageLevel screensaver_usage_level(uint8_t percent)
{
    if (percent >= BAD_PERCENT) return ScreensaverUsageLevel::Bad;
    if (percent >= WARN_PERCENT) return ScreensaverUsageLevel::Warn;
    return ScreensaverUsageLevel::Base;
}

ScreensaverUsageRow make_screensaver_usage_row(const UsageModel& model, Provider provider, bool connected)
{
    ScreensaverUsageRow row{};
    const DisplayState state =
        model.display_state(provider, connected);
    if (state != DisplayState::Online && state != DisplayState::Partial) {
        return row;
    }

    // Only windows from the last valid publication participate in selection.
    const ProviderSnapshot& snapshot = model.snapshot(provider);
    const UsageWindow* fill_window = nullptr;
    if (snapshot.latest_short_present) {
        fill_window = &snapshot.short_window;
    } else if (snapshot.latest_week_present) {
        fill_window = &snapshot.week_window;
    }
    if (fill_window == nullptr) return row;

    row.has_data = true;
    row.percent = fill_window->used_percent;
    row.level = screensaver_usage_level(row.percent);
    row.show_percent = row.percent >= WARN_PERCENT;
    return row;
}

ScreensaverUsageView make_screensaver_usage_view(const UsageModel& model, bool connected)
{
    ScreensaverUsageView view{};
    for (Provider provider : {Provider::Codex, Provider::Claude}) {
        view.rows[row_index(provider)] = make_screensaver_usage_row(model, provider, connected);
    }

    // Showing both labels keeps the shared columns aligned during an alert.
    if (view.rows[0].show_percent || view.rows[1].show_percent) {
        for (ScreensaverUsageRow& row : view.rows) {
            if (row.has_data) row.show_percent = true;
        }
    }

    view.offline = !view.rows[0].has_data && !view.rows[1].has_data;
    return view;
}

}  // namespace usage_panel
