/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：书名覆盖独立存于 NVS，完整路径防止哈希冲突误写。
 * English: Title overrides live in NVS; full paths reject hash collisions.
 */
#include "book_title.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "nvs.h"

#define TITLE_PATH_MAX 288
#define TITLE_MAX 120
#define RECORD_MAX (TITLE_PATH_MAX + TITLE_MAX + 8)
static const char *NS = "rp_titles";

bool book_title_from_path(const char *path, char *out, size_t cap) {
    if (!path || !out || cap < 2) return false;
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    size_t len = strlen(base);
    const char *dot = strrchr(base, '.');
    if (dot && dot != base) len = (size_t)(dot - base);
    if (!len) return false;
    if (len >= cap) {
        len = cap - 1;
        while (len && ((unsigned char)base[len] & 0xc0) == 0x80) --len;
    }
    memcpy(out, base, len);
    out[len] = 0;
    return len > 0;
}

void book_title_clean_import(char *title) {
    if (!title) return;
    size_t n = strlen(title);
    while (n && (title[n - 1] == ' ' || title[n - 1] == '\t')) title[--n] = 0;
    // 个别 EPUB 转换器把元数据属性片段拼进书名，当前样本的尾巴是“=This”。
    // Some EPUB converters append an attribute fragment; the observed trailing fragment is “=This”.
    if (n >= 5 && !strcasecmp(title + n - 5, "=This")) {
        title[n - 5] = 0;
        n -= 5;
        while (n && (title[n - 1] == ' ' || title[n - 1] == '\t')) title[--n] = 0;
    }
}

static bool valid_utf8(const char *s, size_t max) {
    size_t n = strnlen(s, max + 1);
    if (!n || n > max || s[0] == ' ' || s[n - 1] == ' ') return false;
    for (size_t i = 0; i < n;) {
        uint32_t cp; unsigned more; unsigned char c = (unsigned char)s[i++];
        if (c < 32 || c == 127) return false;
        if (c < 128) continue;
        if (c >= 0xc2 && c <= 0xdf) { cp = c & 31; more = 1; }
        else if (c >= 0xe0 && c <= 0xef) { cp = c & 15; more = 2; }
        else if (c >= 0xf0 && c <= 0xf4) { cp = c & 7; more = 3; }
        else return false;
        unsigned width = more;
        while (more--) {
            if (i == n || ((unsigned char)s[i] & 0xc0) != 0x80) return false;
            cp = (cp << 6) | ((unsigned char)s[i++] & 63);
        }
        if ((width == 2 && cp < 0x800) || (width == 3 && cp < 0x10000) ||
            (cp >= 0xd800 && cp <= 0xdfff) || cp > 0x10ffff) return false;
    }
    return true;
}
static bool path_ok(const char *path) { return path && path[0] && strnlen(path, TITLE_PATH_MAX) < TITLE_PATH_MAX; }
static void key_for(const char *path, char key[11]) {
    uint32_t hash = UINT32_C(2166136261);
    for (const unsigned char *p = (const unsigned char *)path; *p; ++p) hash = (hash ^ *p) * UINT32_C(16777619);
    snprintf(key, 11, "t_%08" PRIx32, hash);
}
static esp_err_t get_record(nvs_handle_t h, const char *key, const char *path, char data[RECORD_MAX], size_t *len) {
    *len = RECORD_MAX;
    esp_err_t err = nvs_get_blob(h, key, data, len);
    if (err != ESP_OK) return err;
    size_t p = strlen(path) + 1;
    if (*len < p + 2 || *len > RECORD_MAX || memcmp(data, path, p) ||
        strnlen(data + p, *len - p) != *len - p - 1) return ESP_ERR_INVALID_STATE;
    return ESP_OK;
}
bool book_title_get(const char *path, char *out, size_t cap) {
    if (!path_ok(path) || !out || !cap) return false;
    out[0] = 0;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    char key[11], data[RECORD_MAX]; size_t len;
    key_for(path, key);
    esp_err_t err = get_record(h, key, path, data, &len);
    nvs_close(h);
    if (err != ESP_OK) return false;
    const char *title = data + strlen(path) + 1;
    if (strlen(title) >= cap) return false;
    strcpy(out, title);
    return true;
}
esp_err_t book_title_set(const char *path, const char *title) {
    if (!path_ok(path) || !title || !valid_utf8(title, TITLE_MAX)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    char key[11], old[RECORD_MAX]; size_t len;
    key_for(path, key);
    err = get_record(h, key, path, old, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) {
        char data[RECORD_MAX];
        size_t p = strlen(path) + 1, t = strlen(title) + 1;
        memcpy(data, path, p); memcpy(data + p, title, t);
        err = nvs_set_blob(h, key, data, p + t);
        if (err == ESP_OK) err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}
esp_err_t book_title_clear(const char *path) {
    if (!path_ok(path)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h; esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    char key[11], old[RECORD_MAX]; size_t len;
    key_for(path, key);
    err = get_record(h, key, path, old, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    else if (err == ESP_OK) { err = nvs_erase_key(h, key); if (err == ESP_OK) err = nvs_commit(h); }
    nvs_close(h);
    return err;
}
