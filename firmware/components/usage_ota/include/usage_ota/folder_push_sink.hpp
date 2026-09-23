#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "esp_desktop_buddy/folder_push.h"
#include "usage_ota/session.hpp"

namespace usage_panel {

using OtaFolderPushNowMs = uint64_t (*)();

/**
 * Adapts the generic Folder Push protocol to the firmware OTA state machine.
 *
 * One transfer must contain exactly `manifest.json` followed by `firmware.bin`.
 * The manifest must declare the bound target, firmware size/version/digest, and
 * the transfer byte total must equal manifest bytes plus firmware bytes. The
 * manifest starts OtaSession confirmation; firmware data is accepted only
 * after the session has entered Receiving.
 */
class OtaFolderPushSink {
public:
    OtaFolderPushSink() = default;

    /**
     * Binds a session and public target identifier. Both must remain valid for
     * the sink lifetime. Returns false for an empty/oversized target or clock.
     */
    bool bind(
        OtaSession& session, const char* target,
        OtaFolderPushNowMs now_ms = default_now_ms);

    /** Returns callbacks suitable for esp_desktop_buddy_folder_push_new(). */
    esp_desktop_buddy_folder_push_config_t config();

private:
    // Protocol order is intentionally strict: accepting files out of order
    // would let the transport bypass manifest authorization or confirmation.
    enum class Stage : uint8_t {
        Idle,
        AwaitManifest,
        ManifestFile,
        AwaitFirmware,
        FirmwareFile,
        FirmwareComplete,
        Completed,
    };

    static uint64_t default_now_ms();
    static esp_desktop_buddy_folder_push_sink_result_t begin_transfer_trampoline(
        void*, const char*, uint32_t);
    static esp_desktop_buddy_folder_push_sink_result_t begin_file_trampoline(
        void*, const char*, uint32_t);
    static esp_desktop_buddy_folder_push_sink_result_t write_chunk_trampoline(
        void*, const uint8_t*, size_t);
    static esp_desktop_buddy_folder_push_sink_result_t end_file_trampoline(void*);
    static esp_desktop_buddy_folder_push_sink_result_t end_transfer_trampoline(void*);
    static void abort_transfer_trampoline(void*);

    esp_desktop_buddy_folder_push_sink_result_t begin_transfer(
        const char* name, uint32_t total_bytes);
    esp_desktop_buddy_folder_push_sink_result_t begin_file(
        const char* path, uint32_t size);
    esp_desktop_buddy_folder_push_sink_result_t write_chunk(
        const uint8_t* data, size_t len);
    esp_desktop_buddy_folder_push_sink_result_t end_file();
    esp_desktop_buddy_folder_push_sink_result_t end_transfer();
    void abort_transfer();
    bool accept_manifest();
    void reset_state();

    OtaSession* session_ = nullptr;
    OtaFolderPushNowMs now_ms_ = default_now_ms;
    Stage stage_ = Stage::Idle;
    std::array<char, 32> target_{};
    std::array<char, 513> manifest_{};
    size_t manifest_size_ = 0;
    uint32_t manifest_expected_ = 0;
    uint32_t transfer_expected_ = 0;
    uint32_t firmware_expected_ = 0;
    uint32_t firmware_written_ = 0;
};

}  // namespace usage_panel
