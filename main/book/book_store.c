/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 书目录与 FAT 容量查询；调用方负责先显示首次初始化提示。
 * Book directories and FAT capacity queries; callers show initialization feedback first.
 *
 * 冻结：不格式化 SD；内置 storage 分区按需挂载，失败不影响可用 SD。
 * Frozen: never format SD; mount internal storage on demand without blocking usable SD on failure.
 * 冻结：有卡但挂载失效时不把上传静默切换到内置存储；拔卡不复用旧容量。
 * Frozen: present but invalid media must not silently redirect uploads to flash; never reuse removed-card capacity.
 * 冻结：用户确认后仅删已挂载根内的直接普通书籍；文件与进度部分失败必须返回，不通知 UI 版本。
 * Frozen: after user confirmation, delete only direct regular books in mounted roots; report partial failures without notifying UI revision.
 * 冻结：删除前清除同名普通覆盖备份；仅有备份时先恢复，失败保留可恢复的原书。
 * Frozen: clear a matching regular replacement backup before deletion; restore backup-only books first and preserve recovery on failure.
 */
#include "book_store.h"
#include "book_progress.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "read_pico_sd.h"
#include "wear_levelling.h"

static const char* TAG = "book_store";
static wl_handle_t s_wl = WL_INVALID_HANDLE;
static unsigned s_revision;
static bool s_roots_degraded;

void book_store_notify_changed(void) { ++s_revision; }
unsigned book_store_revision(void) { return s_revision; }
bool book_store_roots_degraded(void) { return s_roots_degraded; }

static esp_err_t ensure_dir(const char* path) {
    if (mkdir(path, 0777) == 0) return ESP_OK;
    if (errno == EEXIST) {
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return ESP_OK;
    }
    ESP_LOGW(TAG, "book directory unavailable: %s (%d)", path, errno);
    return ESP_FAIL;
}

static bool sd_ready(void) {
    read_pico_sd_info_t info = {0};
    read_pico_sd_get_info(&info);
    return info.present && info.mounted;
}

static esp_err_t flash_root(book_store_root_t* out) {
    if (!book_store_flash_ready()) {
        const esp_vfs_fat_mount_config_t config = {
            .format_if_mount_failed = true,
            .max_files = 4,
            .allocation_unit_size = 4096,
        };
        esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl("/flash", "storage", &config, &s_wl);
        if (err != ESP_OK) {
            s_wl = WL_INVALID_HANDLE;
            ESP_LOGW(TAG, "flash mount: %s", esp_err_to_name(err));
            return err;
        }
    }
    esp_err_t err = ensure_dir("/flash/books");
    if (err == ESP_OK) *out = (book_store_root_t){.path = "/flash/books", .is_flash = true};
    return err;
}

esp_err_t book_store_roots(book_store_root_t out[2], int* n) {
    if (out == NULL || n == NULL) return ESP_ERR_INVALID_ARG;
    *n = 0;
    s_roots_degraded = false;
    memset(out, 0, sizeof(*out) * 2);
    if (sd_ready()) {
        if (ensure_dir("/sdcard/books") == ESP_OK)
            out[(*n)++] = (book_store_root_t){.path = "/sdcard/books", .is_flash = false};
        else s_roots_degraded = true;
    } else {
        read_pico_sd_info_t info = {0};
        read_pico_sd_get_info(&info);
        if (info.present) s_roots_degraded = true;
    }
    esp_err_t err = flash_root(&out[*n]);
    if (err == ESP_OK) ++*n;
    else s_roots_degraded = true;
    return *n > 0 ? ESP_OK : err;
}

esp_err_t book_store_upload_root(book_store_root_t* out) {
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    if (!sd_ready()) {
        read_pico_sd_info_t info = {0};
        read_pico_sd_get_info(&info);
        // 有卡但挂载失效时让用户显式重挂，不能悄悄改写内置目录。
        // Present but invalid media requires an explicit remount, not silent flash redirection.
        if (info.present) return ESP_ERR_INVALID_STATE;
        return flash_root(out);
    }
    esp_err_t err = ensure_dir("/sdcard/books");
    if (err == ESP_OK) *out = (book_store_root_t){.path = "/sdcard/books", .is_flash = false};
    return err;
}

size_t book_store_file_limit(const book_store_root_t* root) {
    if (root == NULL) return 0;
    return root->is_flash ? BOOK_STORE_FLASH_FILE_MAX : SIZE_MAX;
}

uint64_t book_store_free_bytes(const book_store_root_t* root) {
    if (root == NULL) return 0;
    if (!root->is_flash && !sd_ready()) return 0;
    uint64_t total = 0, free_bytes = 0;
    esp_err_t err = esp_vfs_fat_info(root->is_flash ? "/flash" : "/sdcard", &total, &free_bytes);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "FAT capacity: %s", esp_err_to_name(err));
        return 0;
    }
    return free_bytes;
}

bool book_store_flash_ready(void) {
    return s_wl != WL_INVALID_HANDLE;
}

esp_err_t book_store_delete(const char* path, bool* removed) {
    if (!removed) return ESP_ERR_INVALID_ARG;
    *removed = false;
    if (!path || strnlen(path, BOOK_STORE_PATH_MAX) >= BOOK_STORE_PATH_MAX) return ESP_ERR_INVALID_ARG;
    const char* root;
    const char* name;
    if (!strncmp(path, "/sdcard/books/", 14)) {
        root = "/sdcard/books"; name = path + 14;
        if (!sd_ready()) return ESP_ERR_INVALID_STATE;
    } else if (!strncmp(path, "/flash/books/", 13)) {
        root = "/flash/books"; name = path + 13;
        if (!book_store_flash_ready()) return ESP_ERR_INVALID_STATE;
    } else return ESP_ERR_INVALID_ARG;
    size_t len = strlen(name);
    if (!len || len > 255) return ESP_ERR_INVALID_ARG;
    for (const unsigned char* p = (const unsigned char*)name; *p; ++p) {
        if (*p < 32 || *p == 127 || *p == '/' || *p == '\\' || *p == ':') return ESP_ERR_INVALID_ARG;
    }
    const char* ext = strrchr(name, '.');
    if (!ext || (strcasecmp(ext, ".txt") && strcasecmp(ext, ".epub"))) return ESP_ERR_INVALID_ARG;
    struct stat st;
    if (stat(root, &st) != 0 || !S_ISDIR(st.st_mode)) return ESP_ERR_INVALID_STATE;
    // FAT 没有符号链接；主机测试用 lstat，确保不会跟随主机链接删其他文件。
    // FAT has no symlinks; host builds use lstat to reject host links rather than follow them.
#ifdef ESP_PLATFORM
    int result = stat(path, &st);
#else
    int result = lstat(path, &st);
#endif
    bool exists = result == 0;
    if (!exists && errno != ENOENT) return ESP_FAIL;
    if (exists && !S_ISREG(st.st_mode)) return ESP_ERR_INVALID_ARG;
    // 显式删除也处理覆盖备份，避免下次传书启动恢复已删图书。
    // Explicit deletion handles replacement backups so transfer startup cannot resurrect deleted books.
    const char suffix[] = ".rename-backup";
    if (len + sizeof(suffix) - 1 <= 255) {
        char backup[BOOK_STORE_PATH_MAX + sizeof(suffix)];
        snprintf(backup, sizeof(backup), "%s%s", path, suffix);
#ifdef ESP_PLATFORM
        result = stat(backup, &st);
#else
        result = lstat(backup, &st);
#endif
        if (result != 0) {
            if (errno != ENOENT) return ESP_FAIL;
        } else {
            if (!S_ISREG(st.st_mode)) return ESP_ERR_INVALID_ARG;
            if (exists) {
                if (unlink(backup) != 0 && errno != ENOENT) return ESP_FAIL;
            } else {
                // 未完成覆盖先恢复旧书；后续删除失败时仍保留可见原书。
                // Restore an interrupted replacement first, keeping the old book visible if deletion fails.
                if (rename(backup, path) != 0) return ESP_FAIL;
                exists = true;
            }
        }
    }
    if (exists) {
        if (unlink(path) != 0 && errno != ENOENT) return ESP_FAIL;
    }
    *removed = true;
    return book_progress_forget(path);
}
