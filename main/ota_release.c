/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：公开升级清单的严格解析与版本比较。/ English: strict public release parsing and version comparison.
 */
#include "ota_online.h"
#include "cJSON.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 只接收正式版本号，拒绝截断、溢出和未知后缀。/ Accept bounded release versions without truncation or unknown suffixes.
static bool version_parts(const char *s, unsigned parts[4]) {
    if (!s || !*s || strlen(s) > 32) return false;
    memset(parts, 0, 4 * sizeof(*parts));
    for (unsigned i = 0; i < 3; ++i) {
        if (!isdigit((unsigned char)*s)) return false;
        char *end; unsigned long n = strtoul(s, &end, 10);
        if (n > 65535) return false;
        parts[i] = (unsigned)n; s = end;
        if (i < 2 && *s++ != '.') return false;
    }
    // 正式版排在该版本所有 rc 之后。/ A stable release sorts after its release candidates.
    parts[3] = 65536;
    if (!*s) return true;
    if (strncmp(s, "-rc", 3)) return false;
    s += 3;
    if (!isdigit((unsigned char)*s)) return false;
    char *end; unsigned long n = strtoul(s, &end, 10);
    if (*end || n > 65535) return false;
    parts[3] = (unsigned)n;
    return true;
}
int pico_version_compare(const char *a, const char *b) {
    unsigned left[4], right[4];
    if (!version_parts(a, left) || !version_parts(b, right)) return 0;
    for (unsigned i = 0; i < 4; ++i) {
        if (left[i] < right[i]) return -1;
        if (left[i] > right[i]) return 1;
    }
    return 0;
}
bool pico_release_url_valid(const char *url) {
    // 升级源限定官方 HTTPS 站点，避免重定向泄漏或被替换为任意下载源。
    // Limit updates to the official HTTPS origin; do not follow arbitrary redirects.
    static const char *const prefixes[] = {
        "https://kiikoread.com/", "https://wegooo-cell.github.io/read-pico-reader/"
    };
    if (!url || strlen(url) >= 256) return false;
    const char *name = NULL;
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i) {
        size_t n = strlen(prefixes[i]);
        if (!strncmp(url, prefixes[i], n)) { name = url + n; break; }
    }
    if (!name) return false;
    if (!*name || strstr(name, "..") || strchr(name, '/') || strchr(name, '\\')) return false;
    for (const char *p = name; *p; ++p)
        if (!isalnum((unsigned char)*p) && *p != '-' && *p != '_' && *p != '.') return false;
    size_t n = strlen(name);
    return n > 4 && !strcmp(name + n - 4, ".bin");
}
static bool field(const cJSON *root, const char *key, char *out, size_t cap) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsString(v) || !v->valuestring || !*v->valuestring || strlen(v->valuestring) >= cap) return false;
    memcpy(out, v->valuestring, strlen(v->valuestring) + 1);
    return true;
}
esp_err_t pico_release_parse(const char *json, size_t length, pico_release_t *release) {
    if (!json || !release || !length || length > 8192 || memchr(json, 0, length)) return ESP_ERR_INVALID_ARG;
    memset(release, 0, sizeof(*release));
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(json, length, &end, false);
    if (!root || !cJSON_IsObject(root)) { cJSON_Delete(root); return ESP_ERR_INVALID_RESPONSE; }
    // 拒绝重复字段与尾随载荷。/ Reject duplicate fields and trailing payloads.
    bool valid = true;
    for (const cJSON *a = root->child; a; a = a->next)
        for (const cJSON *b = a->next; b; b = b->next)
            if (a->string && b->string && !strcmp(a->string, b->string)) valid = false;
    while (end && end < json + length && isspace((unsigned char)*end)) ++end;
    if (!end || end != json + length) valid = false;
    const cJSON *schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
    const cJSON *size = cJSON_GetObjectItemCaseSensitive(root, "size");
    unsigned version[4], base[4];
    valid = valid && cJSON_IsNumber(schema) && schema->valuedouble == 1 &&
        field(root, "version", release->version, sizeof(release->version)) &&
        field(root, "project", release->project, sizeof(release->project)) &&
        field(root, "board", release->board, sizeof(release->board)) &&
        field(root, "layout", release->layout, sizeof(release->layout)) &&
        field(root, "minimum_base_version", release->minimum_base_version, sizeof(release->minimum_base_version)) &&
        field(root, "url", release->url, sizeof(release->url)) &&
        field(root, "sha256", release->sha256, sizeof(release->sha256)) &&
        field(root, "notes", release->notes, sizeof(release->notes)) &&
        cJSON_IsNumber(size) && size->valuedouble >= 288 && size->valuedouble <= 0x400000 &&
        size->valuedouble == (uint32_t)size->valuedouble &&
        version_parts(release->version, version) && version_parts(release->minimum_base_version, base) &&
        !strcmp(release->board, PICO_OTA_BOARD) && !strcmp(release->layout, PICO_OTA_LAYOUT) &&
        pico_release_url_valid(release->url) && strlen(release->sha256) == 64;
    if (valid) {
        for (unsigned i = 0; i < 64; ++i)
            if (!isxdigit((unsigned char)release->sha256[i])) valid = false;
        release->size = (uint32_t)size->valuedouble;
    }
    cJSON_Delete(root);
    if (!valid) { memset(release, 0, sizeof(*release)); return ESP_ERR_INVALID_RESPONSE; }
    return ESP_OK;
}
