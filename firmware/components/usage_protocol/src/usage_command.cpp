#include "usage_protocol/usage_command.hpp"

#include <cstring>

namespace usage_panel {

FieldResult get_decimal_u32(const esp_desktop_buddy_command_view_t* request,
                            const char* key,
                            uint32_t& value)
{
    const char* text = nullptr;
    if (esp_desktop_buddy_command_view_get_string(request, key, &text) != ESP_OK) {
        return FieldResult::Missing;
    }
    if (!text || text[0] == '\0' || (text[0] == '0' && text[1] != '\0')) {
        return FieldResult::Invalid;
    }
    uint64_t parsed = 0;
    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
        if (*cursor < '0' || *cursor > '9') return FieldResult::Invalid;
        parsed = parsed * 10u + static_cast<unsigned>(*cursor - '0');
        if (parsed > UINT32_MAX) return FieldResult::Invalid;
    }
    value = static_cast<uint32_t>(parsed);
    return FieldResult::Valid;
}

namespace {

FieldResult get_optional_percent(
    const esp_desktop_buddy_command_view_t* request,
    const char* key,
    UsageWindow& window)
{
    uint32_t value = 0;
    const auto field = get_decimal_u32(request, key, value);
    if (field != FieldResult::Valid) return field;
    if (value > 100) return FieldResult::Invalid;
    window.present = true;
    window.used_percent = static_cast<uint8_t>(value);
    return FieldResult::Valid;
}

}  // namespace

UsageCommandParseResult parse_usage_command(
    const esp_desktop_buddy_command_view_t* request)
{
    UsageCommandParseResult result{};
    uint32_t version = 0;
    const char* provider_text = nullptr;
    const char* state_text = nullptr;
    if (get_decimal_u32(request, "seq", result.sequence) != FieldResult::Valid ||
        get_decimal_u32(request, "v", version) != FieldResult::Valid ||
        get_decimal_u32(request, "sampled_at", result.update.sampled_at) != FieldResult::Valid ||
        get_decimal_u32(request, "sent_at", result.update.sent_at) != FieldResult::Valid ||
        esp_desktop_buddy_command_view_get_string(request, "provider", &provider_text) != ESP_OK ||
        esp_desktop_buddy_command_view_get_string(request, "state", &state_text) != ESP_OK ||
        version != 1 ||
        uint64_t(result.update.sent_at) + kMaxInverseClockSkewSeconds <
            result.update.sampled_at) {
        return result;
    }

    if (strcmp(provider_text, "codex") == 0) {
        result.update.provider = Provider::Codex;
    } else if (strcmp(provider_text, "claude") == 0) {
        result.update.provider = Provider::Claude;
    } else {
        return result;
    }

    const auto short_result =
        get_optional_percent(request, "short_used_pct", result.update.short_window);
    const auto week_result =
        get_optional_percent(request, "week_used_pct", result.update.week_window);
    uint32_t short_reset = 0;
    uint32_t week_reset = 0;
    const auto short_reset_result =
        get_decimal_u32(request, "short_reset_at", short_reset);
    const auto week_reset_result =
        get_decimal_u32(request, "week_reset_at", week_reset);
    if (short_result == FieldResult::Invalid ||
        week_result == FieldResult::Invalid ||
        short_reset_result == FieldResult::Invalid ||
        week_reset_result == FieldResult::Invalid ||
        (short_reset_result == FieldResult::Valid && short_result != FieldResult::Valid) ||
        (week_reset_result == FieldResult::Valid && week_result != FieldResult::Valid)) {
        return result;
    }
    if (short_reset_result == FieldResult::Valid) {
        result.update.short_window.has_reset = true;
        result.update.short_window.reset_at = short_reset;
    }
    if (week_reset_result == FieldResult::Valid) {
        result.update.week_window.has_reset = true;
        result.update.week_window.reset_at = week_reset;
    }

    if (strcmp(state_text, "ok") == 0) {
        result.update.state = SourceState::Ok;
    } else if (strcmp(state_text, "partial") == 0) {
        result.update.state = SourceState::Partial;
    } else if (strcmp(state_text, "unavailable") == 0) {
        result.update.state = SourceState::Unavailable;
    } else {
        return result;
    }

    const unsigned present =
        unsigned(short_result == FieldResult::Valid) +
        unsigned(week_result == FieldResult::Valid);
    result.valid =
        (result.update.state == SourceState::Ok && present == 2) ||
        (result.update.state == SourceState::Partial && present == 1) ||
        (result.update.state == SourceState::Unavailable && present == 0);
    return result;
}

}  // namespace usage_panel
