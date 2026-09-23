#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "cJSON.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Parse bounded UTF-8 JSON objects, rejecting duplicate keys and non-u32 numbers. */
cJSON* usage_json_parse(const char* text, size_t size);
bool usage_version_valid(const char* text);
bool usage_name_valid(const char* text);
#ifdef __cplusplus
}
#endif
