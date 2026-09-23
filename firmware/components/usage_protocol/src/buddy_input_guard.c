#include "usage_json.h"
#include "buddy_internal.h"

void __real_buddy_protocol_process_line(esp_desktop_buddy_t*, const char*, size_t);

static void reject_types(esp_desktop_buddy_t* buddy, const cJSON* root)
{
    const cJSON* cmd = cJSON_GetObjectItemCaseSensitive(root, "cmd");
    if (!cJSON_IsString(cmd)) return;
    const cJSON* seq = cJSON_GetObjectItemCaseSensitive(root, "seq");
    uint64_t value = 0;
    if (cJSON_IsString(seq)) {
        const char* p = seq->valuestring;
        bool valid = *p && !(*p == '0' && p[1]);
        for (; valid && *p; ++p) {
            if (*p < '0' || *p > '9') { valid = false; break; }
            value = value * 10 + (*p - '0');
            if (value > UINT32_MAX) valid = false;
        }
        if (!valid) value = 0;
    }
    buddy_codec_queue_ack(buddy, cmd->valuestring, false, (uint32_t)value, "invalid_request");
}

void __wrap_buddy_protocol_process_line(esp_desktop_buddy_t* buddy, const char* line, size_t size)
{
    cJSON* root = usage_json_parse(line, size);
    if (!root) return;
    const char* strings[] = {"cmd", "v", "seq", "provider", "state", "sampled_at", "sent_at",
        "short_used_pct", "week_used_pct", "short_reset_at", "week_reset_at", "year", "month",
        "day", "weekday", "hour", "minute", "second", "utc_offset_min", "previous", "name", "path", "d"};
    for (size_t i = 0; i < sizeof(strings) / sizeof(strings[0]); ++i) {
        const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, strings[i]);
        if (item && !cJSON_IsString(item)) { reject_types(buddy, root); cJSON_Delete(root); return; }
    }
    const char* numbers[] = {"size", "total"};
    for (size_t i = 0; i < sizeof(numbers) / sizeof(numbers[0]); ++i) {
        const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, numbers[i]);
        if (item && !cJSON_IsNumber(item)) { reject_types(buddy, root); cJSON_Delete(root); return; }
    }
    cJSON_Delete(root);
    __real_buddy_protocol_process_line(buddy, line, size);
}

/* Buddy's pinned line-buffer layout is checked by CMake before using this boundary. */
void __wrap_buddy_linebuf_feed(esp_desktop_buddy_t* buddy, const uint8_t* data, size_t size)
{
    buddy_linebuf_t* buffer = &buddy->linebuf;
    for (size_t i = 0; i < size; ++i) {
        char c = (char)data[i];
        if (c == '\n') {
            if (!buffer->dropping) {
                if (buffer->len && buffer->buf[buffer->len - 1] == '\r') --buffer->len;
                if (buffer->len) {
                    buffer->buf[buffer->len] = 0;
                    __wrap_buddy_protocol_process_line(buddy, buffer->buf, buffer->len);
                }
            }
            buffer->len = 0;
            buffer->dropping = false;
        } else if (!buffer->dropping) {
            if (buffer->len < ESP_DESKTOP_BUDDY_LINE_MAX ||
                (buffer->len == ESP_DESKTOP_BUDDY_LINE_MAX && c == '\r')) {
                buffer->buf[buffer->len++] = c;
            } else {
                buffer->len = 0;
                buffer->dropping = true;
                buddy_emit_error(buddy, ESP_DESKTOP_BUDDY_ERROR_INPUT, ESP_DESKTOP_BUDDY_LINE_MAX);
            }
        }
    }
}
