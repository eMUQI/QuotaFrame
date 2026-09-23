/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_desktop_buddy/command_extensions.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct esp_desktop_buddy_folder_push esp_desktop_buddy_folder_push_t;

/** Stable folder-push sink failure reasons. */
typedef enum {
    ESP_DESKTOP_BUDDY_FOLDER_PUSH_SINK_REASON_NONE = 0,
    /** Sink rejected the transfer for application or policy reasons. */
    ESP_DESKTOP_BUDDY_FOLDER_PUSH_SINK_REASON_REJECTED,
    /** Sink failed to persist, stage, or promote transferred data. */
    ESP_DESKTOP_BUDDY_FOLDER_PUSH_SINK_REASON_STORAGE_FAILED,
    /** Sink accepted the protocol flow, but rejected the transferred content. */
    ESP_DESKTOP_BUDDY_FOLDER_PUSH_SINK_REASON_INVALID_CONTENT,
} esp_desktop_buddy_folder_push_sink_reason_t;

/** Sink callback result used by the folder-push state machine. */
typedef struct {
    esp_err_t err;
    esp_desktop_buddy_folder_push_sink_reason_t reason;
    const char *detail;
} esp_desktop_buddy_folder_push_sink_result_t;

/**
 * @brief Returns a successful sink result.
 *
 * @return Successful sink result wrapper.
 */
static inline esp_desktop_buddy_folder_push_sink_result_t esp_desktop_buddy_folder_push_result_ok(void)
{
    return (esp_desktop_buddy_folder_push_sink_result_t){
        .err = ESP_OK,
        .reason = ESP_DESKTOP_BUDDY_FOLDER_PUSH_SINK_REASON_NONE,
        .detail = NULL,
    };
}

/**
 * @brief Returns a failed sink result with a standard error code, stable reason, and optional detail.
 *
 * @param err ESP-IDF error code for the broad failure category.
 * @param reason Stable folder-push sink reason for application diagnostics.
 * @param detail Optional internal detail string for logs or diagnostics.
 *
 * @return Failed sink result wrapper.
 */
static inline esp_desktop_buddy_folder_push_sink_result_t esp_desktop_buddy_folder_push_result_err(esp_err_t err,
                                                           esp_desktop_buddy_folder_push_sink_reason_t reason,
                                                           const char *detail)
{
    return (esp_desktop_buddy_folder_push_sink_result_t){
        .err = err,
        .reason = reason,
        .detail = detail,
    };
}

/**
 * @brief Returns a stable name for folder-push sink reasons.
 *
 * @param reason Sink reason to stringify.
 *
 * @return Stable reason name for logs and diagnostics.
 */
const char *esp_desktop_buddy_folder_push_sink_reason_to_name(esp_desktop_buddy_folder_push_sink_reason_t reason);

/** Content-agnostic sink contract used to persist transferred data. */
typedef struct {
    esp_desktop_buddy_folder_push_sink_result_t (*begin_transfer)(void *ctx, const char *name, uint32_t total_bytes);
    esp_desktop_buddy_folder_push_sink_result_t (*begin_file)(void *ctx, const char *path, uint32_t size);
    esp_desktop_buddy_folder_push_sink_result_t (*write_chunk)(void *ctx, const uint8_t *data, size_t len);
    esp_desktop_buddy_folder_push_sink_result_t (*end_file)(void *ctx);
    esp_desktop_buddy_folder_push_sink_result_t (*end_transfer)(void *ctx);
    void (*abort_transfer)(void *ctx);
    void *ctx;
} esp_desktop_buddy_folder_push_sink_t;

/** Configuration used to create a folder-push handler. */
typedef struct {
    esp_desktop_buddy_folder_push_sink_t sink;
} esp_desktop_buddy_folder_push_config_t;

/**
 * @brief Creates a folder-push state machine with the supplied sink callbacks.
 *
 * @param config Folder-push configuration to apply.
 * @param[out] out_push Returned folder-push state machine.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if the configuration is incomplete
 *      - ESP_ERR_NO_MEM if allocation fails
 */
esp_err_t esp_desktop_buddy_folder_push_new(const esp_desktop_buddy_folder_push_config_t *config,
                                esp_desktop_buddy_folder_push_t **out_push);
/**
 * @brief Destroys a folder-push state machine.
 *
 * @param push Folder-push state machine to destroy.
 */
void esp_desktop_buddy_folder_push_delete(esp_desktop_buddy_folder_push_t *push);
/**
 * @brief Returns an optional command extension backed by a folder-push state machine.
 *
 * Pass `push == NULL` to expose a char-begin refusal adapter instead of the
 * full folder-push command family. In that mode the returned extension binds
 * only `char_begin` and consumes it without acking it, matching the desktop
 * reference behavior for devices that do not accept pushed files.
 *
 * @param push Folder-push state machine to expose through the core command-extension interface.
 *
 * @return Optional command-extension table backed by @p push.
 */
esp_desktop_buddy_command_extension_set_t esp_desktop_buddy_folder_push_command_extension(esp_desktop_buddy_folder_push_t *push);

/**
 * @brief Begins one transfer through the folder-push state machine.
 *
 * @param push Folder-push state machine that owns transfer state.
 * @param name Public transfer name.
 * @param total_bytes Total decoded payload bytes expected for the transfer.
 * @param[out] out_reply Reply payload to emit on the wire.
 *
 * @return ESP_OK on protocol handling success, or ESP_ERR_INVALID_ARG if the arguments are invalid.
 */
esp_err_t esp_desktop_buddy_folder_push_start_transfer(esp_desktop_buddy_folder_push_t *push,
                                           const char *name,
                                           uint32_t total_bytes,
                                           esp_desktop_buddy_command_ack_t *out_reply);
/**
 * @brief Begins one file within the active transfer.
 *
 * @param push Folder-push state machine that owns transfer state.
 * @param path Relative path for the file being transferred.
 * @param size Total decoded payload bytes expected for the file.
 * @param[out] out_reply Reply payload to emit on the wire.
 *
 * @return ESP_OK on protocol handling success, or ESP_ERR_INVALID_ARG if the arguments are invalid.
 */
esp_err_t esp_desktop_buddy_folder_push_start_file(esp_desktop_buddy_folder_push_t *push,
                                       const char *path,
                                       uint32_t size,
                                       esp_desktop_buddy_command_ack_t *out_reply);
/**
 * @brief Writes one base64-encoded data chunk into the active file.
 *
 * @param push Folder-push state machine that owns transfer state.
 * @param base64_data Base64-encoded chunk payload.
 * @param[out] out_reply Reply payload to emit on the wire.
 *
 * @return ESP_OK on protocol handling success, or ESP_ERR_INVALID_ARG if the arguments are invalid.
 */
esp_err_t esp_desktop_buddy_folder_push_write_chunk_b64(esp_desktop_buddy_folder_push_t *push,
                                               const char *base64_data,
                                               esp_desktop_buddy_command_ack_t *out_reply);
/**
 * @brief Ends the active file within the transfer.
 *
 * @param push Folder-push state machine that owns transfer state.
 * @param[out] out_reply Reply payload to emit on the wire.
 *
 * @return ESP_OK on protocol handling success, or ESP_ERR_INVALID_ARG if the arguments are invalid.
 */
esp_err_t esp_desktop_buddy_folder_push_finish_file(esp_desktop_buddy_folder_push_t *push,
                                     esp_desktop_buddy_command_ack_t *out_reply);
/**
 * @brief Ends the active transfer.
 *
 * @param push Folder-push state machine that owns transfer state.
 * @param[out] out_reply Reply payload to emit on the wire.
 *
 * @return ESP_OK on protocol handling success, or ESP_ERR_INVALID_ARG if the arguments are invalid.
 */
esp_err_t esp_desktop_buddy_folder_push_finish_transfer(esp_desktop_buddy_folder_push_t *push,
                                         esp_desktop_buddy_command_ack_t *out_reply);

#ifdef __cplusplus
}
#endif
