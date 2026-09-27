/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 版本化小端进度记录，以完整路径校验哈希碰撞。
 * Versioned little-endian progress records with full-path collision checks.
 *
 * 冻结：独立 rp_books 命名空间；不擦除分区、不覆盖碰撞书籍。为可靠最近阅读排序，
 * v2 改用持久序号，v1 仍可恢复但排序为 0；遗忘只清指定路径，部分失败允许重试。
 * Frozen: use rp_books; never erase partitions or overwrite collisions. Reliable recent ordering uses
 * persistent v2 sequences; v1 resumes with order zero. Forget only the given path, retrying partial failures.
 */
#include "book_progress.h"
#include "book_store.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nvs.h"

#define PATH_CAP BOOK_STORE_PATH_MAX
#define HEADER_SIZE 24
#define RECORD_CAP (HEADER_SIZE + PATH_CAP)
static const char* TAG = "book_progress";
static const char* NS = "rp_books";

// 列表拓扑仅在 HTTP 停止并 join 后修改；HTTP 只原子标记已有节点。
// Mutate list topology only after HTTP stops and joins; HTTP only atomically marks existing nodes.
struct book_progress_watch {
    char path[PATH_CAP];
    atomic_bool invalidated;
    struct book_progress_watch* next;
};
static book_progress_watch_t* s_watches;

static bool path_valid(const char* path) {
    return path != NULL && path[0] != '\0' && strnlen(path, PATH_CAP) < PATH_CAP;
}

book_progress_watch_t* book_progress_watch_create(const char* path) {
    if (!path_valid(path)) return NULL;
    book_progress_watch_t* watch = malloc(sizeof(*watch));
    if (!watch) return NULL;
    strcpy(watch->path, path);
    atomic_init(&watch->invalidated, false);
    watch->next = s_watches;
    s_watches = watch;
    return watch;
}

bool book_progress_watch_invalidated(const book_progress_watch_t* watch) {
    return watch && atomic_load(&watch->invalidated);
}

void book_progress_watch_destroy(book_progress_watch_t* watch) {
    book_progress_watch_t** p = &s_watches;
    while (*p && *p != watch) p = &(*p)->next;
    if (*p) { *p = watch->next; free(watch); }
}

static void make_key(const char* path, char key[11]) {
    uint32_t hash = UINT32_C(2166136261);
    for (const unsigned char* p = (const unsigned char*)path; *p; ++p) {
        hash = (hash ^ *p) * UINT32_C(16777619);
    }
    snprintf(key, 11, "b_%08" PRIx32, hash);
}

static uint32_t get32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put32(uint8_t* p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8 * i));
}

static bool record_valid(const uint8_t* data, size_t len) {
    if (len < HEADER_SIZE + 2 || len > RECORD_CAP || memcmp(data, "RPB", 3) != 0 ||
        (data[3] != 1 && data[3] != 2)) return false;
    size_t path_len = len - HEADER_SIZE;
    if (data[3] == 1 && (path_len > 160 || data[23] != 0)) return false;
    return ((size_t)data[22] | ((size_t)data[23] << 8)) == path_len &&
        strnlen((const char*)data + HEADER_SIZE, path_len) == path_len - 1;
}

static esp_err_t warn_error(esp_err_t err) {
    if (err != ESP_OK) ESP_LOGW(TAG, "progress persistence: %s", esp_err_to_name(err));
    return err;
}

bool book_progress_load(const char* path, uint32_t size, book_progress_t* out) {
    if (!path_valid(path) || out == NULL) return false;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    char key[11];
    make_key(path, key);
    uint8_t data[RECORD_CAP];
    size_t len = sizeof(data);
    esp_err_t err = nvs_get_blob(h, key, data, &len);
    nvs_close(h);
    if (err != ESP_OK || !record_valid(data, len) ||
        strcmp((const char*)data + HEADER_SIZE, path) != 0 || get32(data + 4) != size ||
        data[14] < 36 || data[14] > 72 || (data[14] - 36) % 4 != 0 || data[15] > 100) return false;
    *out = (book_progress_t){
        .file_size = get32(data + 4), .chapter = (uint16_t)(data[8] | (data[9] << 8)),
        .byte_off = get32(data + 10), .px = data[14], .pct = data[15],
        .last_open_s = data[3] == 2 ? get32(data + 16) : 0,
    };
    return true;
}

// 旧记录必须可识别且属于同一路径；损坏或未知版本也不盲目覆盖。
// Existing records must be recognized and owned by this path; preserve corrupt or unknown versions too.
static esp_err_t check_owner(nvs_handle_t h, const char* key, const char* path) {
    uint8_t data[RECORD_CAP];
    size_t len = sizeof(data);
    esp_err_t err = nvs_get_blob(h, key, data, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    return record_valid(data, len) && strcmp((const char*)data + HEADER_SIZE, path) == 0
        ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t book_progress_save(const char* path, const book_progress_t* progress) {
    if (!path_valid(path) || progress == NULL || progress->px < 36 || progress->px > 72 ||
        (progress->px - 36) % 4 != 0 || progress->pct > 100) return ESP_ERR_INVALID_ARG;
    uint8_t data[RECORD_CAP] = {'R', 'P', 'B', 2};
    put32(data + 4, progress->file_size);
    data[8] = (uint8_t)progress->chapter;
    data[9] = (uint8_t)(progress->chapter >> 8);
    put32(data + 10, progress->byte_off);
    data[14] = progress->px;
    data[15] = progress->pct;
    size_t path_len = strlen(path) + 1;
    data[22] = (uint8_t)path_len;
    data[23] = (uint8_t)(path_len >> 8);
    memcpy(data + HEADER_SIZE, path, path_len);
    char key[11];
    make_key(path, key);
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return warn_error(err);
    err = check_owner(h, key, path);
    uint32_t sequence = 0;
    if (err == ESP_OK) {
        err = nvs_get_u32(h, "seq", &sequence);
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    }
    if (err == ESP_OK && sequence == UINT32_MAX) err = ESP_ERR_INVALID_STATE;
    // 先提交序号预留，再写进度；失败可留空洞，但重启后不能倒序。
    // Commit the sequence reservation before progress; failures may leave gaps, never reverse order after restart.
    if (err == ESP_OK) err = nvs_set_u32(h, "seq", ++sequence);
    if (err == ESP_OK) err = nvs_commit(h);
    put32(data + 16, sequence);
    if (err == ESP_OK) err = nvs_set_blob(h, key, data, HEADER_SIZE + path_len);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return warn_error(err);
}

esp_err_t book_progress_clear(const char* path) {
    if (!path_valid(path)) return ESP_ERR_INVALID_ARG;
    char key[11];
    make_key(path, key);
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return warn_error(err);
    err = check_owner(h, key, path);
    if (err == ESP_OK) err = nvs_erase_key(h, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return warn_error(err);
}

esp_err_t book_progress_forget(const char* path) {
    if (!path_valid(path)) return ESP_ERR_INVALID_ARG;
    for (book_progress_watch_t* watch = s_watches; watch; watch = watch->next)
        if (!strcmp(watch->path, path)) atomic_store(&watch->invalidated, true);
    char key[11];
    make_key(path, key);
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return warn_error(err);
    err = check_owner(h, key, path);
    char last[PATH_CAP];
    size_t len = sizeof(last);
    bool matching_last = false;
    if (err == ESP_OK) {
        err = nvs_get_str(h, "last", last, &len);
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
        else if (err == ESP_OK) matching_last = strcmp(last, path) == 0;
    }
    // 先移除自动恢复入口；失败不继续清进度，重试也不会波及其他书。
    // Remove automatic resume first; on failure retain progress, and retries never touch other books.
    if (err == ESP_OK && matching_last) {
        err = nvs_erase_key(h, "last");
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
        if (err == ESP_OK) err = nvs_commit(h);
    }
    if (err == ESP_OK) err = nvs_erase_key(h, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return warn_error(err);
}

bool book_progress_last_path(char* out, size_t cap) {
    if (out == NULL || cap == 0) return false;
    out[0] = '\0';
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = cap;
    esp_err_t err = nvs_get_str(h, "last", out, &len);
    nvs_close(h);
    if (err != ESP_OK || !path_valid(out)) {
        out[0] = '\0';
        return false;
    }
    return true;
}

esp_err_t book_progress_set_last_path(const char* path) {
    if (path == NULL || strnlen(path, PATH_CAP) >= PATH_CAP) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return warn_error(err);
    err = nvs_set_str(h, "last", path);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return warn_error(err);
}
