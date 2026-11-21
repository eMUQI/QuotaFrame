#pragma once

#include "usage_core/usage_state.hpp"

namespace usage_panel {

/** One 30-minute record in the displays' on-card history format. */
struct TrendPoint {
    uint32_t epoch;
    uint8_t codex, claude;
    // Legacy records have a zero version byte and contain no Gemini sample.
    uint8_t reserved[2]{};

    uint8_t value(Provider provider) const
    {
        if (provider == Provider::Codex) return codex;
        if (provider == Provider::Claude) return claude;
        return reserved[1] == 1 ? reserved[0] : 255;
    }

    bool valid_values() const
    {
        const auto valid = [](uint8_t percent) { return percent <= 100 || percent == 255; };
        return valid(codex) && valid(claude) && valid(value(Provider::Gemini));
    }
};

static_assert(sizeof(TrendPoint) == 8, "Trend records must remain compatible with existing history");

}  // namespace usage_panel
