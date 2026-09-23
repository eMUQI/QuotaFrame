#include "usage_core/usage_state.hpp"

#include <cstdio>

namespace usage_panel {
namespace {
size_t index_of(Provider provider) { return provider == Provider::Codex ? 0 : 1; }
}

bool UsageModel::apply(const UsageUpdate& update, uint64_t monotonic_ms)
{
    const unsigned present = unsigned(update.short_window.present) + unsigned(update.week_window.present);
    // Keep the model defensive even though the BLE parser validates the same
    // invariants. Tests and future producers can construct UsageUpdate directly.
    if ((update.short_window.present && update.short_window.used_percent > 100) ||
        (update.week_window.present && update.week_window.used_percent > 100) ||
        (uint64_t(update.sent_at) + kMaxInverseClockSkewSeconds <
             update.sampled_at) ||
        (update.state == SourceState::Ok && present != 2) ||
        (update.state == SourceState::Partial && present != 1) ||
        (update.state == SourceState::Unavailable && present != 0)) {
        return false;
    }
    auto& current = snapshots_[index_of(update.provider)];
    current.sent_at = update.sent_at;
    current.received_ms = monotonic_ms;
    // Unavailable publications update the clock anchor but preserve the last
    // valid sample, including its state and window selection.
    if (update.state == SourceState::Unavailable) return true;
    current.source_state = update.state;
    current.latest_short_present = update.short_window.present;
    current.latest_week_present = update.week_window.present;
    current.has_valid_data = true;
    current.sampled_at = update.sampled_at;
    if (update.short_window.present) current.short_window = update.short_window;
    if (update.week_window.present) current.week_window = update.week_window;
    return true;
}

const ProviderSnapshot& UsageModel::snapshot(Provider provider) const
{
    return snapshots_[index_of(provider)];
}

uint32_t UsageModel::estimated_epoch(Provider provider, uint64_t now_ms) const
{
    const auto& item = snapshot(provider);
    // sent_at supplies an epoch anchor while the monotonic clock supplies
    // elapsed time; this avoids depending on a device RTC for usage countdowns.
    const uint64_t elapsed = now_ms > item.received_ms ? (now_ms - item.received_ms) / 1000u : 0;
    const uint64_t epoch = uint64_t(item.sent_at) + elapsed;
    return epoch > UINT32_MAX ? UINT32_MAX : uint32_t(epoch);
}

DisplayState UsageModel::display_state(Provider provider, bool connected) const
{
    const auto& item = snapshot(provider);
    if (!item.has_valid_data) return DisplayState::NoData;
    if (!connected) return DisplayState::Offline;
    if (item.source_state == SourceState::Partial) return DisplayState::Partial;
    return DisplayState::Online;
}

const char* display_state_name(DisplayState state)
{
    switch (state) {
    case DisplayState::NoData: return "NO DATA";
    case DisplayState::Offline: return "OFFLINE";
    case DisplayState::Partial: return "PARTIAL";
    default: return "ONLINE";
    }
}

void format_countdown(bool has_reset, uint32_t reset_at, uint32_t now_epoch,
                      char* output, size_t output_size)
{
    if (!has_reset) { snprintf(output, output_size, "--"); return; }
    if (reset_at <= now_epoch) { snprintf(output, output_size, "WAIT"); return; }
    const uint32_t minutes = (reset_at - now_epoch + 59u) / 60u;
    if (minutes < 60) snprintf(output, output_size, "%lum", (unsigned long)minutes);
    else if (minutes < 1440) snprintf(output, output_size, "%luh %02lum",
                                     (unsigned long)(minutes / 60), (unsigned long)(minutes % 60));
    else snprintf(output, output_size, "%lud %02luh",
                  (unsigned long)(minutes / 1440), (unsigned long)((minutes / 60) % 24));
}
}  // namespace usage_panel
