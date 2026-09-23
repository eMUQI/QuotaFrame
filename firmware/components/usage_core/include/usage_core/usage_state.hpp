#pragma once

#include <cstddef>
#include <cstdint>

namespace usage_panel {

/** Usage providers supported by protocol version 1. */
enum class Provider : uint8_t { Codex, Claude };

/** Whether a source sample contains both, one, or no usage windows. */
enum class SourceState : uint8_t { Ok, Partial, Unavailable };

/** UI state derived from the last valid sample and link state. */
enum class DisplayState : uint8_t { NoData, Offline, Partial, Online };

/**
 * Clock-skew tolerance for a publication, in seconds.
 *
 * `sent_at` may precede `sampled_at` by at most this much. The Bridge enforces
 * the same bound as `MAX_INVERSE_CLOCK_SKEW_SECONDS`; the two must stay equal.
 */
constexpr uint32_t kMaxInverseClockSkewSeconds = 300;

/** One normalized usage window received from the Bridge. */
struct UsageWindow {
    bool present = false;
    uint8_t used_percent = 0;
    bool has_reset = false;
    uint32_t reset_at = 0;  // UTC epoch seconds when has_reset is true.
};

/** One validated usage publication before it is merged into the cached model. */
struct UsageUpdate {
    Provider provider = Provider::Codex;
    SourceState state = SourceState::Unavailable;
    uint32_t sampled_at = 0;  // Source sample time, in UTC epoch seconds.
    uint32_t sent_at = 0;     // Bridge send time, in UTC epoch seconds.
    UsageWindow short_window;
    UsageWindow week_window;
};

/** Cached state for one provider, including the last valid values. */
struct ProviderSnapshot {
    bool has_valid_data = false;
    SourceState source_state = SourceState::Unavailable;
    uint32_t sampled_at = 0;
    uint32_t sent_at = 0;
    // Monotonic receive time in milliseconds. It anchors sent_at to the local
    // monotonic clock so countdowns do not depend on an RTC.
    uint64_t received_ms = 0;
    // The cached windows below intentionally retain their last valid values.
    // These flags record which of them were actually carried by the latest
    // valid publication. Consumers can exclude windows retained from an older
    // sample without changing the cached values.
    bool latest_short_present = false;
    bool latest_week_present = false;
    UsageWindow short_window;
    UsageWindow week_window;
};

/** Maintains last-known usage while tracking the current source/link state. */
class UsageModel {
public:
    /**
     * Validates and merges one update.
     *
     * Unavailable updates refresh only the clock anchor, preserving the last
     * valid sample's state, timestamp, windows, and window selection.
     * `monotonic_ms` uses the same millisecond clock as estimated_epoch().
     *
     * @return false if percentages, timestamps, or state/window cardinality
     *         violate the protocol contract; true otherwise.
     */
    bool apply(const UsageUpdate& update, uint64_t monotonic_ms);

    /** Returns the cached snapshot for a provider. */
    const ProviderSnapshot& snapshot(Provider provider) const;

    /**
     * Derives the user-visible state: no data, disconnected, partial, or online.
     * Sample age does not change presentation.
     */
    DisplayState display_state(Provider provider, bool connected) const;

    /** Estimates current UTC epoch seconds from sent_at plus monotonic elapsed time. */
    uint32_t estimated_epoch(Provider provider, uint64_t now_ms) const;

private:
    ProviderSnapshot snapshots_[2]{};
};

/** Returns the fixed display label for a DisplayState. */
const char* display_state_name(DisplayState state);

/** Formats a reset countdown; WAIT means the reported reset time has passed. */
void format_countdown(bool has_reset, uint32_t reset_at, uint32_t now_epoch,
                      char* output, size_t output_size);

}  // namespace usage_panel
