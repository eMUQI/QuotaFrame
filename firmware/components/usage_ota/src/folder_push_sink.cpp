#include "usage_ota/folder_push_sink.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "cJSON.h"
#include "usage_json.h"
#include "esp_timer.h"

namespace usage_panel {
namespace {

// Folder Push is deliberately a two-file protocol rather than a generic file
// receiver. Keeping these names and their order fixed prevents an authenticated
// BLE peer from turning OTA into arbitrary filesystem-style input.
constexpr char kTransferName[] = "firmware";
constexpr char kManifestPath[] = "manifest.json";
constexpr char kFirmwarePath[] = "firmware.bin";

esp_desktop_buddy_folder_push_sink_result_t ok()
{
    return esp_desktop_buddy_folder_push_result_ok();
}

esp_desktop_buddy_folder_push_sink_result_t rejected(const char* detail)
{
    return esp_desktop_buddy_folder_push_result_err(
        ESP_ERR_NOT_ALLOWED,
        ESP_DESKTOP_BUDDY_FOLDER_PUSH_SINK_REASON_REJECTED,
        detail);
}

esp_desktop_buddy_folder_push_sink_result_t invalid(const char* detail)
{
    return esp_desktop_buddy_folder_push_result_err(
        ESP_ERR_INVALID_ARG,
        ESP_DESKTOP_BUDDY_FOLDER_PUSH_SINK_REASON_INVALID_CONTENT,
        detail);
}

esp_desktop_buddy_folder_push_sink_result_t storage_failed(const char* detail)
{
    return esp_desktop_buddy_folder_push_result_err(
        ESP_FAIL,
        ESP_DESKTOP_BUDDY_FOLDER_PUSH_SINK_REASON_STORAGE_FAILED,
        detail);
}

bool json_u32(const cJSON* item, uint32_t& output)
{
    // cJSON stores numbers as double. Require an exactly representable positive
    // integer before narrowing so fractional/NaN/overflow values cannot change
    // the transfer size seen by the OTA session.
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) ||
        item->valuedouble < 1 || item->valuedouble > UINT32_MAX ||
        std::floor(item->valuedouble) != item->valuedouble) {
        return false;
    }
    output = static_cast<uint32_t>(item->valuedouble);
    return true;
}

const char* json_string(const cJSON* root, const char* key)
{
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : nullptr;
}

}  // namespace

bool OtaFolderPushSink::bind(
    OtaSession& session, const char* target, OtaFolderPushNowMs now_ms)
{
    if (!target || target[0] == '\0' || !now_ms) return false;
    const int written = snprintf(target_.data(), target_.size(), "%s", target);
    if (written < 0 || static_cast<size_t>(written) >= target_.size()) return false;
    session_ = &session;
    now_ms_ = now_ms;
    reset_state();
    return true;
}

esp_desktop_buddy_folder_push_config_t OtaFolderPushSink::config()
{
    esp_desktop_buddy_folder_push_config_t result{};
    result.sink.begin_transfer = &begin_transfer_trampoline;
    result.sink.begin_file = &begin_file_trampoline;
    result.sink.write_chunk = &write_chunk_trampoline;
    result.sink.end_file = &end_file_trampoline;
    result.sink.end_transfer = &end_transfer_trampoline;
    result.sink.abort_transfer = &abort_transfer_trampoline;
    result.sink.ctx = this;
    return result;
}

esp_desktop_buddy_folder_push_sink_result_t OtaFolderPushSink::begin_transfer(
    const char* name, uint32_t total_bytes)
{
    if (!session_ || !name || strcmp(name, kTransferName) != 0 || total_bytes == 0) {
        return invalid("transfer_envelope");
    }
    if (stage_ != Stage::Idle || session_->busy()) return rejected("ota_busy");
    reset_state();
    // Preserve the sender-declared total until the manifest is parsed. The
    // manifest is accepted only when manifest_bytes + image_bytes equals this
    // value, binding the transfer envelope to the image metadata.
    transfer_expected_ = total_bytes;
    stage_ = Stage::AwaitManifest;
    return ok();
}

esp_desktop_buddy_folder_push_sink_result_t OtaFolderPushSink::begin_file(
    const char* path, uint32_t size)
{
    if (!path || size == 0) return invalid("file_envelope");
    if (stage_ == Stage::AwaitManifest) {
        if (strcmp(path, kManifestPath) != 0 || size >= manifest_.size()) {
            return invalid("manifest_file");
        }
        manifest_expected_ = size;
        manifest_size_ = 0;
        stage_ = Stage::ManifestFile;
        return ok();
    }
    if (stage_ == Stage::AwaitFirmware) {
        const OtaSnapshot snapshot = session_->snapshot();
        // Parsing a valid manifest only moves the session to Confirming. Flash
        // bytes are rejected until the physical UI confirmation moves it to
        // Receiving, so transport progress cannot bypass local consent.
        if (strcmp(path, kFirmwarePath) != 0 ||
            snapshot.phase != OtaPhase::Receiving || size != firmware_expected_) {
            return rejected("firmware_not_authorized");
        }
        firmware_written_ = 0;
        stage_ = Stage::FirmwareFile;
        return ok();
    }
    return rejected("file_sequence");
}

esp_desktop_buddy_folder_push_sink_result_t OtaFolderPushSink::write_chunk(
    const uint8_t* data, size_t len)
{
    if (!data || len == 0) return invalid("empty_chunk");
    if (stage_ == Stage::ManifestFile) {
        if (manifest_size_ > manifest_expected_ ||
            len > manifest_expected_ - manifest_size_ ||
            len >= manifest_.size() - manifest_size_) {
            return invalid("manifest_size");
        }
        memcpy(manifest_.data() + manifest_size_, data, len);
        manifest_size_ += len;
        return ok();
    }
    if (stage_ == Stage::FirmwareFile) {
        if (firmware_written_ > firmware_expected_ ||
            len > firmware_expected_ - firmware_written_ ||
            !session_->write(firmware_written_, data, len, now_ms_())) {
            return storage_failed("firmware_write");
        }
        firmware_written_ += static_cast<uint32_t>(len);
        return ok();
    }
    return rejected("chunk_sequence");
}

esp_desktop_buddy_folder_push_sink_result_t OtaFolderPushSink::end_file()
{
    if (stage_ == Stage::ManifestFile) {
        if (manifest_size_ != manifest_expected_ || !accept_manifest()) {
            return invalid("manifest_content");
        }
        stage_ = Stage::AwaitFirmware;
        return ok();
    }
    if (stage_ == Stage::FirmwareFile) {
        if (firmware_written_ != firmware_expected_) {
            return invalid("firmware_size");
        }
        stage_ = Stage::FirmwareComplete;
        return ok();
    }
    return rejected("file_end_sequence");
}

esp_desktop_buddy_folder_push_sink_result_t OtaFolderPushSink::end_transfer()
{
    if (stage_ != Stage::FirmwareComplete) return rejected("transfer_incomplete");
    if (!session_->end()) {
        reset_state();
        return invalid("firmware_verification");
    }
    stage_ = Stage::Completed;
    return ok();
}

void OtaFolderPushSink::abort_transfer()
{
    if (session_ && session_->busy() &&
        stage_ != Stage::Idle && stage_ != Stage::Completed) {
        session_->abort();
    }
    reset_state();
}

bool OtaFolderPushSink::accept_manifest()
{
    manifest_[manifest_size_] = '\0';
    cJSON* root = usage_json_parse(manifest_.data(), manifest_size_);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    const char* project = json_string(root, "firmware_project");
    const char* schema = json_string(root, "schema");
    const char* target = json_string(root, "target");
    const char* version = json_string(root, "version");
    const char* sha256 = json_string(root, "sha256");
    if (project && target && (strcmp(project, "quotaframe") != 0 || strcmp(target, target_.data()) != 0)) {
        session_->reject_identity(strcmp(project, "quotaframe") == 0);
        cJSON_Delete(root);
        return false;
    }
    uint32_t size = 0;
    // Require exactly the six schema-1 fields. Silently accepting extra keys
    // would widen the OTA protocol without an explicit version/capability
    // change and could give future senders a false impression that they matter.
    const bool valid = cJSON_GetArraySize(root) == 6 &&
        project && strcmp(project, "quotaframe") == 0 &&
        schema && strcmp(schema, "1") == 0 &&
        target && strcmp(target, target_.data()) == 0 && version && sha256 &&
        json_u32(cJSON_GetObjectItemCaseSensitive(root, "size"), size) &&
        size <= UINT32_MAX - manifest_expected_ &&
        transfer_expected_ == manifest_expected_ + size;
    if (valid) firmware_expected_ = size;
    // OtaSession::begin validates the digest/version and enters Confirming; it
    // intentionally does not open the flash partition before UI confirmation.
    const bool accepted = valid && session_->begin(size, sha256, version, now_ms_());
    cJSON_Delete(root);
    return accepted;
}

void OtaFolderPushSink::reset_state()
{
    stage_ = Stage::Idle;
    manifest_.fill(0);
    manifest_size_ = 0;
    manifest_expected_ = 0;
    transfer_expected_ = 0;
    firmware_expected_ = 0;
    firmware_written_ = 0;
}

uint64_t OtaFolderPushSink::default_now_ms()
{
    // ESP timer is monotonic microseconds since boot. OTA timeout accounting is
    // intentionally independent of RTC/wall-clock changes.
    return static_cast<uint64_t>(esp_timer_get_time()) / 1000;
}

esp_desktop_buddy_folder_push_sink_result_t
OtaFolderPushSink::begin_transfer_trampoline(
    void* context, const char* name, uint32_t total_bytes)
{
    return static_cast<OtaFolderPushSink*>(context)->begin_transfer(name, total_bytes);
}

esp_desktop_buddy_folder_push_sink_result_t OtaFolderPushSink::begin_file_trampoline(
    void* context, const char* path, uint32_t size)
{
    return static_cast<OtaFolderPushSink*>(context)->begin_file(path, size);
}

esp_desktop_buddy_folder_push_sink_result_t OtaFolderPushSink::write_chunk_trampoline(
    void* context, const uint8_t* data, size_t len)
{
    return static_cast<OtaFolderPushSink*>(context)->write_chunk(data, len);
}

esp_desktop_buddy_folder_push_sink_result_t OtaFolderPushSink::end_file_trampoline(
    void* context)
{
    return static_cast<OtaFolderPushSink*>(context)->end_file();
}

esp_desktop_buddy_folder_push_sink_result_t
OtaFolderPushSink::end_transfer_trampoline(void* context)
{
    return static_cast<OtaFolderPushSink*>(context)->end_transfer();
}

void OtaFolderPushSink::abort_transfer_trampoline(void* context)
{
    static_cast<OtaFolderPushSink*>(context)->abort_transfer();
}

}  // namespace usage_panel
