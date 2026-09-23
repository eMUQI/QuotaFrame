/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "mbedtls/base64.h"
#include "sdkconfig.h"

#include "esp_desktop_buddy/folder_push.h"

#define TAG "esp_desktop_buddy_folder_push"
#define BUDDY_FOLDER_PUSH_NAME_MAX 64
#define BUDDY_FOLDER_PUSH_PATH_MAX 64
#define BUDDY_FOLDER_PUSH_MAX_BASE64_FOR_DECODED_CHUNK \
    (4 * ((CONFIG_ESP_DESKTOP_BUDDY_FOLDER_PUSH_MAX_DECODED_CHUNK + 2) / 3))
#define BUDDY_FOLDER_PUSH_TOKEN_INVALID_REQUEST "invalid_request"
#define BUDDY_FOLDER_PUSH_TOKEN_BAD_SEQUENCE "bad_sequence"
#define BUDDY_FOLDER_PUSH_TOKEN_INVALID_PATH "invalid_path"
#define BUDDY_FOLDER_PUSH_TOKEN_INVALID_BASE64 "invalid_base64"
#define BUDDY_FOLDER_PUSH_TOKEN_TRANSFER_FAILED "transfer_failed"
#define BUDDY_FOLDER_PUSH_TOKEN_SIZE_MISMATCH "size_mismatch"
#define BUDDY_FOLDER_PUSH_TOKEN_TRANSFER_TOO_LARGE "transfer_too_large"

struct esp_desktop_buddy_folder_push {
    esp_desktop_buddy_folder_push_sink_t sink;
    bool transfer_active;
    bool file_active;
    char transfer_name[BUDDY_FOLDER_PUSH_NAME_MAX + 1];
    char current_path[BUDDY_FOLDER_PUSH_PATH_MAX + 1];
    uint32_t total_expected;
    uint32_t total_written;
    uint32_t file_expected;
    uint32_t file_written;
};

static void esp_desktop_buddy_folder_push_set_reply(esp_desktop_buddy_command_ack_t *reply,
                                        bool ok,
                                        uint32_t n,
                                        const char *error)
{
    if (reply == NULL) {
        return;
    }

    reply->ok = ok;
    reply->n = n;
    reply->error = error;
}
static esp_desktop_buddy_command_extension_result_t esp_desktop_buddy_folder_push_ack_error(uint32_t n, const char *error)
{
    return (esp_desktop_buddy_command_extension_result_t){
        .mode = ESP_DESKTOP_BUDDY_COMMAND_EXTENSION_ACK,
        .reply = {
            .ok = false,
            .n = n,
            .error = error,
        },
    };
}

static esp_desktop_buddy_command_extension_result_t esp_desktop_buddy_folder_push_ack_reply(const esp_desktop_buddy_command_ack_t *reply)
{
    if (reply == NULL) {
        return esp_desktop_buddy_folder_push_ack_error(0, BUDDY_FOLDER_PUSH_TOKEN_TRANSFER_FAILED);
    }

    return (esp_desktop_buddy_command_extension_result_t){
        .mode = ESP_DESKTOP_BUDDY_COMMAND_EXTENSION_ACK,
        .reply = *reply,
    };
}

static bool esp_desktop_buddy_folder_push_is_safe_path(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return false;
    }
    if (path[0] == '/' || strchr(path, '\\') != NULL || strstr(path, "..") != NULL) {
        return false;
    }
#if CONFIG_ESP_DESKTOP_BUDDY_FOLDER_PUSH_FLAT_PATHS_ONLY
    if (strchr(path, '/') != NULL) {
        return false;
    }
#endif
    return true;
}

static void esp_desktop_buddy_folder_push_reset_file(esp_desktop_buddy_folder_push_t *push)
{
    if (push == NULL) {
        return;
    }

    push->file_active = false;
    push->current_path[0] = '\0';
    push->file_expected = 0;
    push->file_written = 0;
}

static void esp_desktop_buddy_folder_push_reset_transfer(esp_desktop_buddy_folder_push_t *push)
{
    if (push == NULL) {
        return;
    }

    push->transfer_active = false;
    push->transfer_name[0] = '\0';
    push->total_expected = 0;
    push->total_written = 0;
    esp_desktop_buddy_folder_push_reset_file(push);
}

static const char *esp_desktop_buddy_folder_push_map_result_error(esp_desktop_buddy_folder_push_sink_result_t result)
{
    if (result.err == ESP_OK) {
        return NULL;
    }
    return BUDDY_FOLDER_PUSH_TOKEN_TRANSFER_FAILED;
}

const char *esp_desktop_buddy_folder_push_sink_reason_to_name(esp_desktop_buddy_folder_push_sink_reason_t reason)
{
    switch (reason) {
    case ESP_DESKTOP_BUDDY_FOLDER_PUSH_SINK_REASON_NONE:
        return "ESP_DESKTOP_BUDDY_FOLDER_PUSH_SINK_REASON_NONE";
    case ESP_DESKTOP_BUDDY_FOLDER_PUSH_SINK_REASON_REJECTED:
        return "ESP_DESKTOP_BUDDY_FOLDER_PUSH_SINK_REASON_REJECTED";
    case ESP_DESKTOP_BUDDY_FOLDER_PUSH_SINK_REASON_STORAGE_FAILED:
        return "ESP_DESKTOP_BUDDY_FOLDER_PUSH_SINK_REASON_STORAGE_FAILED";
    case ESP_DESKTOP_BUDDY_FOLDER_PUSH_SINK_REASON_INVALID_CONTENT:
        return "ESP_DESKTOP_BUDDY_FOLDER_PUSH_SINK_REASON_INVALID_CONTENT";
    default:
        return "ESP_DESKTOP_BUDDY_FOLDER_PUSH_SINK_REASON_UNKNOWN";
    }
}

static void esp_desktop_buddy_folder_push_log_sink_failure(const char *operation,
                                               esp_desktop_buddy_folder_push_sink_result_t result)
{
    const char *detail = result.detail;

    if (detail != NULL && detail[0] != '\0') {
        ESP_LOGW(TAG,
                 "%s failed: err=%s reason=%s detail=%s",
                 operation,
                 esp_err_to_name(result.err),
                 esp_desktop_buddy_folder_push_sink_reason_to_name(result.reason),
                 detail);
        return;
    }

    ESP_LOGW(TAG,
             "%s failed: err=%s reason=%s",
             operation,
             esp_err_to_name(result.err),
             esp_desktop_buddy_folder_push_sink_reason_to_name(result.reason));
}

static void esp_desktop_buddy_folder_push_abort(esp_desktop_buddy_folder_push_t *push)
{
    if (push == NULL || !push->transfer_active) {
        return;
    }

    if (push->sink.abort_transfer != NULL) {
        push->sink.abort_transfer(push->sink.ctx);
    }
    esp_desktop_buddy_folder_push_reset_transfer(push);
}

esp_err_t esp_desktop_buddy_folder_push_new(const esp_desktop_buddy_folder_push_config_t *config,
                                esp_desktop_buddy_folder_push_t **out_push)
{
    esp_desktop_buddy_folder_push_t *push;

    if (config == NULL || out_push == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->sink.begin_transfer == NULL ||
        config->sink.begin_file == NULL ||
        config->sink.write_chunk == NULL ||
        config->sink.end_file == NULL ||
        config->sink.end_transfer == NULL ||
        config->sink.abort_transfer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    push = calloc(1, sizeof(*push));
    if (push == NULL) {
        return ESP_ERR_NO_MEM;
    }

    push->sink = config->sink;
    *out_push = push;
    return ESP_OK;
}

void esp_desktop_buddy_folder_push_delete(esp_desktop_buddy_folder_push_t *push)
{
    if (push == NULL) {
        return;
    }

    esp_desktop_buddy_folder_push_abort(push);
    free(push);
}

esp_err_t esp_desktop_buddy_folder_push_start_transfer(esp_desktop_buddy_folder_push_t *push,
                                           const char *name,
                                           uint32_t total_bytes,
                                           esp_desktop_buddy_command_ack_t *out_reply)
{
    esp_desktop_buddy_folder_push_sink_result_t result;

    if (push == NULL || out_reply == NULL || name == NULL || name[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    if (push->transfer_active) {
        esp_desktop_buddy_folder_push_set_reply(out_reply, false, 0, BUDDY_FOLDER_PUSH_TOKEN_BAD_SEQUENCE);
        return ESP_OK;
    }
    if (strlen(name) > BUDDY_FOLDER_PUSH_NAME_MAX) {
        esp_desktop_buddy_folder_push_set_reply(out_reply, false, 0, BUDDY_FOLDER_PUSH_TOKEN_INVALID_REQUEST);
        return ESP_OK;
    }
    if (CONFIG_ESP_DESKTOP_BUDDY_FOLDER_PUSH_MAX_TRANSFER_BYTES > 0 &&
        total_bytes > (uint32_t)CONFIG_ESP_DESKTOP_BUDDY_FOLDER_PUSH_MAX_TRANSFER_BYTES) {
        esp_desktop_buddy_folder_push_set_reply(out_reply,
                                    false,
                                    0,
                                    BUDDY_FOLDER_PUSH_TOKEN_TRANSFER_TOO_LARGE);
        return ESP_OK;
    }

    result = push->sink.begin_transfer(push->sink.ctx, name, total_bytes);
    if (result.err != ESP_OK) {
        esp_desktop_buddy_folder_push_log_sink_failure("begin_transfer", result);
        esp_desktop_buddy_folder_push_set_reply(out_reply, false, 0, esp_desktop_buddy_folder_push_map_result_error(result));
        return ESP_OK;
    }

    push->transfer_active = true;
    strlcpy(push->transfer_name, name, sizeof(push->transfer_name));
    push->total_expected = total_bytes;
    push->total_written = 0;
    esp_desktop_buddy_folder_push_reset_file(push);

    esp_desktop_buddy_folder_push_set_reply(out_reply, true, 0, NULL);
    return ESP_OK;
}

esp_err_t esp_desktop_buddy_folder_push_start_file(esp_desktop_buddy_folder_push_t *push,
                                       const char *path,
                                       uint32_t size,
                                       esp_desktop_buddy_command_ack_t *out_reply)
{
    esp_desktop_buddy_folder_push_sink_result_t result;

    if (push == NULL || out_reply == NULL || path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!push->transfer_active || push->file_active) {
        esp_desktop_buddy_folder_push_set_reply(out_reply, false, 0, BUDDY_FOLDER_PUSH_TOKEN_BAD_SEQUENCE);
        return ESP_OK;
    }
    if (!esp_desktop_buddy_folder_push_is_safe_path(path) || strlen(path) > BUDDY_FOLDER_PUSH_PATH_MAX) {
        esp_desktop_buddy_folder_push_set_reply(out_reply, false, 0, BUDDY_FOLDER_PUSH_TOKEN_INVALID_PATH);
        return ESP_OK;
    }

    result = push->sink.begin_file(push->sink.ctx, path, size);
    if (result.err != ESP_OK) {
        esp_desktop_buddy_folder_push_log_sink_failure("begin_file", result);
        esp_desktop_buddy_folder_push_set_reply(out_reply, false, 0, esp_desktop_buddy_folder_push_map_result_error(result));
        esp_desktop_buddy_folder_push_abort(push);
        return ESP_OK;
    }

    push->file_active = true;
    strlcpy(push->current_path, path, sizeof(push->current_path));
    push->file_expected = size;
    push->file_written = 0;

    esp_desktop_buddy_folder_push_set_reply(out_reply, true, 0, NULL);
    return ESP_OK;
}

esp_err_t esp_desktop_buddy_folder_push_write_chunk_b64(esp_desktop_buddy_folder_push_t *push,
                                               const char *base64_data,
                                               esp_desktop_buddy_command_ack_t *out_reply)
{
    uint8_t decoded[CONFIG_ESP_DESKTOP_BUDDY_FOLDER_PUSH_MAX_DECODED_CHUNK];
    size_t decoded_len = 0;
    esp_desktop_buddy_folder_push_sink_result_t result;
    int rc;

    if (push == NULL || out_reply == NULL || base64_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!push->transfer_active || !push->file_active) {
        esp_desktop_buddy_folder_push_set_reply(out_reply, false, 0, BUDDY_FOLDER_PUSH_TOKEN_BAD_SEQUENCE);
        return ESP_OK;
    }
    if (strlen(base64_data) > CONFIG_ESP_DESKTOP_BUDDY_FOLDER_PUSH_MAX_RAW_CHUNK ||
        strlen(base64_data) > BUDDY_FOLDER_PUSH_MAX_BASE64_FOR_DECODED_CHUNK) {
        esp_desktop_buddy_folder_push_set_reply(out_reply, false, 0, BUDDY_FOLDER_PUSH_TOKEN_INVALID_REQUEST);
        return ESP_OK;
    }

    rc = mbedtls_base64_decode(decoded,
                               sizeof(decoded),
                               &decoded_len,
                               (const unsigned char *)base64_data,
                               strlen(base64_data));
    if (rc != 0) {
        esp_desktop_buddy_folder_push_set_reply(out_reply, false, 0, BUDDY_FOLDER_PUSH_TOKEN_INVALID_BASE64);
        return ESP_OK;
    }
    if (decoded_len > (size_t)(push->file_expected - push->file_written) ||
        decoded_len > (size_t)(push->total_expected - push->total_written)) {
        esp_desktop_buddy_folder_push_set_reply(out_reply,
                                    false,
                                    push->file_written,
                                    BUDDY_FOLDER_PUSH_TOKEN_SIZE_MISMATCH);
        esp_desktop_buddy_folder_push_abort(push);
        return ESP_OK;
    }

    result = push->sink.write_chunk(push->sink.ctx, decoded, decoded_len);
    if (result.err != ESP_OK) {
        esp_desktop_buddy_folder_push_log_sink_failure("write_chunk", result);
        esp_desktop_buddy_folder_push_set_reply(out_reply, false, 0, esp_desktop_buddy_folder_push_map_result_error(result));
        esp_desktop_buddy_folder_push_abort(push);
        return ESP_OK;
    }

    push->file_written += (uint32_t)decoded_len;
    push->total_written += (uint32_t)decoded_len;
    esp_desktop_buddy_folder_push_set_reply(out_reply, true, push->file_written, NULL);
    return ESP_OK;
}

esp_err_t esp_desktop_buddy_folder_push_finish_file(esp_desktop_buddy_folder_push_t *push,
                                     esp_desktop_buddy_command_ack_t *out_reply)
{
    esp_desktop_buddy_folder_push_sink_result_t result;
    uint32_t written;

    if (push == NULL || out_reply == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!push->transfer_active || !push->file_active) {
        esp_desktop_buddy_folder_push_set_reply(out_reply, false, 0, BUDDY_FOLDER_PUSH_TOKEN_BAD_SEQUENCE);
        return ESP_OK;
    }

    written = push->file_written;
    if (push->file_expected != written) {
        esp_desktop_buddy_folder_push_set_reply(out_reply, false, written, BUDDY_FOLDER_PUSH_TOKEN_SIZE_MISMATCH);
        esp_desktop_buddy_folder_push_abort(push);
        return ESP_OK;
    }

    result = push->sink.end_file(push->sink.ctx);
    if (result.err != ESP_OK) {
        esp_desktop_buddy_folder_push_log_sink_failure("end_file", result);
        esp_desktop_buddy_folder_push_set_reply(out_reply, false, written, esp_desktop_buddy_folder_push_map_result_error(result));
        esp_desktop_buddy_folder_push_abort(push);
        return ESP_OK;
    }

    esp_desktop_buddy_folder_push_reset_file(push);
    esp_desktop_buddy_folder_push_set_reply(out_reply, true, written, NULL);
    return ESP_OK;
}

esp_err_t esp_desktop_buddy_folder_push_finish_transfer(esp_desktop_buddy_folder_push_t *push,
                                         esp_desktop_buddy_command_ack_t *out_reply)
{
    esp_desktop_buddy_folder_push_sink_result_t result;
    uint32_t written;

    if (push == NULL || out_reply == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!push->transfer_active || push->file_active) {
        esp_desktop_buddy_folder_push_set_reply(out_reply, false, 0, BUDDY_FOLDER_PUSH_TOKEN_BAD_SEQUENCE);
        return ESP_OK;
    }
    if (push->total_written != push->total_expected) {
        written = push->total_written;
        esp_desktop_buddy_folder_push_set_reply(out_reply, false, written, BUDDY_FOLDER_PUSH_TOKEN_SIZE_MISMATCH);
        esp_desktop_buddy_folder_push_abort(push);
        return ESP_OK;
    }

    result = push->sink.end_transfer(push->sink.ctx);
    if (result.err != ESP_OK) {
        esp_desktop_buddy_folder_push_log_sink_failure("end_transfer", result);
        esp_desktop_buddy_folder_push_set_reply(out_reply, false, 0, esp_desktop_buddy_folder_push_map_result_error(result));
        esp_desktop_buddy_folder_push_reset_transfer(push);
        return ESP_OK;
    }

    esp_desktop_buddy_folder_push_reset_transfer(push);
    esp_desktop_buddy_folder_push_set_reply(out_reply, true, 0, NULL);
    return ESP_OK;
}

static esp_desktop_buddy_command_extension_result_t esp_desktop_buddy_folder_push_start_transfer_command(
    void *ctx,
    esp_desktop_buddy_t *buddy,
    const esp_desktop_buddy_command_view_t *request)
{
    esp_desktop_buddy_folder_push_t *push = (esp_desktop_buddy_folder_push_t *)ctx;
    const char *name = NULL;
    uint32_t total_bytes = 0;
    esp_desktop_buddy_command_ack_t reply = {0};
    esp_err_t err;

    (void)buddy;

    if (esp_desktop_buddy_command_view_get_string(request, "name", &name) != ESP_OK ||
        esp_desktop_buddy_command_view_get_u32(request, "total", &total_bytes) != ESP_OK) {
        return (esp_desktop_buddy_command_extension_result_t){
            .mode = ESP_DESKTOP_BUDDY_COMMAND_EXTENSION_ACK,
            .reply = {
                .ok = false,
                .n = 0,
                .error = BUDDY_FOLDER_PUSH_TOKEN_INVALID_REQUEST,
            },
        };
    }

    err = esp_desktop_buddy_folder_push_start_transfer(push, name, total_bytes, &reply);
    return err == ESP_OK ? esp_desktop_buddy_folder_push_ack_reply(&reply)
                         : (esp_desktop_buddy_command_extension_result_t){
                               .mode = ESP_DESKTOP_BUDDY_COMMAND_EXTENSION_ACK,
                               .reply = {
                                   .ok = false,
                                   .n = 0,
                                   .error = BUDDY_FOLDER_PUSH_TOKEN_TRANSFER_FAILED,
                               },
                           };
}

static esp_desktop_buddy_command_extension_result_t esp_desktop_buddy_folder_push_start_file_command(
    void *ctx,
    esp_desktop_buddy_t *buddy,
    const esp_desktop_buddy_command_view_t *request)
{
    esp_desktop_buddy_folder_push_t *push = (esp_desktop_buddy_folder_push_t *)ctx;
    const char *path = NULL;
    uint32_t size = 0;
    esp_desktop_buddy_command_ack_t reply = {0};
    esp_err_t err;

    (void)buddy;

    if (esp_desktop_buddy_command_view_get_string(request, "path", &path) != ESP_OK ||
        esp_desktop_buddy_command_view_get_u32(request, "size", &size) != ESP_OK) {
        return (esp_desktop_buddy_command_extension_result_t){
            .mode = ESP_DESKTOP_BUDDY_COMMAND_EXTENSION_ACK,
            .reply = {
                .ok = false,
                .n = 0,
                .error = BUDDY_FOLDER_PUSH_TOKEN_INVALID_REQUEST,
            },
        };
    }

    err = esp_desktop_buddy_folder_push_start_file(push, path, size, &reply);
    return err == ESP_OK ? esp_desktop_buddy_folder_push_ack_reply(&reply)
                         : (esp_desktop_buddy_command_extension_result_t){
                               .mode = ESP_DESKTOP_BUDDY_COMMAND_EXTENSION_ACK,
                               .reply = {
                                   .ok = false,
                                   .n = 0,
                                   .error = BUDDY_FOLDER_PUSH_TOKEN_TRANSFER_FAILED,
                               },
                           };
}

static esp_desktop_buddy_command_extension_result_t esp_desktop_buddy_folder_push_write_chunk_command(
    void *ctx,
    esp_desktop_buddy_t *buddy,
    const esp_desktop_buddy_command_view_t *request)
{
    esp_desktop_buddy_folder_push_t *push = (esp_desktop_buddy_folder_push_t *)ctx;
    const char *base64_data = NULL;
    esp_desktop_buddy_command_ack_t reply = {0};
    esp_err_t err;

    (void)buddy;

    if (esp_desktop_buddy_command_view_get_string(request, "d", &base64_data) != ESP_OK) {
        return (esp_desktop_buddy_command_extension_result_t){
            .mode = ESP_DESKTOP_BUDDY_COMMAND_EXTENSION_ACK,
            .reply = {
                .ok = false,
                .n = 0,
                .error = BUDDY_FOLDER_PUSH_TOKEN_INVALID_REQUEST,
            },
        };
    }

    err = esp_desktop_buddy_folder_push_write_chunk_b64(push, base64_data, &reply);
    return err == ESP_OK ? esp_desktop_buddy_folder_push_ack_reply(&reply)
                         : (esp_desktop_buddy_command_extension_result_t){
                               .mode = ESP_DESKTOP_BUDDY_COMMAND_EXTENSION_ACK,
                               .reply = {
                                   .ok = false,
                                   .n = 0,
                                   .error = BUDDY_FOLDER_PUSH_TOKEN_TRANSFER_FAILED,
                               },
                           };
}

static esp_desktop_buddy_command_extension_result_t esp_desktop_buddy_folder_push_finish_file_command(
    void *ctx,
    esp_desktop_buddy_t *buddy,
    const esp_desktop_buddy_command_view_t *request)
{
    esp_desktop_buddy_folder_push_t *push = (esp_desktop_buddy_folder_push_t *)ctx;
    esp_desktop_buddy_command_ack_t reply = {0};
    esp_err_t err;

    (void)buddy;
    (void)request;

    err = esp_desktop_buddy_folder_push_finish_file(push, &reply);
    return err == ESP_OK ? esp_desktop_buddy_folder_push_ack_reply(&reply)
                         : (esp_desktop_buddy_command_extension_result_t){
                               .mode = ESP_DESKTOP_BUDDY_COMMAND_EXTENSION_ACK,
                               .reply = {
                                   .ok = false,
                                   .n = 0,
                                   .error = BUDDY_FOLDER_PUSH_TOKEN_TRANSFER_FAILED,
                               },
                           };
}

static esp_desktop_buddy_command_extension_result_t esp_desktop_buddy_folder_push_finish_transfer_command(
    void *ctx,
    esp_desktop_buddy_t *buddy,
    const esp_desktop_buddy_command_view_t *request)
{
    esp_desktop_buddy_folder_push_t *push = (esp_desktop_buddy_folder_push_t *)ctx;
    esp_desktop_buddy_command_ack_t reply = {0};
    esp_err_t err;

    (void)buddy;
    (void)request;

    err = esp_desktop_buddy_folder_push_finish_transfer(push, &reply);
    return err == ESP_OK ? esp_desktop_buddy_folder_push_ack_reply(&reply)
                         : (esp_desktop_buddy_command_extension_result_t){
                               .mode = ESP_DESKTOP_BUDDY_COMMAND_EXTENSION_ACK,
                               .reply = {
                                   .ok = false,
                                   .n = 0,
                                   .error = BUDDY_FOLDER_PUSH_TOKEN_TRANSFER_FAILED,
                               },
                           };
}

static esp_desktop_buddy_command_extension_result_t esp_desktop_buddy_folder_push_drop_char_begin_command(
    void *ctx,
    esp_desktop_buddy_t *buddy,
    const esp_desktop_buddy_command_view_t *request)
{
    (void)ctx;
    (void)buddy;
    (void)request;

    return (esp_desktop_buddy_command_extension_result_t){
        .mode = ESP_DESKTOP_BUDDY_COMMAND_EXTENSION_NO_ACK,
    };
}

esp_desktop_buddy_command_extension_set_t esp_desktop_buddy_folder_push_command_extension(esp_desktop_buddy_folder_push_t *push)
{
    static const esp_desktop_buddy_command_extension_entry_t s_supported_bindings[] = {
        { .command = "char_begin", .handler = esp_desktop_buddy_folder_push_start_transfer_command },
        { .command = "file", .handler = esp_desktop_buddy_folder_push_start_file_command },
        { .command = "chunk", .handler = esp_desktop_buddy_folder_push_write_chunk_command },
        { .command = "file_end", .handler = esp_desktop_buddy_folder_push_finish_file_command },
        { .command = "char_end", .handler = esp_desktop_buddy_folder_push_finish_transfer_command },
    };
    static const esp_desktop_buddy_command_extension_entry_t s_refusal_bindings[] = {
        { .command = "char_begin", .handler = esp_desktop_buddy_folder_push_drop_char_begin_command },
    };

    return (esp_desktop_buddy_command_extension_set_t){
        .ctx = push,
        .bindings = push != NULL ? s_supported_bindings : s_refusal_bindings,
        .binding_count = push != NULL
                             ? sizeof(s_supported_bindings) / sizeof(s_supported_bindings[0])
                             : sizeof(s_refusal_bindings) / sizeof(s_refusal_bindings[0]),
    };
}
