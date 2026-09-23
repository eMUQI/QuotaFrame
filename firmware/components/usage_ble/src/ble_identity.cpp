#include "usage_ble/ble_identity.hpp"

#include <cstdio>
#include <cstring>
#include "usage_json.h"

namespace usage_panel {
namespace {

bool token_valid(const char* text, bool lowercase)
{
    if (!text || !*text || strlen(text) > 31) return false;
    for (const char* p = text; *p; ++p) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
            (!lowercase && *p >= 'A' && *p <= 'Z') || *p == '.' || *p == '_' || *p == '-') continue;
        return false;
    }
    return !lowercase || ((*text >= 'a' && *text <= 'z') || (*text >= '0' && *text <= '9'));
}

const char* phase_name(OtaPhase phase)
{
    switch (phase) {
        case OtaPhase::Idle: return "idle";
        case OtaPhase::Confirming: return "confirming";
        case OtaPhase::Receiving: return "receiving";
        case OtaPhase::Verifying: return "verifying";
        case OtaPhase::Rebooting: return "rebooting";
    }
    return nullptr;
}

const char* error_name(OtaError error)
{
    switch (error) {
        case OtaError::None: return "";
        case OtaError::Denied: return "denied";
        case OtaError::Timeout: return "timeout";
        case OtaError::TooLarge: return "too_large";
        case OtaError::BadImage: return "bad_image";
        case OtaError::LinkLost: return "link_lost";
        case OtaError::SeqGap: return "seq_gap";
        case OtaError::LowPower: return "low_power";
        case OtaError::ProjectMismatch: return "project_mismatch";
        case OtaError::TargetMismatch: return "target_mismatch";
    }
    return nullptr;
}

}  // namespace

bool make_advertising_name(
    const char* prefix, uint16_t mac_suffix,
    char* output, size_t output_size)
{
    if (!prefix || !output || output_size == 0 || strlen(prefix) > 15 ||
        strncmp(prefix, "QF-", 3) != 0) return false;
    for (const char* p = prefix; *p; ++p)
        if (static_cast<unsigned char>(*p) < 0x21 || static_cast<unsigned char>(*p) > 0x7e)
            return false;
    const int written = snprintf(
        output, output_size, "%s%04X", prefix, static_cast<unsigned>(mac_suffix));
    return written >= 0 && static_cast<size_t>(written) < output_size;
}

bool make_status_json(
    const char* status_name, bool encrypted, const char* ui_location,
    const char* firmware_version, const char* target,
    const OtaSnapshot& ota, bool enable_time_sync,
    char* output, size_t output_size, bool enable_screen_toggle, bool boot_valid, bool enable_screen_page)
{
    if (!usage_name_valid(status_name) || !firmware_version || strlen(firmware_version) > 31 ||
        !usage_version_valid(firmware_version[0] == 'v' ? firmware_version + 1 : firmware_version) ||
        !target || !output || !output_size) return false;
    if (!token_valid(target, true) || (ui_location && *ui_location && !token_valid(ui_location, false))) return false;
    const char* phase = phase_name(ota.phase);
    const char* error = error_name(ota.error);
    if (!phase || !error) return false;
    cJSON* root = cJSON_CreateObject();
    if (!root) return false;
    cJSON_AddStringToObject(root, "name", status_name);
    cJSON_AddBoolToObject(root, "sec", encrypted);
    cJSON_AddNumberToObject(root, "protocol", 1);
    if (ui_location && *ui_location) cJSON_AddStringToObject(root, "page", ui_location);
    cJSON* caps = cJSON_AddArrayToObject(root, "caps");
    if (caps) {
        cJSON_AddItemToArray(caps, cJSON_CreateString("usage.v1"));
        cJSON_AddItemToArray(caps, cJSON_CreateString("ota.folder.v1"));
        if (enable_time_sync) cJSON_AddItemToArray(caps, cJSON_CreateString("time.sync.v1"));
        if (enable_screen_toggle) cJSON_AddItemToArray(caps, cJSON_CreateString("screen.toggle.v1"));
        if (enable_screen_page) cJSON_AddItemToArray(caps, cJSON_CreateString("screen.page.v1"));
    }
    cJSON_AddStringToObject(root, "firmware_project", "quotaframe");
    cJSON_AddStringToObject(root, "fw", firmware_version);
    cJSON_AddStringToObject(root, "target", target);
    cJSON_AddBoolToObject(root, "boot_valid", boot_valid);
    cJSON* state = cJSON_AddObjectToObject(root, "ota");
    char offset[11], size[11];
    snprintf(offset, sizeof(offset), "%u", static_cast<unsigned>(ota.offset));
    snprintf(size, sizeof(size), "%u", static_cast<unsigned>(ota.size));
    cJSON_AddStringToObject(state, "phase", phase);
    cJSON_AddStringToObject(state, "off", offset);
    cJSON_AddStringToObject(state, "size", size);
    cJSON_AddStringToObject(state, "err", error);
    const bool valid = caps && state && cJSON_PrintPreallocated(root, output, output_size, false);
    cJSON_Delete(root);
    return valid;
}

}  // namespace usage_panel
