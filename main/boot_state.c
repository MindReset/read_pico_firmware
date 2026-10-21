/* SPDX-License-Identifier: Apache-2.0
 * 中文：固定大小且校验的 NVS 启动记录。开书中断后保留原进度，只屏蔽自动重试。
 * English: Fixed-size checked NVS startup state. Interrupted book opening retains progress and blocks automatic retries only.
 * 冻结：不擦除分区、不动态分配、不逐页写 Flash；恢复入口须先成功消费，资源失败回可操作页面。
 * Frozen: No partition erasure, allocations or per-page Flash writes. Consume resume successfully before using it; failed resources fall back to an operable page.
 */
#include "boot_state.h"
#include <stddef.h>
#include <string.h>
#include "esp_log.h"
#include "nvs.h"

#define BOOT_MAGIC UINT32_C(0x50425331)
typedef struct {
    uint32_t magic;
    uint8_t starting, resume_valid, reserved[2];
    pico_resume_t resume;
    char opening[PICO_BOOT_PATH_MAX], blocked[PICO_BOOT_PATH_MAX];
    uint32_t checksum;
} boot_record_t;
static boot_record_t s_record;
static pico_resume_t s_resume;
static bool s_recovery, s_resume_pending, s_writable, s_interrupted;

static uint32_t checksum(const boot_record_t *r) {
    const uint8_t *p = (const uint8_t *)r;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < offsetof(boot_record_t, checksum); ++i) hash = (hash ^ p[i]) * 16777619u;
    return hash;
}
static bool path_valid(const char *path, bool empty) {
    size_t n = strnlen(path, PICO_BOOT_PATH_MAX);
    if (!n) return empty;
    if (n == PICO_BOOT_PATH_MAX || (strncmp(path, "/sdcard/", 8) && strncmp(path, "/flash/books/", 13))) return false;
    for (const char *p = path + 1; *p;) {
        const char *end = strchr(p, '/');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (!len || (len == 1 && p[0] == '.') || (len == 2 && p[0] == '.' && p[1] == '.')) return false;
        for (size_t i = 0; i < len; ++i) if ((unsigned char)p[i] < 32 || p[i] == '\\') return false;
        if (!end) break;
        p = end + 1;
        if (!*p) return false;
    }
    return true;
}
static bool resume_valid(const pico_resume_t *r) {
    return r && r->tab < 4 && r->reader <= 1 && r->fullscreen <= 1 &&
        (!r->reader || r->tab == 1) && path_valid(r->path, !r->reader);
}
static esp_err_t store(void) {
    if (!s_writable) return ESP_ERR_INVALID_STATE;
    s_record.checksum = checksum(&s_record);
    nvs_handle_t h;
    esp_err_t err = nvs_open("pico_startup", NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, "state", &s_record, sizeof(s_record));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) ESP_LOGW("boot_state", "checkpoint: %s", esp_err_to_name(err));
    return err;
}
void pico_boot_init(bool abnormal_reset) {
    memset(&s_record, 0, sizeof(s_record));
    s_resume_pending = false;
    s_interrupted = false;
    s_recovery = abnormal_reset;
    nvs_handle_t h;
    esp_err_t err = nvs_open("pico_startup", NVS_READWRITE, &h);
    s_writable = err == ESP_OK;
    if (s_writable) {
        size_t size = sizeof(s_record);
        err = nvs_get_blob(h, "state", &s_record, &size);
        nvs_close(h);
        bool valid = err == ESP_OK && size == sizeof(s_record) && s_record.magic == BOOT_MAGIC &&
            s_record.starting <= 1 && s_record.resume_valid <= 1 &&
            resume_valid(&s_record.resume) && path_valid(s_record.opening, true) &&
            path_valid(s_record.blocked, true) && s_record.checksum == checksum(&s_record);
        if (!valid) {
            if (err != ESP_ERR_NVS_NOT_FOUND) s_recovery = true;
            memset(&s_record, 0, sizeof(s_record));
        }
    } else s_recovery = true;
    if (s_record.starting || s_record.opening[0]) s_recovery = true;
    if (s_record.opening[0]) {
        s_interrupted = true;
        memcpy(s_record.blocked, s_record.opening, sizeof(s_record.blocked));
        s_record.opening[0] = 0;
    }
    if (s_record.resume_valid && !s_recovery) {
        s_resume = s_record.resume;
        s_resume_pending = true;
    }
    s_record.magic = BOOT_MAGIC;
    s_record.starting = 1;
    s_record.resume_valid = 0;
    if (store() != ESP_OK) { s_resume_pending = false; s_recovery = true; }
    ESP_LOGI("boot_state", "startup recovery=%d resume=%d", s_recovery, s_resume_pending);
}
void pico_boot_ready(void) {
    s_record.starting = 0;
    (void)store();
}
bool pico_boot_recovery(void) { return s_recovery; }
bool pico_boot_asset_allowed(const char *path) {
    return path && !s_recovery && (!s_record.blocked[0] || strcmp(path, s_record.blocked));
}
esp_err_t pico_boot_book_begin(const char *path) {
    if (!path || !path_valid(path, false)) return ESP_ERR_INVALID_ARG;
    memset(s_record.opening, 0, sizeof(s_record.opening));
    memcpy(s_record.opening, path, strlen(path));
    s_record.resume_valid = 0;
    return store();
}
void pico_boot_book_end(bool success) {
    if (success && !strcmp(s_record.blocked, s_record.opening)) s_record.blocked[0] = 0;
    s_record.opening[0] = 0;
    (void)store();
}
esp_err_t pico_boot_save_resume(const pico_resume_t *resume) {
    if (!resume_valid(resume)) return ESP_ERR_INVALID_ARG;
    memset(&s_record.resume, 0, sizeof(s_record.resume));
    s_record.resume.tab = resume->tab;
    s_record.resume.reader = resume->reader;
    s_record.resume.fullscreen = resume->fullscreen;
    memcpy(s_record.resume.path, resume->path, strlen(resume->path));
    s_record.resume_valid = 1;
    return store();
}
void pico_boot_hold_resume(void) {
    if (s_resume_pending) (void)pico_boot_save_resume(&s_resume);
}
bool pico_boot_take_resume(pico_resume_t *resume) {
    if (!resume || !s_resume_pending) return false;
    // 密码等待可能再次持久化；先消费再允许恢复，写入失败不能重试开书。
    // Authentication may have re-persisted it; consume before admission and never retry after a write failure.
    if (s_record.resume_valid) {
        s_record.resume_valid = 0;
        if (store() != ESP_OK) { s_resume_pending = false; return false; }
    }
    *resume = s_resume;
    s_resume_pending = false;
    return true;
}
void pico_boot_clear_resume(void) {
    s_resume_pending = false;
    if (!s_record.resume_valid) return;
    s_record.resume_valid = 0;
    (void)store();
}
bool pico_boot_interrupted_book(char *path, size_t capacity) {
    if (!s_interrupted || !path || strlen(s_record.blocked) >= capacity) return false;
    memcpy(path, s_record.blocked, strlen(s_record.blocked) + 1);
    return true;
}
void pico_boot_forget_interrupted_book(void) {
    s_interrupted = false;
    s_record.blocked[0] = 0;
    (void)store();
}
