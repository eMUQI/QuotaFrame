#include "usage_ble/ota_commands.hpp"

#include <cstdint>

#include "usage_protocol/usage_command.hpp"

namespace usage_panel {
namespace {

esp_desktop_buddy_command_extension_result_t ack(
    bool ok, uint32_t sequence, const char* error = nullptr)
{
    esp_desktop_buddy_command_extension_result_t result{};
    result.mode = ESP_DESKTOP_BUDDY_COMMAND_EXTENSION_ACK;
    result.reply.ok = ok;
    result.reply.n = sequence;
    result.reply.error = error;
    return result;
}

bool parse_common(
    const esp_desktop_buddy_command_view_t* request, uint32_t& sequence)
{
    uint32_t version = 0;
    return get_decimal_u32(request, "v", version) == FieldResult::Valid &&
        version == 1 &&
        get_decimal_u32(request, "seq", sequence) == FieldResult::Valid;
}

}  // namespace

bool OtaCommandRouter::busy() const
{
    return session_ && session_->busy();
}

esp_desktop_buddy_command_extension_result_t OtaCommandRouter::handle_abort(
    const esp_desktop_buddy_command_view_t* request)
{
    uint32_t sequence = 0;
    if (!parse_common(request, sequence) || !session_) {
        return ack(false, sequence, "invalid_request");
    }
    session_->abort();
    return ack(true, sequence);
}

}  // namespace usage_panel
