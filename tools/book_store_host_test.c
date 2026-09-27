/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 挂载与目录错误策略测试，不访问真实磁盘。
 * Mount and directory-error policy tests without real disk access.
 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "book_store.h"
#include "read_pico_sd.h"
#include "esp_vfs_fat.h"

static bool sd_mounted;
static bool sd_invalid_present;
static int mount_calls, mount_error, mkdir_error, is_directory = 1, fat_error;
static int sd_mkdir_error;
static int file_mode = S_IFREG, file_error, unlink_error, forget_error, unlinks, forgets;
static bool sd_exists = true, flash_exists = true;
static bool backup_exists;
static int backup_mode = S_IFREG, backup_unlink_error, rename_error;
static char forgotten[BOOK_STORE_PATH_MAX];
static int mock_mkdir(const char* path, mode_t mode) {
    (void)mode;
    errno = sd_mkdir_error && !strncmp(path, "/sdcard/", 8) ? sd_mkdir_error : mkdir_error;
    return errno ? -1 : 0;
}
static int mock_stat(const char* path, struct stat* out) {
    if (!strcmp(path, "/sdcard/books") || !strcmp(path, "/flash/books")) {
        out->st_mode = is_directory ? S_IFDIR : S_IFREG; return 0;
    }
    if (strstr(path, ".rename-backup")) {
        if (!backup_exists) { errno = ENOENT; return -1; }
        out->st_mode = backup_mode; return 0;
    }
    if (file_error) { errno = file_error; return -1; }
    bool exists = !strncmp(path, "/sdcard/", 8) ? sd_exists : flash_exists;
    if (!exists) { errno = ENOENT; return -1; }
    out->st_mode = file_mode; return 0;
}
static int mock_unlink(const char* path) {
    ++unlinks;
    if (strstr(path, ".rename-backup")) {
        if (backup_unlink_error) { errno = backup_unlink_error; return -1; }
        backup_exists = false; return 0;
    }
    if (unlink_error) { errno = unlink_error; return -1; }
    if (!strncmp(path, "/sdcard/", 8)) sd_exists = false; else flash_exists = false;
    return 0;
}
static int mock_rename(const char* from, const char* to) {
    assert(strstr(from, ".rename-backup") && !strstr(to, ".rename-backup"));
    if (rename_error) { errno = rename_error; return -1; }
    assert(backup_exists); backup_exists = false; sd_exists = true; return 0;
}
esp_err_t book_progress_forget(const char* path) {
    ++forgets; strcpy(forgotten, path); return forget_error;
}
#define mkdir mock_mkdir
#define stat(path, out) mock_stat(path, out)
#define lstat(path, out) mock_stat(path, out)
#define unlink mock_unlink
#define rename mock_rename
#include "../main/book/book_store.c"
#undef mkdir
#undef stat
#undef lstat
#undef unlink
#undef rename

esp_err_t read_pico_sd_get_info(read_pico_sd_info_t* out) {
    memset(out, 0, sizeof(*out)); out->mounted = sd_mounted;
    out->present = sd_mounted || sd_invalid_present; return ESP_OK;
}
esp_err_t esp_vfs_fat_spiflash_mount_rw_wl(const char* path, const char* label,
    const esp_vfs_fat_mount_config_t* config, wl_handle_t* out) {
    assert(!strcmp(path, "/flash") && !strcmp(label, "storage"));
    assert(config->format_if_mount_failed && config->max_files == 4 && config->allocation_unit_size == 4096);
    ++mount_calls; if (!mount_error) *out = 1; return mount_error;
}
esp_err_t esp_vfs_fat_info(const char* path, uint64_t* total, uint64_t* free_bytes) {
    assert(!strcmp(path, "/flash") || !strcmp(path, "/sdcard"));
    *total = 9999; *free_bytes = 1234; return fat_error;
}

int main(void) {
    book_store_root_t roots[2], upload;
    int n = -1;
    assert(!book_store_flash_ready());
    assert(book_store_roots(NULL, &n) == ESP_ERR_INVALID_ARG);
    mount_error = ESP_FAIL;
    assert(book_store_roots(roots, &n) == ESP_FAIL && n == 0);
    assert(book_store_roots_degraded());
    sd_mounted = true;
    assert(book_store_roots(roots, &n) == ESP_OK && n == 1 && !roots[0].is_flash);
    assert(book_store_roots_degraded());
    assert(book_store_upload_root(&upload) == ESP_OK && !upload.is_flash);
    mount_error = 0;
    assert(book_store_roots(roots, &n) == ESP_OK && n == 2);
    assert(!book_store_roots_degraded());
    assert(!roots[0].is_flash && roots[1].is_flash && book_store_flash_ready());
    int calls = mount_calls;
    assert(book_store_roots(roots, &n) == ESP_OK && mount_calls == calls);
    sd_mkdir_error = EACCES;
    assert(book_store_roots(roots, &n) == ESP_OK && n == 1 && roots[0].is_flash && book_store_roots_degraded());
    sd_mkdir_error = 0;
    assert(book_store_roots(roots, &n) == ESP_OK && n == 2 && !book_store_roots_degraded());
    assert(book_store_file_limit(&roots[0]) == SIZE_MAX);
    assert(book_store_file_limit(&roots[1]) == 1024 * 1024);
    assert(book_store_free_bytes(&roots[1]) == 1234);
    fat_error = ESP_FAIL; assert(book_store_free_bytes(&roots[0]) == 0);
    mkdir_error = EEXIST;
    assert(book_store_roots(roots, &n) == ESP_OK && n == 2);
    is_directory = 0;
    assert(book_store_roots(roots, &n) == ESP_FAIL && n == 0);
    assert(book_store_upload_root(&upload) == ESP_FAIL);
    mkdir_error = EACCES;
    assert(book_store_upload_root(&upload) == ESP_FAIL);
    mkdir_error = 0; sd_mounted = false;
    assert(book_store_roots(roots, &n) == ESP_OK && n == 1 && roots[0].is_flash);
    assert(!book_store_roots_degraded());
    assert(book_store_upload_root(&upload) == ESP_OK && upload.is_flash);
    sd_invalid_present = true;
    assert(book_store_upload_root(&upload) == ESP_ERR_INVALID_STATE && !upload.path[0]);
    assert(book_store_roots(roots, &n) == ESP_OK && n == 1 && book_store_roots_degraded());
    book_store_root_t removed_root = {.path = "/sdcard/books", .is_flash = false};
    assert(book_store_free_bytes(&removed_root) == 0);
    sd_invalid_present = false;
    // 取消由调用方不调用删除实现；以下计数从零开始。/ Cancellation is represented by no delete call; counters start at zero.
    assert(!unlinks && !forgets);
    sd_mounted = true; is_directory = 1;
    const char* path = "/sdcard/books/同名.TXT";
    bool removed = true;
    unsigned revision = book_store_revision();
    const char* invalid[] = {"/sdcard/books/../escape.txt", "/sdcard/books2/x.txt", "/sdcard/books/sub/x.txt",
        "/flash/books/..\\x.epub", "/flash/books/x.bin", "/sdcard/books/", "/other/books/x.txt"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
        assert(book_store_delete(invalid[i], &removed) != ESP_OK && !removed);
    }
    file_mode = S_IFDIR;
    assert(book_store_delete(path, &removed) != ESP_OK && !removed);
    file_mode = S_IFLNK;
    assert(book_store_delete(path, &removed) != ESP_OK && !removed);
    file_mode = S_IFREG; file_error = EACCES;
    assert(book_store_delete(path, &removed) != ESP_OK && !removed);
    file_error = 0; unlink_error = EACCES;
    assert(book_store_delete(path, &removed) != ESP_OK && !removed && !forgets && sd_exists);
    unlink_error = 0; forget_error = ESP_FAIL;
    assert(book_store_delete(path, &removed) == ESP_FAIL && removed && !sd_exists && flash_exists);
    assert(forgets == 1 && !strcmp(forgotten, path));
    int prior_unlinks = unlinks;
    forget_error = 0;
    assert(book_store_delete(path, &removed) == ESP_OK && removed && unlinks == prior_unlinks && forgets == 2);
    assert(book_store_delete("/flash/books/同名.epub", &removed) == ESP_OK && removed && !flash_exists);
    assert(!strcmp(forgotten, "/flash/books/同名.epub"));
    assert(book_store_revision() == revision);
    // 模拟下次启动恢复条件：删除成功不得留可复活的备份。/ Successful deletion must leave no backup eligible for startup restoration.
    sd_exists = backup_exists = true;
    backup_unlink_error = EACCES;
    int previous_forgets = forgets;
    assert(book_store_delete(path, &removed) == ESP_FAIL && !removed && sd_exists && backup_exists && forgets == previous_forgets);
    backup_unlink_error = 0;
    assert(book_store_delete(path, &removed) == ESP_OK && removed && !sd_exists && !backup_exists);
    backup_exists = true; rename_error = EACCES;
    assert(book_store_delete(path, &removed) == ESP_FAIL && !removed && !sd_exists && backup_exists);
    rename_error = 0; unlink_error = EACCES;
    assert(book_store_delete(path, &removed) == ESP_FAIL && !removed && sd_exists && !backup_exists);
    unlink_error = 0;
    assert(book_store_delete(path, &removed) == ESP_OK && removed && !sd_exists && !backup_exists);
    backup_exists = true;
    assert(book_store_delete(path, &removed) == ESP_OK && removed && !sd_exists && !backup_exists);
    sd_exists = backup_exists = true; backup_mode = S_IFLNK;
    assert(book_store_delete(path, &removed) == ESP_ERR_INVALID_ARG && !removed && sd_exists && backup_exists);
    backup_mode = S_IFDIR;
    assert(book_store_delete(path, &removed) == ESP_ERR_INVALID_ARG && !removed && sd_exists && backup_exists);
    backup_exists = false; backup_mode = S_IFREG;
    char long_path[BOOK_STORE_PATH_MAX];
    strcpy(long_path, "/sdcard/books/");
    memset(long_path + 14, 'x', 251);
    strcpy(long_path + 265, ".txt");
    sd_exists = true;
    assert(book_store_delete(long_path, &removed) == ESP_OK && removed && !strcmp(forgotten, long_path));
    memset(long_path + 14, 'x', 252);
    strcpy(long_path + 266, ".txt");
    assert(book_store_delete(long_path, &removed) == ESP_ERR_INVALID_ARG && !removed);
    sd_mounted = false;
    assert(book_store_delete(path, &removed) == ESP_ERR_INVALID_STATE && !removed);
    s_wl = WL_INVALID_HANDLE;
    assert(book_store_delete("/flash/books/同名.epub", &removed) == ESP_ERR_INVALID_STATE && !removed);
    assert(book_store_delete(NULL, &removed) == ESP_ERR_INVALID_ARG && !removed);
    assert(book_store_delete(path, NULL) == ESP_ERR_INVALID_ARG);
    puts("book store: mount fallback, precedence, capacity, lazy init and directory errors passed");
    puts("book delete: path bounds, symlinks, unlink failure, partial cleanup, retry and root isolation passed");
}
