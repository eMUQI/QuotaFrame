#include "usage_json.h"
#include <stdint.h>
#include <string.h>

static bool next_codepoint(const unsigned char** cursor, const unsigned char* end, uint32_t* code)
{
    if (*cursor == end) return false;
    uint32_t c = *(*cursor)++;
    unsigned extra = 0;
    uint32_t minimum = 0;
    if (c >= 0xc2 && c <= 0xdf) { extra = 1; minimum = 0x80; c &= 0x1f; }
    else if (c >= 0xe0 && c <= 0xef) { extra = 2; minimum = 0x800; c &= 0xf; }
    else if (c >= 0xf0 && c <= 0xf4) { extra = 3; minimum = 0x10000; c &= 7; }
    else if (c >= 0x80) return false;
    for (unsigned i = 0; i < extra; ++i) {
        if (*cursor == end || (**cursor & 0xc0) != 0x80) return false;
        c = (c << 6) | (*(*cursor)++ & 0x3f);
    }
    if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return false;
    *code = c;
    return true;
}

static bool whitespace(uint32_t c)
{
    return c == 0x20 || c == 0x85 || c == 0xa0 || c == 0x1680 ||
        (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 ||
        c == 0x202f || c == 0x205f || c == 0x3000 || (c >= 9 && c <= 13);
}

bool usage_name_valid(const char* text)
{
    if (!text) return false;
    size_t bytes = strlen(text);
    if (!bytes || bytes > 128) return false;
    const unsigned char* cursor = (const unsigned char*)text;
    const unsigned char* end = cursor + bytes;
    size_t count = 0;
    uint32_t c = 0;
    while (cursor < end) {
        if (!next_codepoint(&cursor, end, &c) || ++count > 64 || c < 32 ||
            (c >= 0x7f && c <= 0x9f) || c == 0x61c || c == 0x200e || c == 0x200f ||
            (c >= 0x2028 && c <= 0x202e) || (c >= 0x2066 && c <= 0x2069) ||
            (count == 1 && whitespace(c))) return false;
    }
    return !whitespace(c);
}

static bool unique_keys(const cJSON* root, unsigned depth)
{
    if (depth > 32) return false;
    for (const cJSON* item = root->child; item; item = item->next) {
        if (cJSON_IsObject(root)) {
            for (const cJSON* next = item->next; next; next = next->next)
                if (!strcmp(item->string, next->string)) return false;
        }
        if (!unique_keys(item, depth + 1)) return false;
    }
    return true;
}

cJSON* usage_json_parse(const char* text, size_t size)
{
    if (!text || !size || size > 4096 || (size >= 3 && !memcmp(text, "\xef\xbb\xbf", 3))) return NULL;
    const unsigned char* cursor = (const unsigned char*)text;
    const unsigned char* end = cursor + size;
    uint32_t c;
    while (cursor < end) {
        if (!next_codepoint(&cursor, end, &c) || !c) return NULL;
    }
    bool string = false;
    unsigned depth = 0;
    for (size_t i = 0; i < size; ++i) {
        char ch = text[i];
        if (string) {
            if ((unsigned char)ch < 0x20) return NULL;
            if (ch == '\\') {
                if (++i >= size) return NULL;
                if (text[i] == 'u' && i + 4 < size && !memcmp(text + i + 1, "0000", 4)) return NULL;
            } else if (ch == '"') string = false;
        } else if (ch == '"') string = true;
        else if (ch == '{' || ch == '[') { if (++depth > 32) return NULL; }
        else if (ch == '}' || ch == ']') { if (!depth) return NULL; --depth; }
        else if ((unsigned char)ch < 0x20 && ch != '\t' && ch != '\r' && ch != '\n') return NULL;
        else if (ch == '-' || ch == '+' || ch == '.') return NULL;
        else if (ch >= '0' && ch <= '9') {
            uint64_t value = ch - '0';
            size_t start = i;
            while (i + 1 < size && text[i + 1] >= '0' && text[i + 1] <= '9') {
                value = value * 10 + (text[++i] - '0');
                if (value > UINT32_MAX) return NULL;
            }
            if ((i > start && text[start] == '0') ||
                (i + 1 < size && (text[i + 1] == '.' || text[i + 1] == 'e' || text[i + 1] == 'E')))
                return NULL;
        }
    }
    const char* parsed_end = NULL;
    cJSON* root = cJSON_ParseWithLengthOpts(text, size, &parsed_end, false);
    while (parsed_end && parsed_end < text + size &&
           (*parsed_end == ' ' || *parsed_end == '\t' || *parsed_end == '\r' || *parsed_end == '\n')) ++parsed_end;
    if (!cJSON_IsObject(root) || parsed_end != text + size || !unique_keys(root, 0)) {
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}

static bool version_number(const char** cursor)
{
    const char* start = *cursor;
    while (**cursor >= '0' && **cursor <= '9') ++*cursor;
    return *cursor != start && !(*start == '0' && *cursor - start > 1);
}

bool usage_version_valid(const char* text)
{
    if (!text || !*text || strlen(text) > 31) return false;
    const char* p = text;
    for (unsigned part = 0; part < 3; ++part) {
        if (!version_number(&p)) return false;
        if (part < 2 && *p++ != '.') return false;
    }
    bool metadata = false;
    if (*p == '-') ++p;
    else if (*p == '+') { metadata = true; ++p; }
    else return !*p;
    while (true) {
        const char* start = p;
        bool numeric = true;
        while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
               (*p >= '0' && *p <= '9') || *p == '-') {
            if (*p < '0' || *p > '9') numeric = false;
            ++p;
        }
        if (p == start || (!metadata && numeric && *start == '0' && p - start > 1)) return false;
        if (!*p) return true;
        if (*p == '+' && !metadata) metadata = true;
        else if (*p != '.') return false;
        ++p;
    }
}
