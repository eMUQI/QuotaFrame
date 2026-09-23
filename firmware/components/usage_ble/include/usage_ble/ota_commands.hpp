#pragma once

#include "esp_desktop_buddy/esp_desktop_buddy.h"
#include "usage_ota/session.hpp"

namespace usage_panel {

/** Routes OTA control commands to one bound OtaSession. */
class OtaCommandRouter {
public:
    explicit OtaCommandRouter(OtaSession& session) : session_(&session) {}
    OtaCommandRouter() = default;

    /** Binds the session that must outlive this router. */
    void bind(OtaSession& session) { session_ = &session; }

    /** Returns whether the bound session is outside the Idle phase. */
    bool busy() const;

    /**
     * Handles ota_abort version 1 using canonical decimal version/sequence fields.
     * A valid abort is idempotent: it resets any active OTA state and ACKs success.
     */
    esp_desktop_buddy_command_extension_result_t handle_abort(
        const esp_desktop_buddy_command_view_t* request);

private:
    OtaSession* session_ = nullptr;
};

}  // namespace usage_panel
