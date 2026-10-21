/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 热点与已有WiFi、HTTP接收和临时文件提交；容量策略由页面注入。
 * AP/STA networking, HTTP reception and file commit; page injects capacity policy.
 * 冻结：分类上传限制书籍/图片/字体格式；目录中的「添加文件」接受任意格式，保留 UTF-8 名字并先验容量。
 * Frozen: Typed uploads limit book/image/font formats; Add File accepts any format within the TF root, preserving UTF-8 names and checking capacity first.
 * 为传入升级包等其他文件增加目录上传；分类上传仍保持原有限额和失败清理。
 * Directory uploads also accept upgrade packages; typed uploads retain their limits and failed-part cleanup.
 * 冻结：热点网页或停服设备触屏可配置网络，不自动切模式；凭据只存单个NVS blob，状态不含密码。
 * 冻结：显式对时优先复用已连接的 STA；停服时才短暂连接，用毕释放；凭据只保存在原 NVS blob。
 * Frozen: Explicit time sync reuses connected STA; only a stopped service connects briefly and releases WiFi; credentials stay in the original NVS blob.
 * 已连接后仍启动临时 WiFi 会返回状态错误，因此改为按连接状态分流。
 * Starting temporary WiFi while already connected returned invalid state, so sync now routes by connection state.
 */
#include "read_pico_search.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <dirent.h>
#include <unistd.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

#define IMAGE_UPLOAD_LIMIT (20u * 1024u * 1024u)
#define FONT_UPLOAD_LIMIT (32u * 1024u * 1024u)

/* ---- 请求与存储 / Requests and storage ---- */
static int hex_value(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool decode_name_limit(const char *encoded, char *out, size_t cap, unsigned kind) {
    if (!cap) return false;
    size_t n = 0;
    while (*encoded) {
        unsigned char c = (unsigned char)*encoded++;
        if (c == '%') {
            if (!encoded[0] || !encoded[1]) return false;
            int a = hex_value(encoded[0]), b = hex_value(encoded[1]);
            if (a < 0 || b < 0) return false;
            c = (unsigned char)((a << 4) | b); encoded += 2;
        }
        if (n + 1 >= cap || c < 32 || c == 127 || strchr("/\\:*?\"<>|", c)) return false;
        out[n++] = (char)c;
    }
    out[n] = 0;
    if (!n || out[0] == '.' || out[0] == ' ' || out[n - 1] == ' ' || strstr(out, "..")) return false;
    const char *ext = strrchr(out, '.');
    if (!ext || (kind == 1 ? (strcasecmp(ext, ".jpg") && strcasecmp(ext, ".jpeg") && strcasecmp(ext, ".png"))
               : kind == 2 ? (strcasecmp(ext, ".ttf") && strcasecmp(ext, ".otf"))
                           : (strcasecmp(ext, ".txt") && strcasecmp(ext, ".epub")))) return false;
    // 拒绝非规范 UTF-8、代理项与越界码点。/ Reject noncanonical UTF-8, surrogates and out-of-range code points.
    for (size_t i = 0; i < n;) {
        uint32_t cp; unsigned more; unsigned char c = (unsigned char)out[i++];
        if (c < 128) continue;
        if (c >= 0xc2 && c <= 0xdf) { cp = c & 31; more = 1; }
        else if (c >= 0xe0 && c <= 0xef) { cp = c & 15; more = 2; }
        else if (c >= 0xf0 && c <= 0xf4) { cp = c & 7; more = 3; }
        else return false;
        unsigned width = more;
        while (more--) {
            if (i == n || ((unsigned char)out[i] & 0xc0) != 0x80) return false;
            cp = (cp << 6) | ((unsigned char)out[i++] & 63);
        }
        if ((width == 2 && cp < 0x800) || (width == 3 && cp < 0x10000) ||
            (cp >= 0xd800 && cp <= 0xdfff) || cp > 0x10ffff) return false;
    }
    return true;
}

static bool decode_name(const char *encoded, char out[121]) { return decode_name_limit(encoded, out, 121, false); }

static bool raw_book_name(const char *entry, char *name, size_t cap) {
    size_t len = strlen(entry);
    if (!len || len >= cap || len > 255) return false;
    static const char digits[] = "0123456789ABCDEF";
    char encoded[766];
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)entry[i];
        encoded[i * 3] = '%'; encoded[i * 3 + 1] = digits[c >> 4]; encoded[i * 3 + 2] = digits[c & 15];
    }
    encoded[len * 3] = 0;
    return decode_name_limit(encoded, name, cap, false);
}

// 两个存储根都是FAT；变更使用目录真实拼写，不明别名拒绝操作，避免漏清阅读记录。
// Both storage roots use FAT; use directory spelling and reject unknown aliases to protect progress identity.
static bool ascii_name_equal(const char *a, const char *b) {
    while (*a && *b) {
        unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return false;
    }
    return *a == *b;
}

static int resolve_mutation_path(const char *root, const char *name,
                                 char *path, size_t cap) {
    if (snprintf(path, cap, "%s/%s", root, name) >= (int)cap) return 400;
    // 主文件优先；只有备份的中断状态也保留原始名字。/ Prefer the main file; preserve spelling when only a backup remains.
    for (int pass = 0; pass < 2; ++pass) {
        DIR *dir = opendir(root);
        if (!dir) return 507;
        bool found = false;
        int result = 0;
        struct dirent *entry;
        while (errno = 0, (entry = readdir(dir)) != NULL) {
            const char *suffix = ".rename-backup";
            size_t len = strlen(entry->d_name), tail = pass ? strlen(suffix) : 0;
            if (len <= tail || len - tail > 255 || (pass && strcmp(entry->d_name + len - tail, suffix))) continue;
            char actual[256]; memcpy(actual, entry->d_name, len - tail); actual[len - tail] = 0;
            if (!ascii_name_equal(actual, name)) continue;
            if (found || snprintf(path, cap, "%s/%s", root, actual) >= (int)cap) { result = 400; break; }
            found = true;
        }
        if (!entry && errno) result = 507;
        if (closedir(dir)) result = 507;
        if (result || found) return result;
        if (!pass) {
            struct stat st;
            if (!stat(path, &st)) return 400;
            if (errno != ENOENT) return 507;
        }
    }
    struct stat st;
    if (!stat(path, &st)) return 400;
    if (errno != ENOENT) return 507;
    if (strlen(name) + strlen(".rename-backup") <= 255) {
        char backup[480];
        if (snprintf(backup, sizeof(backup), "%s.rename-backup", path) >= (int)sizeof(backup)) return 400;
        if (!stat(backup, &st)) return 400;
        if (errno != ENOENT) return 507;
    }
    return 0;
}

// 网页只接受 TF 卡内相对路径；每段单独检查，不能通过编码后的斜杠绕出根目录。
// Accept only TF-relative paths; validate every segment after URL decoding.
static bool sd_relative_valid(const char *relative, bool allow_root) {
    size_t length = strlen(relative);
    if (!length) return allow_root;
    if (length > 238 || relative[0] == '/' || relative[length - 1] == '/') return false;
    const char *segment = relative;
    for (const char *p = relative;; ++p) {
        unsigned char ch = (unsigned char)*p;
        if (ch && ch != '/') {
            if (ch < 32 || ch == 127 || strchr("\\:*?\"<>|", ch)) return false;
            continue;
        }
        size_t n = (size_t)(p - segment);
        if (!n || n > 240 || (n == 1 && segment[0] == '.') ||
            (n == 2 && segment[0] == '.' && segment[1] == '.') ||
            segment[0] == ' ' || segment[n - 1] == ' ' || segment[n - 1] == '.') return false;
        if (!ch) return true;
        segment = p + 1;
    }
}

static bool sd_decode_relative(const char *encoded, char relative[240], bool allow_root) {
    size_t n = 0;
    while (*encoded) {
        unsigned char ch = (unsigned char)*encoded++;
        if (ch == '%') {
            if (!encoded[0] || !encoded[1]) return false;
            int hi = hex_value(encoded[0]), lo = hex_value(encoded[1]);
            if (hi < 0 || lo < 0) return false;
            ch = (unsigned char)((hi << 4) | lo);
            encoded += 2;
        }
        if (!ch || n + 1 >= 240) return false;
        relative[n++] = (char)ch;
    }
    relative[n] = 0;
    return sd_relative_valid(relative, allow_root);
}

static bool sd_absolute(const char *relative, char *out, size_t cap) {
    return snprintf(out, cap, "/sdcard%s%s", relative[0] ? "/" : "", relative) < (int)cap;
}


static int check_length(size_t total, size_t limit, uint64_t available) {
    if (!total) return 400;
    if (limit && total > limit) return 413;
    if (total > available) return 507;
    return 0;
}

static bool temporary_basename(const char *entry, const char *suffix, char name[121]) {
    size_t len = strlen(entry), tail = strlen(suffix);
    if (len <= tail || len - tail > 120 || strcmp(entry + len - tail, suffix)) return false;
    // 目录项已解码；重新百分号编码后复用校验，保留文件名里的字面百分号。
    // Directory entries are already decoded; encode before validation to preserve literal percent signs.
    static const char digits[] = "0123456789ABCDEF";
    char encoded[361];
    for (size_t i = 0; i < len - tail; ++i) {
        unsigned char c = (unsigned char)entry[i];
        encoded[i * 3] = '%'; encoded[i * 3 + 1] = digits[c >> 4]; encoded[i * 3 + 2] = digits[c & 15];
    }
    encoded[(len - tail) * 3] = 0;
    return decode_name(encoded, name);
}

static int cleanup_interrupted(const char *root, unsigned *removed, unsigned *restored) {
    *removed = *restored = 0;
    // 先恢复缺失的旧书，再删未提交片段；不递归、不删除已有完整书或其备份。
    // Restore missing old books before removing uncommitted parts; never recurse or delete complete books or their backups.
    for (int pass = 0; pass < 2; ++pass) {
        DIR *dir = opendir(root);
        if (!dir) return -1;
        int result = 0;
        struct dirent *entry;
        while (errno = 0, (entry = readdir(dir)) != NULL) {
            char name[121], path[320], target[288];
            if (!temporary_basename(entry->d_name, pass ? ".part" : ".rename-backup", name)) continue;
            if (snprintf(path, sizeof(path), "%s/%s", root, entry->d_name) >= (int)sizeof(path) ||
                snprintf(target, sizeof(target), "%s/%s", root, name) >= (int)sizeof(target)) { result = -1; break; }
            struct stat st;
            if (stat(path, &st)) { result = -1; break; }
            if (!S_ISREG(st.st_mode)) continue;
            if (!pass) {
                if (!stat(target, &st)) continue;
                if (errno != ENOENT || rename(path, target)) { result = -1; break; }
                ++*restored;
            } else {
                if (remove(path)) { result = -1; break; }
                ++*removed;
            }
        }
        if (!entry && errno) result = -1;
        if (closedir(dir)) result = -1;
        if (result) return result;
    }
    return 0;
}

static int commit_file(const char *part, const char *path) {
    if (rename(part, path) == 0) return 0;
    // FatFs 不支持直接覆盖；保留旧书直至新文件成功就位。/ FatFs cannot replace directly; retain the old book until commit.
    if (errno != EEXIST && errno != EACCES) return -1;
    struct stat st;
    if (stat(path, &st) || !S_ISREG(st.st_mode)) return -1;
    char backup[320];
    if (snprintf(backup, sizeof(backup), "%s.rename-backup", path) >= (int)sizeof(backup)) return -1;
    if (!stat(backup, &st) || errno != ENOENT) return -1;
    if (rename(path, backup)) return -1;
    if (rename(part, path)) { rename(backup, path); return -1; }
    // 新书已提交；备份删除失败只能报告警告，不能跳过进度失效回调。
    // The new book is committed; failed backup removal is a warning and must not skip progress invalidation.
    return remove(backup) ? 1 : 0;
}

typedef int (*receive_cb_t)(void *, char *, size_t);
typedef void (*progress_cb_t)(size_t);
enum { TRANSFER_COMMITTED_BACKUP_RETAINED = 299 };
static int receive_file(const char *path, const char *part, size_t total, char *buf,
                        size_t cap, receive_cb_t recv, void *ctx, progress_cb_t progress) {
    FILE *f = fopen(part, "wb");
    if (!f) return 507;
    // HTTP 常按小包到达；扩大文件缓冲可显著减少 TF 卡零碎写入。
    // HTTP arrives in small packets; a larger file buffer cuts fragmented SD writes.
#ifdef ESP_PLATFORM
    char *write_buffer = heap_caps_malloc(8192, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    char *write_buffer = malloc(8192);
#endif
    if (write_buffer && setvbuf(f, write_buffer, _IOFBF, 8192) != 0) {
        free(write_buffer);
        write_buffer = NULL;
    }
    int error = 0;
    size_t done = 0;
    while (done < total) {
        size_t n = total - done < cap ? total - done : cap;
        int got = recv(ctx, buf, n);
        if (got <= 0 || (size_t)got > n) { error = 408; break; }
        if (fwrite(buf, 1, (size_t)got, f) != (size_t)got) { error = 507; break; }
        done += (size_t)got;
        if (progress) progress(done);
    }
    if (fclose(f) != 0) error = 507;
    free(write_buffer);
    if (!error) {
        int committed = commit_file(part, path);
        if (committed < 0) error = 507;
        else if (committed > 0) return TRANSFER_COMMITTED_BACKUP_RETAINED;
    }
    if (error) remove(part);
    return error;
}

typedef int (*file_changed_cb_t)(const char *);
typedef struct {
    int status;
    bool changed, progress_cleanup_failed, storage_cleanup_failed;
} file_result_t;

static file_result_t changed_result(const char *path, file_changed_cb_t callback) {
    bool failed = callback && callback(path) != 0;
    return (file_result_t){.status = failed ? 500 : 200, .changed = true, .progress_cleanup_failed = failed};
}

static void record_file_change(unsigned *count, file_result_t result, bool retry) {
    if (result.changed || (retry && result.status == 200)) ++*count;
}

static int destination_status(const char *path, bool overwrite) {
    struct stat st;
    if (!stat(path, &st)) return !S_ISREG(st.st_mode) ? 400 : overwrite ? 0 : 409;
    return errno == ENOENT ? 0 : 507;
}

static file_result_t upload_managed(const char *path, const char *part, size_t total, size_t limit,
        uint64_t available, bool overwrite, char *buf, size_t cap, receive_cb_t recv, void *ctx,
        progress_cb_t progress, file_changed_cb_t changed) {
    int status = destination_status(path, overwrite);
    if (!status) status = check_length(total, limit, available);
    if (!status) status = receive_file(path, part, total, buf, cap, recv, ctx, progress);
    if (status && status != TRANSFER_COMMITTED_BACKUP_RETAINED) return (file_result_t){.status = status};
    file_result_t result = changed_result(path, changed);
    result.storage_cleanup_failed = status == TRANSFER_COMMITTED_BACKUP_RETAINED;
    return result;
}

// 通用文件在目标目录创建独占临时文件，不会覆盖用户原有的同名 .part 文件。
// General uploads reserve a unique part in the destination directory, preserving existing user .part files.
static file_result_t upload_directory_file(const char *root, const char *relative, size_t total,
        uint64_t available, bool overwrite, char *buf, size_t cap, receive_cb_t recv, void *ctx,
        progress_cb_t progress, file_changed_cb_t changed) {
    if (!sd_relative_valid(relative, false)) return (file_result_t){.status = 400};
    char parent[256], path[256], part[288];
    const char *name = strrchr(relative, '/');
    size_t prefix = name ? (size_t)(name - relative) : 0;
    name = name ? name + 1 : relative;
    if (snprintf(parent, sizeof(parent), "%s%s%.*s", root, prefix ? "/" : "",
                 (int)prefix, relative) >= (int)sizeof(parent)) return (file_result_t){.status = 400};
    struct stat st;
    if (stat(parent, &st) || !S_ISDIR(st.st_mode)) return (file_result_t){.status = 404};
    int status = resolve_mutation_path(parent, name, path, sizeof(path));
    if (!status) status = destination_status(path, overwrite);
    if (!status && total > available) status = 507;
    if (status) return (file_result_t){.status = status};
    if (snprintf(part, sizeof(part), "%s/.pico-upload-XXXXXX", parent) >= (int)sizeof(part))
        return (file_result_t){.status = 400};
    int fd = mkstemp(part);
    if (fd < 0) return (file_result_t){.status = 507};
    if (close(fd)) { remove(part); return (file_result_t){.status = 507}; }
    // 空文件也有效；图书快捷上传仍由原来的长度策略拒绝空内容。
    // Empty general files are valid; typed book uploads still reject empty content.
    status = receive_file(path, part, total, buf, cap, recv, ctx, progress);
    if (status && status != TRANSFER_COMMITTED_BACKUP_RETAINED) {
        remove(part);
        return (file_result_t){.status = status};
    }
    const char *ext = strrchr(name, '.');
    file_result_t result = changed_result(path,
        ext && (!strcasecmp(ext, ".epub") || !strcasecmp(ext, ".txt")) ? changed : NULL);
    result.storage_cleanup_failed = status == TRANSFER_COMMITTED_BACKUP_RETAINED;
    return result;
}

static file_result_t delete_managed(const char *path, file_changed_cb_t changed) {
    struct stat st;
    bool exists = stat(path, &st) == 0;
    if (!exists && errno != ENOENT) return (file_result_t){.status = 507};
    if (exists && !S_ISREG(st.st_mode)) return (file_result_t){.status = 400};
    char backup[480];
    const char *name = strrchr(path, '/'); name = name ? name + 1 : path;
    // 后缀会超过文件名上限时不探测备份，避免长书名删除被ENAMETOOLONG阻止。
    // Skip impossible backup names so ENAMETOOLONG cannot prevent deleting a long book name.
    if (strlen(name) + strlen(".rename-backup") <= 255) {
        if (snprintf(backup, sizeof(backup), "%s.rename-backup", path) >= (int)sizeof(backup)) return (file_result_t){.status = 400};
        bool backed_up = stat(backup, &st) == 0;
        if (!backed_up && errno != ENOENT) return (file_result_t){.status = 507};
        if (backed_up) {
            if (!S_ISREG(st.st_mode)) return (file_result_t){.status = 400};
            if (exists) {
                if (remove(backup)) return (file_result_t){.status = 507};
            } else {
                if (rename(backup, path)) return (file_result_t){.status = 507};
                exists = true;
            }
        }
    }
    if (!exists) return (file_result_t){.status = 404};
    if (remove(path)) return (file_result_t){.status = 507};
    return changed_result(path, changed);
}

#define BOOK_LIST_PAGE_SIZE 16
typedef struct { char name[256]; uint64_t size; } transfer_book_entry_t;
typedef struct {
    transfer_book_entry_t items[BOOK_LIST_PAGE_SIZE];
    size_t count, total;
} transfer_book_page_t;

static bool name_contains(const char *name, const char *query) {
    return read_pico_search_match(name, query);
}

static int list_books(const char *root, const char *query, size_t page, transfer_book_page_t *out) {
    memset(out, 0, sizeof(*out));
    if (page > SIZE_MAX / BOOK_LIST_PAGE_SIZE) return 400;
    DIR *dir = opendir(root);
    if (!dir) return 507;
    int status = 0;
    struct dirent *entry;
    while (errno = 0, (entry = readdir(dir)) != NULL) {
        char name[256], path[448];
        if (!raw_book_name(entry->d_name, name, sizeof(name)) || !name_contains(name, query)) continue;
        if (snprintf(path, sizeof(path), "%s/%s", root, name) >= (int)sizeof(path)) { status = 507; break; }
        struct stat st;
        if (stat(path, &st)) { status = 507; break; }
        if (!S_ISREG(st.st_mode) || st.st_size < 0) continue;
        size_t index = out->total++;
        if (index >= page * BOOK_LIST_PAGE_SIZE && out->count < BOOK_LIST_PAGE_SIZE) {
            transfer_book_entry_t *item = &out->items[out->count++];
            strcpy(item->name, name); item->size = (uint64_t)st.st_size;
        }
    }
    if (!entry && errno) status = 507;
    if (closedir(dir)) status = 507;
    return status;
}

#ifndef READ_PICO_TRANSFER_HOST_TEST
#include "read_pico_transfer.h"
#include "transfer_signature.h"
#include "ble_page_turner.h"
static bool s_ble_network_reserved;
#include "transfer_policy.h"
#include "transfer_credentials_store.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "transfer_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static read_pico_transfer_status_t s_status;
static read_pico_transfer_cfg_t s_cfg;
static char s_root[160];
static read_pico_transfer_cfg_t s_sleep_cfg;
static char s_sleep_root[160];
static bool s_sleep_paused;
static bool s_rf_blocked;
static httpd_handle_t s_http;
static esp_netif_t *s_netif;
static esp_event_handler_instance_t s_events, s_ip_events;
static bool s_wifi, s_started, s_loop_owned;
static char *s_buffer;
static bool s_stopping, s_upload_active;
static bool s_config_busy;
// 对时独占 SNTP 与临时 WiFi；取消后等待清理完成，才允许拆网络或睡眠。
// Time sync owns SNTP/temporary WiFi; wait for cleanup before network teardown or sleep.
static bool s_time_active, s_time_automatic, s_time_cancel;
static uint32_t s_time_pending_utc;
static bool cancel_time_sync(bool automatic_only) {
    portENTER_CRITICAL(&s_lock);
    bool active = s_time_active;
    bool cancellable = !active || !automatic_only || s_time_automatic;
    if (active && cancellable) s_time_cancel = true;
    portEXIT_CRITICAL(&s_lock);
    if (!cancellable) return false;
    const int64_t deadline = esp_timer_get_time() + 2000000;
    while (active) {
        vTaskDelay(pdMS_TO_TICKS(10));
        portENTER_CRITICAL(&s_lock); active = s_time_active; portEXIT_CRITICAL(&s_lock);
        if (active && esp_timer_get_time() >= deadline) return false;
    }
    return true;
}
static bool time_sync_cancelled(void) {
    portENTER_CRITICAL(&s_lock); bool cancelled = s_time_cancel; portEXIT_CRITICAL(&s_lock);
    return cancelled;
}
static esp_err_t sync_time_online_impl(uint32_t *utc_seconds);
static esp_err_t time_sync_ntp(uint32_t *utc_seconds) {
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(3,
        ESP_SNTP_SERVER_LIST("ntp.aliyun.com", "time.cloudflare.com", "pool.ntp.org"));
    if (time_sync_cancelled()) return ESP_ERR_INVALID_STATE;
    esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK) return err;
    const int64_t deadline = esp_timer_get_time() + 12000000;
    do {
        if (time_sync_cancelled()) { err = ESP_ERR_INVALID_STATE; break; }
        err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(100));
    } while (err == ESP_ERR_TIMEOUT && esp_timer_get_time() < deadline);
    if (err == ESP_OK && time_sync_cancelled()) err = ESP_ERR_INVALID_STATE;
    if (err == ESP_OK) {
        time_t now = time(NULL);
        if (now >= 1704067200 && now <= 4102444799U) *utc_seconds = (uint32_t)now;
        else err = ESP_ERR_INVALID_RESPONSE;
    }
    esp_netif_sntp_deinit();
    return err;
}
static EventGroupHandle_t s_time_events;
#define TIME_GOT_IP BIT0
#define TIME_DISCONNECTED BIT1
static void time_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)data;
    if (!s_time_events) return;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) xEventGroupSetBits(s_time_events, TIME_GOT_IP);
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) xEventGroupSetBits(s_time_events, TIME_DISCONNECTED);
}
static transfer_connection_t s_connection;
extern const char upload_start[] asm("_binary_upload_html_start");
extern const char upload_end[] asm("_binary_upload_html_end");

void read_pico_transfer_get_status(read_pico_transfer_status_t *out) {
    if (!out) return;
    portENTER_CRITICAL(&s_lock); *out = s_status; portEXIT_CRITICAL(&s_lock);
}

static bool claim_config(void) {
    portENTER_CRITICAL(&s_lock);
    bool available = !s_config_busy;
    if (available) s_config_busy = true;
    portEXIT_CRITICAL(&s_lock);
    return available;
}

static void release_config(void) {
    portENTER_CRITICAL(&s_lock); s_config_busy = false; portEXIT_CRITICAL(&s_lock);
}

static void publish_credentials(const transfer_credentials_t *c) {
    portENTER_CRITICAL(&s_lock);
    s_status.wifi_configured = c && c->version == 1;
    memset(s_status.wifi_ssid, 0, sizeof(s_status.wifi_ssid));
    if (s_status.wifi_configured) memcpy(s_status.wifi_ssid, c->ssid, sizeof(s_status.wifi_ssid));
    portEXIT_CRITICAL(&s_lock);
}

esp_err_t read_pico_transfer_get_saved_wifi(char ssid[33], bool *configured) {
    if (!ssid || !configured) return ESP_ERR_INVALID_ARG;
    ssid[0] = 0; *configured = false;
    if (!claim_config()) return ESP_ERR_INVALID_STATE;
    transfer_credentials_t saved;
    esp_err_t err = load_credentials(&saved);
    if (err == ESP_OK && saved.version == 1) { memcpy(ssid, saved.ssid, 33); *configured = true; }
    clear_secret(&saved, sizeof(saved)); release_config();
    return err;
}

esp_err_t read_pico_transfer_export_wifi_backup(read_pico_transfer_wifi_backup_t *out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    if (!claim_config()) return ESP_ERR_INVALID_STATE;
    transfer_credentials_t saved;
    esp_err_t err = load_credentials(&saved);
    if (err == ESP_OK && saved.version == 1) {
        out->configured = 1;
        memcpy(out->ssid, saved.ssid, sizeof(out->ssid));
        memcpy(out->password, saved.password, sizeof(out->password));
    }
    clear_secret(&saved, sizeof(saved));
    release_config();
    return err;
}

bool read_pico_transfer_wifi_backup_valid(const read_pico_transfer_wifi_backup_t *backup) {
    if (!backup || backup->configured > 1) return false;
    if (!backup->configured) {
        for (size_t i = 0; i < sizeof(backup->ssid); ++i)
            if (backup->ssid[i]) return false;
        for (size_t i = 0; i < sizeof(backup->password); ++i)
            if (backup->password[i]) return false;
        return true;
    }
    transfer_credentials_t saved = {0};
    saved.version = 1;
    memcpy(saved.ssid, backup->ssid, sizeof(saved.ssid));
    memcpy(saved.password, backup->password, sizeof(saved.password));
    bool valid = credentials_valid(&saved);
    clear_secret(&saved, sizeof(saved));
    return valid;
}

esp_err_t read_pico_transfer_import_wifi_backup(const read_pico_transfer_wifi_backup_t *backup) {
    if (!read_pico_transfer_wifi_backup_valid(backup)) return ESP_ERR_INVALID_ARG;
    if (!claim_config()) return ESP_ERR_INVALID_STATE;
    transfer_credentials_t next = {0};
    if (backup->configured) {
        next.version = 1;
        memcpy(next.ssid, backup->ssid, sizeof(next.ssid));
        memcpy(next.password, backup->password, sizeof(next.password));
    }
    esp_err_t err = store_credentials(backup->configured ? &next : NULL);
    if (err == ESP_OK) {
        transfer_credentials_t check = {0};
        err = load_credentials(&check);
        if (err == ESP_OK &&
            (check.version != next.version ||
             (backup->configured &&
              (memcmp(check.ssid, next.ssid, sizeof(next.ssid)) ||
               memcmp(check.password, next.password, sizeof(next.password))))))
            err = ESP_FAIL;
        clear_secret(&check, sizeof(check));
    }
    if (err == ESP_OK && !s_wifi) publish_credentials(backup->configured ? &next : NULL);
    clear_secret(&next, sizeof(next));
    release_config();
    return err;
}

esp_err_t read_pico_transfer_save_wifi(const char *ssid, const char *password) {
    if (!cancel_time_sync(true)) return ESP_ERR_INVALID_STATE;
    read_pico_transfer_status_t status;
    read_pico_transfer_get_status(&status);
    if (s_wifi || s_http || s_netif || status.state != READ_PICO_TRANSFER_STOPPED) return ESP_ERR_INVALID_STATE;
    transfer_credentials_t next;
    if (!credentials_from_text(ssid, password, &next)) return ESP_ERR_INVALID_ARG;
    if (!claim_config()) { clear_secret(&next, sizeof(next)); return ESP_ERR_INVALID_STATE; }
    esp_err_t err = store_credentials(&next);
    if (err == ESP_OK) {
        publish_credentials(&next);
        ESP_LOGI("transfer", "wifi saved configured=1");
    }
    clear_secret(&next, sizeof(next)); release_config(); return err;
}

static esp_err_t sync_time_impl(uint32_t *utc_seconds) {
    if (!utc_seconds) return ESP_ERR_INVALID_ARG;
    *utc_seconds = 0;
    // 对时走的是独立临时射频通路，不经过 start()，闸门要在这里也拦一次。
    // Time sync opens its own temporary radio path and never calls start(), so gate it here too.
    if (s_rf_blocked) return ESP_ERR_INVALID_STATE;
    read_pico_transfer_status_t status;
    read_pico_transfer_get_status(&status);
    transfer_time_route_t route = time_route(s_wifi,
        status.mode == READ_PICO_TRANSFER_MODE_STA, status.network_ready,
        !s_http && !s_netif && status.state == READ_PICO_TRANSFER_STOPPED);
    if (route == TRANSFER_TIME_ONLINE) return sync_time_online_impl(utc_seconds);
    if (route != TRANSFER_TIME_TEMPORARY) return ESP_ERR_INVALID_STATE;
    transfer_credentials_t saved = {0};
    esp_err_t err = load_credentials(&saved);
    if (err != ESP_OK || saved.version != 1) {
        clear_secret(&saved, sizeof(saved));
        return err == ESP_OK ? ESP_ERR_NOT_FOUND : err;
    }
    err = ble_pt_network_acquire();
    if (err != ESP_OK) { clear_secret(&saved, sizeof(saved)); return err; }
    bool loop_owned = false, wifi_initialized = false, wifi_started = false;
    esp_netif_t *netif = NULL;
    esp_event_handler_instance_t wifi_handler = NULL, ip_handler = NULL;
    wifi_config_t wifi = {0};
    memcpy(wifi.sta.ssid, saved.ssid, strlen(saved.ssid));
    memcpy(wifi.sta.password, saved.password, strlen(saved.password));
    wifi.sta.threshold.authmode = saved.password[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    wifi.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wifi.sta.pmf_cfg.capable = true;
    wifi.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    clear_secret(&saved, sizeof(saved));
    s_time_events = xEventGroupCreate();
    if (!s_time_events) { clear_secret(&wifi, sizeof(wifi)); ble_pt_network_release(); return ESP_ERR_NO_MEM; }
    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) goto time_cleanup;
    err = esp_event_loop_create_default();
    if (err == ESP_OK) loop_owned = true;
    else if (err != ESP_ERR_INVALID_STATE) goto time_cleanup;
    err = transfer_create_netif(false, &netif);
    if (err != ESP_OK) goto time_cleanup;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init); if (err != ESP_OK) goto time_cleanup;
    wifi_initialized = true;
    err = esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                               time_wifi_event, NULL, &wifi_handler);
    if (err != ESP_OK) goto time_cleanup;
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               time_wifi_event, NULL, &ip_handler);
    if (err != ESP_OK) goto time_cleanup;
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM); if (err != ESP_OK) goto time_cleanup;
    err = esp_wifi_set_mode(WIFI_MODE_STA); if (err != ESP_OK) goto time_cleanup;
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi); if (err != ESP_OK) goto time_cleanup;
    clear_secret(&wifi, sizeof(wifi));
    err = esp_wifi_start(); if (err != ESP_OK) goto time_cleanup;
    wifi_started = true;
    err = esp_wifi_connect(); if (err != ESP_OK) goto time_cleanup;
    const int64_t ip_deadline = esp_timer_get_time() + 15000000;
    EventBits_t bits = 0;
    while (!(bits & TIME_GOT_IP) && esp_timer_get_time() < ip_deadline) {
        if (time_sync_cancelled()) { err = ESP_ERR_INVALID_STATE; goto time_cleanup; }
        bits = xEventGroupWaitBits(s_time_events, TIME_GOT_IP, pdFALSE, pdFALSE, pdMS_TO_TICKS(100));
    }
    if (!(bits & TIME_GOT_IP)) { err = ESP_ERR_TIMEOUT; goto time_cleanup; }
    err = time_sync_ntp(utc_seconds);
time_cleanup:
    if (wifi_started) esp_wifi_stop();
    if (ip_handler) esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_handler);
    if (wifi_handler) esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, wifi_handler);
    if (wifi_initialized) esp_wifi_deinit();
    if (netif) esp_netif_destroy_default_wifi(netif);
    if (loop_owned) esp_event_loop_delete_default();
    vEventGroupDelete(s_time_events); s_time_events = NULL;
    clear_secret(&wifi, sizeof(wifi));
    ble_pt_network_release();

    return err;
}

static esp_err_t sync_time_online_impl(uint32_t *utc_seconds) {
    read_pico_transfer_status_t status;
    read_pico_transfer_get_status(&status);
    if (!s_wifi || status.mode != READ_PICO_TRANSFER_MODE_STA || !status.network_ready ||
        status.state == READ_PICO_TRANSFER_UPLOADING) return ESP_ERR_INVALID_STATE;
    return time_sync_ntp(utc_seconds);
}

static esp_err_t sync_time_run(uint32_t *utc_seconds, bool online_only, bool automatic) {
    if (!utc_seconds) return ESP_ERR_INVALID_ARG;
    *utc_seconds = 0;
    // 手动操作优先于自动任务；配置和对时的准入在同一个锁下，防止双初始化。
    // Manual actions preempt automatic work; one lock admits configuration and time ownership.
    if (!automatic && !cancel_time_sync(true)) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&s_lock);
    bool available = !s_config_busy && !s_time_active && !s_stopping;
    // 停服的 s_stopping 是正常状态，已有资源则仍拒绝正在拆除的网络。
    // A stopped service normally keeps s_stopping set; reject only a teardown with resources.
    if (!s_config_busy && !s_time_active && s_status.state == READ_PICO_TRANSFER_STOPPED)
        available = true;
    if (available) {
        s_config_busy = s_time_active = true;
        s_time_automatic = automatic;
        s_time_cancel = false;
    }
    portEXIT_CRITICAL(&s_lock);
    if (!available) return ESP_ERR_INVALID_STATE;
    esp_err_t err = online_only ? sync_time_online_impl(utc_seconds) : sync_time_impl(utc_seconds);
    portENTER_CRITICAL(&s_lock);
    if (err == ESP_OK) s_time_pending_utc = *utc_seconds;
    s_time_active = s_time_automatic = s_config_busy = false;
    portEXIT_CRITICAL(&s_lock);
    return err;
}
esp_err_t read_pico_transfer_sync_time(uint32_t *utc_seconds) {
    return sync_time_run(utc_seconds, false, false);
}
esp_err_t read_pico_transfer_sync_time_online(uint32_t *utc_seconds) {
    return sync_time_run(utc_seconds, true, false);
}
esp_err_t read_pico_transfer_sync_time_auto(uint32_t *utc_seconds) {
    return sync_time_run(utc_seconds, false, true);
}
bool read_pico_transfer_take_time_sync(uint32_t *utc_seconds) {
    if (!utc_seconds) return false;
    portENTER_CRITICAL(&s_lock);
    *utc_seconds = s_time_pending_utc;
    s_time_pending_utc = 0;
    portEXIT_CRITICAL(&s_lock);
    return *utc_seconds != 0;
}

esp_err_t read_pico_transfer_scan_wifi(read_pico_transfer_network_t out[READ_PICO_TRANSFER_SCAN_MAX], size_t *count) {
    if (!cancel_time_sync(true)) return ESP_ERR_INVALID_STATE;
    // 飞行模式：保留凭据，只是不让射频起来。/ Flight mode: keep credentials, never raise the radio.
    if (s_rf_blocked) return ESP_ERR_INVALID_STATE;
    if (!out || !count) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out) * READ_PICO_TRANSFER_SCAN_MAX); *count = 0;
    read_pico_transfer_status_t status;
    read_pico_transfer_get_status(&status);
    if (s_wifi || s_http || s_netif || status.state != READ_PICO_TRANSFER_STOPPED) return ESP_ERR_INVALID_STATE;
    if (!claim_config()) return ESP_ERR_INVALID_STATE;
    esp_err_t err = ble_pt_network_acquire();
    if (err != ESP_OK) { release_config(); return err; }
    bool initialized = false, started = false;
    esp_netif_t *scan_netif = NULL;
    const char *stage = "netif";
    ESP_LOGI("transfer", "wifi scan heap internal free=%u largest=%u psram free=%u largest=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) goto cleanup;
    stage = "event-loop";
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) goto cleanup;
    // ESP-IDF 的标准扫描流程会先创建 STA netif。ESP32-S3 的 remote/hosted
    // WiFi 后端同样依赖这个接口来完成控制面初始化；旧的“仅驱动扫描”在真机上
    // 会稳定返回 WIFI_STATE/WIFI_CONN。
    // The documented scan flow creates a STA netif first. The remote/hosted
    // backend used by this target also needs it for control-plane setup.
    stage = "sta-netif";
    err = transfer_create_netif(false, &scan_netif);
    if (err != ESP_OK) goto cleanup;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    stage = "wifi-init";
    err = esp_wifi_init(&init); if (err != ESP_OK) goto cleanup;
    initialized = true;
    stage = "wifi-storage";
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM); if (err != ESP_OK) goto cleanup;
    stage = "wifi-mode";
    err = esp_wifi_set_mode(WIFI_MODE_STA); if (err != ESP_OK) goto cleanup;
    // 覆盖中国 2.4 GHz 的 1–13 信道；设置失败不阻断扫描，保留驱动默认值。
    // Cover CN 2.4 GHz channels 1–13. A backend that cannot set country keeps its default.
    wifi_country_t country = {.cc = "CN", .schan = 1, .nchan = 13, .policy = WIFI_COUNTRY_POLICY_MANUAL};
    esp_err_t country_err = esp_wifi_set_country(&country);
    if (country_err != ESP_OK) ESP_LOGW("transfer", "wifi country setup: %s", esp_err_to_name(country_err));
    stage = "wifi-start";
    err = esp_wifi_start(); if (err != ESP_OK) goto cleanup;
    started = true;
    // Hosted WiFi may need one scheduler turn after start before accepting scan RPCs.
    vTaskDelay(pdMS_TO_TICKS(120));
    stage = "scan";
    err = esp_wifi_scan_start(NULL, true);
    if (err != ESP_OK) {
        // Recover once from a transient hosted-radio state instead of making the
        // user leave the page and re-enter it.
        ESP_LOGW("transfer", "wifi scan first attempt: %s", esp_err_to_name(err));
        (void)esp_wifi_scan_stop();
        (void)esp_wifi_clear_ap_list();
        vTaskDelay(pdMS_TO_TICKS(180));
        err = esp_wifi_scan_start(NULL, true);
    }
    if (err != ESP_OK) goto cleanup;
    uint16_t found = 0;
    stage = "scan-count";
    err = esp_wifi_scan_get_ap_num(&found); if (err != ESP_OK) goto cleanup;
    if (found) {
        // Fetch the result list exactly once. Repeated get_ap_record calls are
        // unreliable with remote WiFi and keep driver-side scan memory alive.
        // The driver returns scan records in signal-strength order. The UI only
        // exposes READ_PICO_TRANSFER_SCAN_MAX entries, so never reserve a larger
        // temporary list. Keep it in PSRAM to leave scarce internal RAM available
        // to the hosted WiFi control path.
        uint16_t capacity = found > READ_PICO_TRANSFER_SCAN_MAX ? READ_PICO_TRANSFER_SCAN_MAX : found;
        wifi_ap_record_t *records = heap_caps_calloc(
            capacity, sizeof(*records), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!records) records = heap_caps_calloc(capacity, sizeof(*records), MALLOC_CAP_8BIT);
        if (!records) { err = ESP_ERR_NO_MEM; goto cleanup; }
        stage = "scan-results";
        err = esp_wifi_scan_get_ap_records(&capacity, records);
        if (err != ESP_OK) { heap_caps_free(records); goto cleanup; }
        for (uint16_t i = 0; i < capacity; ++i) {
            const wifi_ap_record_t *ap = &records[i];
            read_pico_transfer_network_t item = {.rssi = ap->rssi, .authmode = (uint8_t)ap->authmode,
                .requires_password = ap->authmode != WIFI_AUTH_OPEN && ap->authmode != WIFI_AUTH_OWE};
            memcpy(item.ssid, ap->ssid, sizeof(item.ssid) - 1);
            item.supported = ap->authmode == WIFI_AUTH_OPEN || ap->authmode == WIFI_AUTH_WPA2_PSK ||
                ap->authmode == WIFI_AUTH_WPA_WPA2_PSK || ap->authmode == WIFI_AUTH_WPA3_PSK ||
                ap->authmode == WIFI_AUTH_WPA2_WPA3_PSK;
            scan_offer(out, count, &item);
        }
        heap_caps_free(records);
    }
cleanup:
    if (err != ESP_OK) {
        ESP_LOGE("transfer", "wifi scan failed stage=%s result=%s internal free=%u largest=%u",
                 stage, esp_err_to_name(err),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    if (started && err != ESP_OK) { (void)esp_wifi_scan_stop(); (void)esp_wifi_clear_ap_list(); }
    if (started) {
        esp_err_t stop_err = esp_wifi_stop();
        if (stop_err != ESP_OK) ESP_LOGW("transfer", "wifi scan stop: %s", esp_err_to_name(stop_err));
    }
    if (initialized) {
        esp_err_t deinit_err = esp_wifi_deinit();
        if (deinit_err != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(80));
            deinit_err = esp_wifi_deinit();
        }
        if (deinit_err != ESP_OK) ESP_LOGW("transfer", "wifi scan deinit: %s", esp_err_to_name(deinit_err));
    }
    if (scan_netif) esp_netif_destroy_default_wifi(scan_netif);
    // Keep the process-wide default event loop alive. Other screens reuse it;
    // deleting and recreating it around every scan caused stale hosted events.
    if (err != ESP_OK) { memset(out, 0, sizeof(*out) * READ_PICO_TRANSFER_SCAN_MAX); *count = 0; }
    ESP_LOGI("transfer", "wifi scan result=%s count=%u", esp_err_to_name(err), (unsigned)*count);
    ble_pt_network_release();
    release_config();
    return err;
}

esp_err_t read_pico_transfer_forget_wifi(void) {
    if (!cancel_time_sync(true)) return ESP_ERR_INVALID_STATE;
    read_pico_transfer_status_t status;
    read_pico_transfer_get_status(&status);
    // 启动失败会保留 ERROR 提示，但已释放驱动；此时仍允许用户清除坏配置。
    // A failed start retains its ERROR message after releasing the driver; allow clearing its credentials.
    if (status.state == READ_PICO_TRANSFER_UPLOADING ||
        (status.mode == READ_PICO_TRANSFER_MODE_STA && (s_wifi || s_started || s_http || s_netif)))
        return ESP_ERR_INVALID_STATE;
    if (!claim_config()) return ESP_ERR_INVALID_STATE;
    esp_err_t err = store_credentials(NULL);
    if (err == ESP_OK) {
        publish_credentials(NULL);
        ESP_LOGI("transfer", "wifi saved configured=0");
    }
    release_config(); return err;
}

static void set_error(esp_err_t err) {
    portENTER_CRITICAL(&s_lock);
    if (s_status.mode == READ_PICO_TRANSFER_MODE_STA && s_connection.failed) err = ESP_ERR_TIMEOUT;
    s_status.last_error = err;
    s_status.state = err == ESP_OK ? (s_status.network_ready ? READ_PICO_TRANSFER_READY : READ_PICO_TRANSFER_STARTING) : READ_PICO_TRANSFER_ERROR;
    portEXIT_CRITICAL(&s_lock);
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    int64_t now = esp_timer_get_time() / 1000;
    bool disconnected = false;
    portENTER_CRITICAL(&s_lock);
    if (!s_stopping && base == WIFI_EVENT) {
        if (id == WIFI_EVENT_AP_STACONNECTED) s_status.sta_count++;
        if (id == WIFI_EVENT_AP_STADISCONNECTED && s_status.sta_count) s_status.sta_count--;
        if (s_status.mode == READ_PICO_TRANSFER_MODE_STA && id == WIFI_EVENT_STA_DISCONNECTED) {
            disconnected = true;
            connection_lost(&s_connection, now);
            s_status.network_ready = false; s_status.url[0] = 0;
            if (s_status.state != READ_PICO_TRANSFER_UPLOADING && !s_connection.failed)
                s_status.state = READ_PICO_TRANSFER_STARTING;
        }
    }
    portEXIT_CRITICAL(&s_lock);
    if (disconnected) {
        const wifi_event_sta_disconnected_t *event = data;
        ESP_LOGW("transfer", "sta disconnected reason=%u", event ? (unsigned)event->reason : 0);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI("transfer", "ap station connected internal free=%u largest=%u",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        const wifi_event_ap_stadisconnected_t *event = data;
        ESP_LOGW("transfer", "ap station disconnected reason=%u internal free=%u largest=%u",
                 event ? (unsigned)event->reason : 0,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
}

static void ip_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)base;
    if (id == IP_EVENT_STA_LOST_IP) {
        int64_t now = esp_timer_get_time() / 1000;
        portENTER_CRITICAL(&s_lock);
        bool reconnect = !s_stopping && !s_connection.failed;
        if (reconnect) {
            connection_lost(&s_connection, now);
            s_status.network_ready = false; s_status.url[0] = 0;
            if (s_status.state != READ_PICO_TRANSFER_UPLOADING) s_status.state = READ_PICO_TRANSFER_STARTING;
        }
        portEXIT_CRITICAL(&s_lock);
        if (reconnect) {
            ESP_LOGW("transfer", "sta lost IP");
            esp_wifi_disconnect();
        }
        return;
    }
    if (id != IP_EVENT_STA_GOT_IP) return;
    ip_event_got_ip_t *event = data;
    if (event->esp_netif != s_netif) return;
    char url[64]; snprintf(url, sizeof(url), "http://" IPSTR, IP2STR(&event->ip_info.ip));
    portENTER_CRITICAL(&s_lock);
    bool accepted = !s_stopping && !s_connection.failed;
    if (accepted) {
        s_connection.online = true; s_connection.pending = false;
        s_status.network_ready = true; strcpy(s_status.url, url);
        s_status.last_error = ESP_OK;
        if (s_status.state != READ_PICO_TRANSFER_UPLOADING) s_status.state = READ_PICO_TRANSFER_READY;
    }
    portEXIT_CRITICAL(&s_lock);
    if (accepted) ESP_LOGI("transfer", "sta ready url=%s", url);
}

void read_pico_transfer_service_poll(void) {
    if (!s_started || s_cfg.mode != READ_PICO_TRANSFER_MODE_STA) return;
    int64_t now = esp_timer_get_time() / 1000;
    portENTER_CRITICAL(&s_lock);
    int action = s_stopping ? 0 : connection_poll(&s_connection, now);
    unsigned attempts = s_connection.attempts;
    if (action < 0) {
        s_status.network_ready = false; s_status.url[0] = 0;
        if (s_status.state != READ_PICO_TRANSFER_UPLOADING) s_status.state = READ_PICO_TRANSFER_ERROR;
        s_status.last_error = ESP_ERR_TIMEOUT;
    }
    portEXIT_CRITICAL(&s_lock);
    if (action < 0) {
        ESP_LOGW("transfer", "sta connection timeout attempts=%u", attempts);
        esp_wifi_disconnect();
    }
    if (action == 1) {
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK) {
            portENTER_CRITICAL(&s_lock);
            connection_lost(&s_connection, now); s_status.last_error = err;
            portEXIT_CRITICAL(&s_lock);
        }
    }
}

static esp_err_t respond_error(httpd_req_t *req, int code) {
    ESP_LOGW("transfer", "request failed status=%d", code);
    const char *status = code == 413 ? "413 Content Too Large" : code == 507 ? "507 Insufficient Storage" :
                         code == 408 ? "408 Request Timeout" : code == 409 ? "409 Conflict" :
                         code == 404 ? "404 Not Found" : code == 500 ? "500 Internal Server Error" : "400 Bad Request";
    const char *body = code == 413 ? "{\"error\":\"文件超过单文件上限\"}" :
                       code == 507 ? "{\"error\":\"存储空间不足或写入失败\"}" :
                       code == 408 ? "{\"error\":\"连接中断或接收超时，请重试\"}" :
                       code == 409 ? "{\"error\":\"同名文件已存在，请选择替换或跳过\",\"conflict\":true}" :
                       code == 404 ? "{\"error\":\"图书不存在\"}" :
                       code == 500 ? "{\"error\":\"操作失败，请重试\"}" : "{\"error\":\"文件名、格式或请求无效\"}";
    // 同名文件或无效请求不代表无线服务损坏，保持网页可继续上传。
    // A conflict or malformed request must not turn a healthy radio service into an error page.
    set_error(code >= 500 || code == 408 ? (code == 408 ? ESP_ERR_TIMEOUT : ESP_FAIL) : ESP_OK);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_sendstr(req, body);
    // 未消费的请求体不得被解析为下一个请求。/ Never parse an unread body as the next request.
    return ESP_FAIL;
}

static esp_err_t index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, upload_start, upload_end - upload_start - 1);
}

static esp_err_t info_handler(httpd_req_t *req) {
    read_pico_transfer_status_t status;
    read_pico_transfer_get_status(&status);
    cJSON *json = cJSON_CreateObject();
    if (!json) return ESP_ERR_NO_MEM;
    cJSON_AddBoolToObject(json, "is_flash", s_cfg.is_flash);
    cJSON_AddNumberToObject(json, "free_bytes", (double)s_cfg.free_bytes_cb(s_cfg.free_bytes_ctx));
    cJSON_AddNumberToObject(json, "file_limit", (double)s_cfg.file_limit);
    cJSON_AddStringToObject(json, "mode", status.mode == READ_PICO_TRANSFER_MODE_AP ? "ap" : "sta");
    cJSON_AddBoolToObject(json, "wifi_configured", status.wifi_configured);
    cJSON_AddStringToObject(json, "wifi_ssid", status.wifi_ssid);
    cJSON_AddStringToObject(json, "root", s_root);
    char *body = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!body) return ESP_ERR_NO_MEM;
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(req, body);
    cJSON_free(body); return err;
}

static esp_err_t wifi_response(httpd_req_t *req, const char *status, const char *body) {
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_set_status(req, status);
    esp_err_t err = httpd_resp_sendstr(req, body);
    // 成功响应要让 HTTPD 正常完成，否则浏览器可能把已保存的配置当成网络错误。
    // Complete successful requests normally; a handler failure can hide saved WiFi settings from the browser.
    return strncmp(status, "200", 3) == 0 ? err : ESP_FAIL;
}

static bool management_request(httpd_req_t *req) {
    char origin[96], host[64], host_port[72], origin_port[72];
    read_pico_transfer_status_t status;
    read_pico_transfer_get_status(&status);
    if (!status.network_ready || strncmp(status.url, "http://", 7)) return false;
    snprintf(host_port, sizeof(host_port), "%.56s:80", status.url + 7);
    snprintf(origin_port, sizeof(origin_port), "%.63s:80", status.url);
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK ||
        (strcmp(host, status.url + 7) && strcmp(host, host_port))) return false;
    if (httpd_req_get_hdr_value_len(req, "Origin")) {
        if (httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) != ESP_OK ||
            (strcmp(origin, status.url) && strcmp(origin, origin_port))) return false;
    }
    return true;
}

static bool provisioning_request(httpd_req_t *req) {
    if (s_cfg.mode != READ_PICO_TRANSFER_MODE_AP || !management_request(req)) return false;
    char content_type[80];
    return httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type)) == ESP_OK &&
        !strncasecmp(content_type, "application/json", 16) &&
        (!content_type[16] || content_type[16] == ';' || content_type[16] == ' ');
}

static bool receive_stopped(void *ctx) {
    (void)ctx;
    portENTER_CRITICAL(&s_lock); bool stopping = s_stopping; portEXIT_CRITICAL(&s_lock);
    return stopping;
}

static int64_t receive_now(void *ctx) { (void)ctx; return esp_timer_get_time() / 1000; }
static int receive_body(void *ctx, char *buf, size_t n) { return httpd_req_recv(ctx, buf, n); }

static esp_err_t wifi_handler(httpd_req_t *req) {
    if (!provisioning_request(req))
        return wifi_response(req, "403 Forbidden", "{\"error\":\"请在设备热点页面配置网络\"}");
    if (req->method == HTTP_DELETE) {
        if (req->content_len) return wifi_response(req, "400 Bad Request", "{\"error\":\"遗忘请求不应包含正文\"}");
        esp_err_t err = read_pico_transfer_forget_wifi();
        return wifi_response(req, err == ESP_OK ? "200 OK" : "500 Internal Server Error",
            err == ESP_OK ? "{\"ok\":true}" : "{\"error\":\"无法遗忘网络，请重试\"}");
    }
    if (!req->content_len || req->content_len > 256)
        return wifi_response(req, "400 Bad Request", "{\"error\":\"网络配置正文长度无效\"}");
    char body[257] = {0};
    if (!receive_credentials_body(body, req->content_len, receive_body, receive_stopped, receive_now, req)) {
        clear_secret(body, sizeof(body));
        return wifi_response(req, "408 Request Timeout", "{\"error\":\"接收配置超时，请重试\"}");
    }
    transfer_credentials_t next;
    bool valid = parse_credentials(body, req->content_len, &next);
    clear_secret(body, sizeof(body));
    if (!valid) return wifi_response(req, "400 Bad Request", "{\"error\":\"SSID须为1至32字节；密码留空，或8至63个ASCII字符、64位十六进制密钥\"}");
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (claim_config()) {
        err = store_credentials(&next);
        if (err == ESP_OK) {
            publish_credentials(&next);
            ESP_LOGI("transfer", "wifi saved configured=1");
        }
        release_config();
    }
    clear_secret(&next, sizeof(next));
    return wifi_response(req, err == ESP_OK ? "200 OK" : "500 Internal Server Error",
        err == ESP_OK ? "{\"ok\":true}" : "{\"error\":\"保存网络失败，原配置未主动清除，请重试\"}");
}

static int receive_http(void *ctx, char *buf, size_t n) {
    portENTER_CRITICAL(&s_lock); bool stopping = s_stopping; portEXIT_CRITICAL(&s_lock);
    if (stopping) return -1;
    return httpd_req_recv(ctx, buf, n);
}

static void upload_progress(size_t n) {
    portENTER_CRITICAL(&s_lock); s_status.cur_bytes = n; portEXIT_CRITICAL(&s_lock);
}

static bool decode_query(const char *src, char out[121]) {
    size_t n = 0;
    while (*src) {
        unsigned char c = (unsigned char)*src++;
        if (c == '%') {
            if (!src[0] || !src[1]) return false;
            int a = hex_value(src[0]), b = hex_value(src[1]);
            if (a < 0 || b < 0) return false;
            c = (unsigned char)((a << 4) | b); src += 2;
        }
        if (!c || n == 64) return false;
        out[n++] = (char)c;
    }
    out[n] = 0; return true;
}

static esp_err_t send_json(httpd_req_t *req, cJSON *json) {
    if (!json) return ESP_ERR_NO_MEM;
    char *body = cJSON_PrintUnformatted(json); cJSON_Delete(json);
    if (!body) return ESP_ERR_NO_MEM;
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(req, body); cJSON_free(body); return err;
}

static esp_err_t file_response(httpd_req_t *req, file_result_t result, bool deletion, const char *path) {
    if (result.progress_cleanup_failed || result.storage_cleanup_failed) {
        set_error(result.progress_cleanup_failed ? ESP_FAIL : ESP_OK);
        cJSON *json = cJSON_CreateObject();
        if (!json) return ESP_ERR_NO_MEM;
        const char *name = strrchr(path, '/');
        cJSON_AddStringToObject(json, "name", name ? name + 1 : path);
        cJSON_AddBoolToObject(json, deletion ? "deleted" : "committed", true);
        cJSON_AddBoolToObject(json, "ok", !result.progress_cleanup_failed);
        if (result.progress_cleanup_failed) {
            cJSON_AddBoolToObject(json, "progress_cleanup_failed", true);
            cJSON_AddStringToObject(json, "error", deletion ? "文件已删除，但阅读进度清理失败；请重试清理进度" :
                "文件已保存，但旧阅读进度清理失败；请重试清理进度");
            httpd_resp_set_status(req, "500 Internal Server Error");
        }
        if (result.storage_cleanup_failed) {
            cJSON_AddBoolToObject(json, "storage_cleanup_failed", true);
            cJSON_AddStringToObject(json, "warning", "新书已保存，旧书备份清理失败；备份已保留，请勿重复上传");
            ESP_LOGW("transfer", "committed with retained backup");
        }
        return send_json(req, json);
    }
    if (result.status != 200) return respond_error(req, result.status);
    set_error(ESP_OK);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t books_handler(httpd_req_t *req) {
    if (!management_request(req)) return wifi_response(req, "403 Forbidden", "{\"error\":\"请从设备显示的地址打开管理页面\"}");
    char query[1024] = {0}, encoded[766], name[256] = {0}, path[448];
    if (httpd_req_get_url_query_len(req) && httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) return respond_error(req, 400);
    esp_err_t name_err = httpd_query_key_value(query, "name", encoded, sizeof(encoded));
    if (name_err != ESP_OK && name_err != ESP_ERR_NOT_FOUND) return respond_error(req, 400);
    bool named = name_err == ESP_OK;
    if (named && !decode_name_limit(encoded, name, sizeof(name), false)) return respond_error(req, 400);
    snprintf(path, sizeof(path), "%s/%s", s_root, name);
    if (req->method == HTTP_GET) {
        cJSON *json = cJSON_CreateObject();
        if (!json) return ESP_ERR_NO_MEM;
        cJSON_AddStringToObject(json, "root", s_root);
        cJSON_AddStringToObject(json, "root_label", s_cfg.is_flash ? "内置存储" : "TF 卡");
        if (named) {
            struct stat st;
            if (stat(path, &st)) {
                int result = errno == ENOENT ? 404 : 507; cJSON_Delete(json);
                if (result == 404) return wifi_response(req, "404 Not Found", "{\"error\":\"图书不存在\"}");
                return respond_error(req, result);
            }
            if (!S_ISREG(st.st_mode)) { cJSON_Delete(json); return respond_error(req, 400); }
            cJSON *item = cJSON_AddObjectToObject(json, "item");
            cJSON_AddStringToObject(item, "name", name); cJSON_AddNumberToObject(item, "size", (double)st.st_size);
        } else {
            char search[121] = {0}, page_text[16] = "0";
            esp_err_t search_err = httpd_query_key_value(query, "q", encoded, sizeof(encoded));
            if ((search_err != ESP_OK && search_err != ESP_ERR_NOT_FOUND) || (search_err == ESP_OK && !decode_query(encoded, search))) {
                cJSON_Delete(json); return respond_error(req, 400);
            }
            esp_err_t page_err = httpd_query_key_value(query, "page", page_text, sizeof(page_text));
            if (page_err != ESP_OK && page_err != ESP_ERR_NOT_FOUND) { cJSON_Delete(json); return respond_error(req, 400); }
            char *end; unsigned long page = strtoul(page_text, &end, 10);
            if (!page_text[0] || *end || page_text[0] == '-' || page > 1000000) { cJSON_Delete(json); return respond_error(req, 400); }
            transfer_book_page_t *entries = calloc(1, sizeof(*entries));
            if (!entries) { cJSON_Delete(json); return ESP_ERR_NO_MEM; }
            int result = list_books(s_root, search, page, entries);
            if (result) { free(entries); cJSON_Delete(json); return respond_error(req, result); }
            cJSON_AddNumberToObject(json, "page", page); cJSON_AddNumberToObject(json, "total", entries->total);
            cJSON_AddNumberToObject(json, "pages", (entries->total + BOOK_LIST_PAGE_SIZE - 1) / BOOK_LIST_PAGE_SIZE);
            cJSON *items = cJSON_AddArrayToObject(json, "items");
            for (size_t i = 0; i < entries->count; ++i) {
                cJSON *item = cJSON_CreateObject(); cJSON_AddItemToArray(items, item);
                cJSON_AddStringToObject(item, "name", entries->items[i].name); cJSON_AddNumberToObject(item, "size", (double)entries->items[i].size);
            }
            free(entries);
        }
        return send_json(req, json);
    }
    if (!named || req->content_len) return respond_error(req, 400);
    char action[32];
    bool retry = req->method == HTTP_POST && httpd_query_key_value(query, "action", action, sizeof(action)) == ESP_OK && !strcmp(action, "retry_progress");
    if (req->method != HTTP_DELETE && !retry) return respond_error(req, 400);
    int resolved = resolve_mutation_path(s_root, name, path, sizeof(path));
    if (resolved) return respond_error(req, resolved);
    portENTER_CRITICAL(&s_lock);
    bool accepted = admission_begin(s_stopping, &s_upload_active);
    portEXIT_CRITICAL(&s_lock);
    if (!accepted) return wifi_response(req, "409 Conflict", "{\"error\":\"正在上传或切换网络，请稍后重试\"}");
    file_result_t result = retry ? (file_result_t){.status = !s_cfg.file_changed_cb || s_cfg.file_changed_cb(path) == ESP_OK ? 200 : 500} :
        delete_managed(path, s_cfg.file_changed_cb);
    portENTER_CRITICAL(&s_lock);
    record_file_change(&s_status.changed_count, result, retry);
    s_upload_active = false;
    portEXIT_CRITICAL(&s_lock);
    return file_response(req, result, true, path);
}

// 编辑的只是书架显示名；root 由服务器白名单决定，网页不能传任意路径。
// Only the shelf display title changes; the server selects roots from a fixed allowlist.
static const char *title_root(const char *choice) {
    if (!strcmp(choice, "legacy") && !s_cfg.is_flash) return "/sdcard/book";
    if (!strcmp(choice, "books") && !s_cfg.is_flash) return s_cfg.root_dir;
    if (!strcmp(choice, "flash")) return "/flash/books";
    return NULL;
}
static bool recv_small_body(httpd_req_t *req, char *body, size_t cap) {
    if (req->content_len + 1 > cap) return false;
    size_t done = 0;
    while (done < req->content_len) {
        int got = httpd_req_recv(req, body + done, req->content_len - done);
        if (got <= 0) return false;
        done += got;
    }
    body[done] = 0;
    return true;
}
// 两种传书模式共用签名接口，沿用文件操作的忙碌与停止保护。
// Both transfer modes share this endpoint and the existing mutation/stop admission guard.
static esp_err_t signature_handler(httpd_req_t *req) {
    if (!management_request(req)) return respond_error(req, 403);
    if (!s_cfg.signature_get_cb || !s_cfg.signature_set_cb) return respond_error(req, 503);
    char text[96] = {0};
    if (req->method == HTTP_POST && (req->content_len > 95 ||
        !recv_small_body(req, text, sizeof(text)) || !transfer_signature_valid(text, req->content_len)))
        return wifi_response(req, "400 Bad Request", "{\"error\":\"签名最多95字节，不能含换行或控制字符\"}");
    portENTER_CRITICAL(&s_lock);
    bool accepted = admission_begin(s_stopping, &s_upload_active);
    portEXIT_CRITICAL(&s_lock);
    if (!accepted) return wifi_response(req, "409 Conflict", "{\"error\":\"正在传输，请完成后再设置签名\"}");
    esp_err_t err = req->method == HTTP_POST ? s_cfg.signature_set_cb(text) :
                   s_cfg.signature_get_cb(text, sizeof(text)) ? ESP_OK : ESP_FAIL;
    portENTER_CRITICAL(&s_lock); s_upload_active = false; portEXIT_CRITICAL(&s_lock);
    if (err != ESP_OK) return wifi_response(req, "500 Internal Server Error", "{\"error\":\"签名保存或读取失败，请重试\"}");
    cJSON *json = cJSON_CreateObject();
    if (!json) return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(json, "signature", text);
    cJSON_AddBoolToObject(json, "ok", true);
    return send_json(req, json);
}

static esp_err_t clock_handler(httpd_req_t *req) {
    if (!management_request(req)) return respond_error(req, 403);
    if (req->content_len < 12 || req->content_len > 48) return respond_error(req, 400);
    char body[49];
    if (!recv_small_body(req, body, sizeof(body))) return respond_error(req, 400);
    cJSON *json = cJSON_Parse(body);
    const cJSON *epoch = json ? cJSON_GetObjectItemCaseSensitive(json, "epoch") : NULL;
    if (!cJSON_IsNumber(epoch) || epoch->valuedouble < 1704067200 || epoch->valuedouble > 4102444800.0) {
        cJSON_Delete(json); return respond_error(req, 400);
    }
    struct timeval tv = {.tv_sec = (time_t)epoch->valuedouble};
    cJSON_Delete(json);
    if (settimeofday(&tv, NULL)) return respond_error(req, 500);
    return wifi_response(req, "200 OK", "{\"ok\":true}");
}
static esp_err_t titles_handler(httpd_req_t *req) {
    if (!management_request(req)) return wifi_response(req, "403 Forbidden", "{\"error\":\"请从设备显示的地址打开管理页面\"}");
    if (!s_cfg.title_get_cb || !s_cfg.title_set_cb) return respond_error(req, 503);
    char query[1024] = {0}, choice[16] = {0}, encoded[766], name[256] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "root", choice, sizeof(choice)) != ESP_OK) return respond_error(req, 400);
    const char *root = title_root(choice);
    if (!root) return respond_error(req, 400);
    if (req->method == HTTP_GET) {
        char page_text[16] = "0";
        esp_err_t page_err = httpd_query_key_value(query, "page", page_text, sizeof(page_text));
        if (page_err != ESP_OK && page_err != ESP_ERR_NOT_FOUND) return respond_error(req, 400);
        char *end; unsigned long page = strtoul(page_text, &end, 10);
        if (!page_text[0] || *end || page_text[0] == '-' || page > 1000000) return respond_error(req, 400);
        struct stat dir_st;
        if (stat(root, &dir_st) || !S_ISDIR(dir_st.st_mode)) return respond_error(req, 404);
        transfer_book_page_t *entries = heap_caps_calloc(1, sizeof(*entries), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!entries) return respond_error(req, 503);
        int result = list_books(root, "", page, entries);
        if (result) { heap_caps_free(entries); return respond_error(req, result); }
        cJSON *json = cJSON_CreateObject();
        if (!json) { heap_caps_free(entries); return ESP_ERR_NO_MEM; }
        cJSON_AddNumberToObject(json, "total", entries->total);
        cJSON_AddNumberToObject(json, "page", page);
        cJSON_AddNumberToObject(json, "pages", (entries->total + BOOK_LIST_PAGE_SIZE - 1) / BOOK_LIST_PAGE_SIZE);
        cJSON *items = cJSON_AddArrayToObject(json, "items");
        if (!items) { heap_caps_free(entries); cJSON_Delete(json); return ESP_ERR_NO_MEM; }
        for (size_t i = 0; i < entries->count; ++i) {
            char path[448], title[121] = {0};
            if (snprintf(path, sizeof(path), "%s/%s", root, entries->items[i].name) >= (int)sizeof(path)) continue;
            s_cfg.title_get_cb(path, title, sizeof(title));
            cJSON *item = cJSON_CreateObject();
            if (!item) { heap_caps_free(entries); cJSON_Delete(json); return ESP_ERR_NO_MEM; }
            cJSON_AddItemToArray(items, item);
            cJSON_AddStringToObject(item, "name", entries->items[i].name);
            cJSON_AddStringToObject(item, "title", title);
        }
        heap_caps_free(entries);
        return send_json(req, json);
    }
    if (req->method != HTTP_POST || req->content_len < 2 || req->content_len > 192 ||
        httpd_query_key_value(query, "name", encoded, sizeof(encoded)) != ESP_OK ||
        !decode_name_limit(encoded, name, sizeof(name), false)) return respond_error(req, 400);
    char path[448];
    int resolved = resolve_mutation_path(root, name, path, sizeof(path));
    if (resolved) return respond_error(req, resolved);
    struct stat st;
    if (stat(path, &st) || !S_ISREG(st.st_mode)) return respond_error(req, 404);
    char body[193];
    if (!recv_small_body(req, body, sizeof(body))) return respond_error(req, 400);
    cJSON *json = cJSON_Parse(body);
    const cJSON *title = json ? cJSON_GetObjectItemCaseSensitive(json, "title") : NULL;
    if (!cJSON_IsString(title) || !title->valuestring) { cJSON_Delete(json); return respond_error(req, 400); }
    portENTER_CRITICAL(&s_lock);
    bool accepted = admission_begin(s_stopping, &s_upload_active);
    portEXIT_CRITICAL(&s_lock);
    if (!accepted) { cJSON_Delete(json); return respond_error(req, 409); }
    esp_err_t err = s_cfg.title_set_cb(path, title->valuestring);
    portENTER_CRITICAL(&s_lock);
    s_upload_active = false;
    portEXIT_CRITICAL(&s_lock);
    cJSON_Delete(json);
    if (err != ESP_OK) return respond_error(req, err == ESP_ERR_INVALID_ARG ? 400 : 500);
    return wifi_response(req, "200 OK", "{\"ok\":true}");
}

static esp_err_t upload_handler(httpd_req_t *req) {
    if (!management_request(req)) return wifi_response(req, "403 Forbidden", "{\"error\":\"请从设备显示的地址打开管理页面\"}");
    char query[512], encoded[361], name[121], path[288], part[296], replace[8];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "name", encoded, sizeof(encoded)) != ESP_OK || !decode_name(encoded, name))
        return respond_error(req, 400);
    int resolved = resolve_mutation_path(s_root, name, path, sizeof(path));
    if (resolved) return respond_error(req, resolved);
    snprintf(part, sizeof(part), "%s.part", path);
    bool overwrite = httpd_query_key_value(query, "overwrite", replace, sizeof(replace)) == ESP_OK && !strcmp(replace, "1");
    portENTER_CRITICAL(&s_lock);
    if (!admission_begin(s_stopping, &s_upload_active)) {
        portEXIT_CRITICAL(&s_lock);
        return wifi_response(req, "503 Service Unavailable", "{\"error\":\"网络正在切换，请连接后重试\"}");
    }
    s_status.state = READ_PICO_TRANSFER_UPLOADING; s_status.last_error = ESP_OK;
    memcpy(s_status.cur_name, name, strlen(name) + 1);
    s_status.cur_bytes = 0; s_status.cur_total = req->content_len;
    portEXIT_CRITICAL(&s_lock);
    int64_t started = esp_timer_get_time();
    ESP_LOGI("transfer", "receive name=%s bytes=%u", name, (unsigned)req->content_len);
    file_result_t result = upload_managed(path, part, req->content_len, s_cfg.file_limit,
        s_cfg.free_bytes_cb(s_cfg.free_bytes_ctx), overwrite, s_buffer, 16384, receive_http, req, upload_progress, s_cfg.file_changed_cb);
    portENTER_CRITICAL(&s_lock);
    s_upload_active = false;
    if (result.changed) { s_status.done_count++; s_status.changed_count++; }
    portEXIT_CRITICAL(&s_lock);
    if (result.changed) ESP_LOGI("transfer", "committed name=%s bytes=%u elapsed_ms=%lld", name,
             (unsigned)req->content_len, (esp_timer_get_time() - started) / 1000);
    return file_response(req, result, false, path);
}

static esp_err_t media_upload_handler(httpd_req_t *req, const char *root, unsigned kind, size_t limit) {
    if (!management_request(req)) return wifi_response(req, "403 Forbidden", "{\"error\":\"请从设备显示的地址打开管理页面\"}");
    if (s_cfg.is_flash) return wifi_response(req, "400 Bad Request", "{\"error\":\"请插入 TF 卡后重试\"}");
    char query[512], encoded[361], name[121], path[320], part[328], replace[8];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "name", encoded, sizeof(encoded)) != ESP_OK ||
        !decode_name_limit(encoded, name, sizeof(name), kind)) return respond_error(req, 400);
    if (mkdir(root, 0775) && errno != EEXIST) return respond_error(req, 507);
    int resolved = resolve_mutation_path(root, name, path, sizeof(path));
    if (resolved) return respond_error(req, resolved);
    snprintf(part, sizeof(part), "%s.part", path);
    bool overwrite = httpd_query_key_value(query, "overwrite", replace, sizeof(replace)) == ESP_OK && !strcmp(replace, "1");
    char wallpaper[8] = {0};
    bool set_wallpaper = kind == 1 && httpd_query_key_value(query, "wallpaper", wallpaper, sizeof(wallpaper)) == ESP_OK && !strcmp(wallpaper, "1");
    if (set_wallpaper && !s_cfg.wallpaper_set_cb)
        return wifi_response(req, "503 Service Unavailable", "{\"error\":\"锁屏壁纸设置不可用\"}");
    portENTER_CRITICAL(&s_lock);
    if (!admission_begin(s_stopping, &s_upload_active)) {
        portEXIT_CRITICAL(&s_lock);
        return wifi_response(req, "503 Service Unavailable", "{\"error\":\"网络正在切换，请稍后重试\"}");
    }
    s_status.state = READ_PICO_TRANSFER_UPLOADING; s_status.last_error = ESP_OK;
    memcpy(s_status.cur_name, name, strlen(name) + 1);
    s_status.cur_bytes = 0; s_status.cur_total = req->content_len;
    portEXIT_CRITICAL(&s_lock);
    file_result_t result = upload_managed(path, part, req->content_len,
        set_wallpaper && limit > 2u * 1024u * 1024u ? 2u * 1024u * 1024u : limit,
        s_cfg.free_bytes_cb(s_cfg.free_bytes_ctx), overwrite, s_buffer, 16384, receive_http, req, upload_progress, NULL);
    bool wallpaper_failed = set_wallpaper && result.status == 200 && result.changed && !s_cfg.wallpaper_set_cb(path);
    portENTER_CRITICAL(&s_lock);
    s_upload_active = false;
    if (result.changed) { s_status.done_count++; s_status.changed_count++; }
    portEXIT_CRITICAL(&s_lock);
    if (wallpaper_failed)
        return wifi_response(req, "500 Internal Server Error", "{\"error\":\"图片已上传，但设置锁屏壁纸失败\"}");
    return file_response(req, result, false, path);
}

static esp_err_t image_upload_handler(httpd_req_t *req) {
    return media_upload_handler(req, "/sdcard/pictures", 1, IMAGE_UPLOAD_LIMIT);
}

static esp_err_t font_upload_handler(httpd_req_t *req) {
    return media_upload_handler(req, "/sdcard/fonts", 2, FONT_UPLOAD_LIMIT);
}

static esp_err_t file_upload_handler(httpd_req_t *req) {
    if (!management_request(req)) return wifi_response(req, "403 Forbidden", "{\"error\":\"请从设备显示的地址打开\"}");
    if (s_cfg.is_flash) return wifi_response(req, "400 Bad Request", "{\"error\":\"请插入 TF 卡后重试\"}");
    char query[800], encoded[720], relative[240], replace[8], path[256];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "path", encoded, sizeof(encoded)) != ESP_OK ||
        !sd_decode_relative(encoded, relative, false) || !sd_absolute(relative, path, sizeof(path)))
        return respond_error(req, 400);
    bool overwrite = httpd_query_key_value(query, "overwrite", replace, sizeof(replace)) == ESP_OK && !strcmp(replace, "1");
    const char *name = strrchr(relative, '/'); name = name ? name + 1 : relative;
    portENTER_CRITICAL(&s_lock);
    if (!admission_begin(s_stopping, &s_upload_active)) {
        portEXIT_CRITICAL(&s_lock);
        return wifi_response(req, "503 Service Unavailable", "{\"error\":\"网络正在切换，请稍后重试\"}");
    }
    s_status.state = READ_PICO_TRANSFER_UPLOADING; s_status.last_error = ESP_OK;
    snprintf(s_status.cur_name, sizeof(s_status.cur_name), "%s", name);
    s_status.cur_bytes = 0; s_status.cur_total = req->content_len;
    portEXIT_CRITICAL(&s_lock);
    file_result_t result = upload_directory_file("/sdcard", relative, req->content_len,
        s_cfg.free_bytes_cb(s_cfg.free_bytes_ctx), overwrite, s_buffer, 16384,
        receive_http, req, upload_progress, s_cfg.file_changed_cb);
    portENTER_CRITICAL(&s_lock);
    s_upload_active = false;
    if (result.changed) { ++s_status.done_count; ++s_status.changed_count; }
    portEXIT_CRITICAL(&s_lock);
    return file_response(req, result, false, path);
}


static esp_err_t files_error(httpd_req_t *req, const char *status, const char *message) {
    return wifi_response(req, status, message);
}

#define TRANSFER_FILE_PAGE_SIZE 20
typedef struct { char name[241]; uint64_t size; bool directory; } transfer_file_entry_t;

static esp_err_t files_list_handler(httpd_req_t *req) {
    if (!management_request(req)) return files_error(req, "403 Forbidden", "{\"error\":\"请从设备显示的地址打开\"}");
    if (s_cfg.is_flash) return files_error(req, "400 Bad Request", "{\"error\":\"TF 卡尚未就绪\"}");
    char query[800] = {0}, encoded[720] = {0}, relative[240] = {0}, path[256];
    if (httpd_req_get_url_query_len(req) && httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK)
        return files_error(req, "400 Bad Request", "{\"error\":\"目录地址过长\"}");
    esp_err_t path_result = httpd_query_key_value(query, "path", encoded, sizeof(encoded));
    if (path_result != ESP_OK && path_result != ESP_ERR_NOT_FOUND)
        return files_error(req, "400 Bad Request", "{\"error\":\"目录地址无效\"}");
    if (path_result == ESP_OK && !sd_decode_relative(encoded, relative, true))
        return files_error(req, "400 Bad Request", "{\"error\":\"目录地址无效\"}");
    if (!sd_absolute(relative, path, sizeof(path)))
        return files_error(req, "400 Bad Request", "{\"error\":\"目录地址过长\"}");
    char page_text[12] = {0};
    size_t page = 0;
    if (httpd_query_key_value(query, "page", page_text, sizeof(page_text)) == ESP_OK) {
        char *end = NULL;
        unsigned long value = strtoul(page_text, &end, 10);
        if (!page_text[0] || *end || value > 100000)
            return files_error(req, "400 Bad Request", "{\"error\":\"页码无效\"}");
        page = value;
    }
    DIR *dir = opendir(path);
    if (!dir) return files_error(req, "404 Not Found", "{\"error\":\"目录不存在或无法读取\"}");
    transfer_file_entry_t *items = heap_caps_calloc(TRANSFER_FILE_PAGE_SIZE, sizeof(*items), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!items) { closedir(dir); return files_error(req, "500 Internal Server Error", "{\"error\":\"内存不足\"}"); }
    size_t total = 0, count = 0;
    for (int pass = 0; pass < 2; ++pass) {
        rewinddir(dir);
        struct dirent *entry;
        while ((entry = readdir(dir))) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
            size_t len = strlen(entry->d_name);
            if (!len || len >= sizeof(items[0].name)) continue;
            char child[512];
            if (snprintf(child, sizeof(child), "%s/%s", path, entry->d_name) >= (int)sizeof(child)) continue;
            struct stat st;
            if (stat(child, &st) || (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode)) ||
                (S_ISDIR(st.st_mode) != (pass == 0))) continue;
            if (total >= page * TRANSFER_FILE_PAGE_SIZE && count < TRANSFER_FILE_PAGE_SIZE) {
                transfer_file_entry_t *item = &items[count++];
                memcpy(item->name, entry->d_name, len + 1);
                item->directory = S_ISDIR(st.st_mode);
                item->size = S_ISREG(st.st_mode) && st.st_size > 0 ? (uint64_t)st.st_size : 0;
            }
            ++total;
        }
    }
    closedir(dir);
    cJSON *json = cJSON_CreateObject(), *array = cJSON_CreateArray();
    if (!json || !array) { cJSON_Delete(json); cJSON_Delete(array); free(items); return ESP_ERR_NO_MEM; }
    cJSON_AddStringToObject(json, "path", relative);
    cJSON_AddNumberToObject(json, "total", total);
    cJSON_AddNumberToObject(json, "page", page);
    cJSON_AddNumberToObject(json, "pages", (total + TRANSFER_FILE_PAGE_SIZE - 1) / TRANSFER_FILE_PAGE_SIZE);
    cJSON_AddItemToObject(json, "items", array);
    for (size_t i = 0; i < count; ++i) {
        cJSON *entry = cJSON_CreateObject();
        if (!entry) continue;
        cJSON_AddStringToObject(entry, "name", items[i].name);
        cJSON_AddBoolToObject(entry, "directory", items[i].directory);
        cJSON_AddNumberToObject(entry, "size", (double)items[i].size);
        cJSON_AddItemToArray(array, entry);
    }
    free(items);
    return send_json(req, json);
}

static int copy_sd_file(const char *source, const char *target, uint64_t free_bytes) {
    struct stat st;
    if (stat(source, &st) || !S_ISREG(st.st_mode) || st.st_size < 0) return 400;
    if ((uint64_t)st.st_size > free_bytes) return 507;
    char part[280];
    if (snprintf(part, sizeof(part), "%s.part", target) >= (int)sizeof(part)) return 400;
    if (!stat(part, &st) || errno != ENOENT) return 409;
    FILE *in = fopen(source, "rb");
    if (!in) return 507;
    FILE *out = fopen(part, "wb");
    if (!out) { fclose(in); return 507; }
    int status = 200;
    while (!feof(in)) {
        size_t n = fread(s_buffer, 1, 16384, in);
        if (n && fwrite(s_buffer, 1, n, out) != n) { status = 507; break; }
        if (ferror(in)) { status = 507; break; }
    }
    if (fclose(in)) status = 507;
    if (fclose(out)) status = 507;
    if (status == 200 && rename(part, target)) status = 507;
    if (status != 200) remove(part);
    return status;
}

static void forget_sd_book(const char *path) {
    if (s_cfg.file_deleted_cb) { s_cfg.file_deleted_cb(path); return; }
    const char *ext = strrchr(path, '.');
    if (s_cfg.file_changed_cb && ext && (!strcasecmp(ext, ".epub") || !strcasecmp(ext, ".txt")))
        (void)s_cfg.file_changed_cb(path);
}

#define TRANSFER_TREE_MAX_DEPTH 8
#define TRANSFER_TREE_MAX_NODES 1000
static int inspect_sd_tree(const char *path, unsigned depth, unsigned *nodes, uint64_t *bytes) {
    if (depth > TRANSFER_TREE_MAX_DEPTH || ++*nodes > TRANSFER_TREE_MAX_NODES) return 400;
    struct stat st;
    if (stat(path, &st)) return 507;
    if (S_ISREG(st.st_mode)) { *bytes += st.st_size > 0 ? (uint64_t)st.st_size : 0; return 200; }
    if (!S_ISDIR(st.st_mode)) return 400;
    DIR *dir = opendir(path);
    if (!dir) return 507;
    char *child = heap_caps_malloc(512, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!child) { closedir(dir); return 507; }
    int status = 200;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (snprintf(child, 512, "%s/%s", path, entry->d_name) >= 512 ||
            (status = inspect_sd_tree(child, depth + 1, nodes, bytes)) != 200) break;
    }
    free(child);
    if (closedir(dir)) status = 507;
    return status;
}

static int delete_sd_tree(const char *path, unsigned depth) {
    if (depth > TRANSFER_TREE_MAX_DEPTH) return 400;
    struct stat st;
    if (stat(path, &st)) return 507;
    if (S_ISREG(st.st_mode)) {
        if (unlink(path)) return 507;
        forget_sd_book(path);
        return 200;
    }
    if (!S_ISDIR(st.st_mode)) return 400;
    DIR *dir = opendir(path);
    if (!dir) return 507;
    char *child = heap_caps_malloc(512, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!child) { closedir(dir); return 507; }
    int status = 200;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (snprintf(child, 512, "%s/%s", path, entry->d_name) >= 512 ||
            (status = delete_sd_tree(child, depth + 1)) != 200) break;
    }
    free(child);
    if (closedir(dir)) status = 507;
    if (status == 200 && rmdir(path)) status = 507;
    if (status == 200 && s_cfg.directory_deleted_cb) s_cfg.directory_deleted_cb(path);
    return status;
}

static int copy_sd_tree(const char *source, const char *target, unsigned depth) {
    if (depth > TRANSFER_TREE_MAX_DEPTH) return 400;
    struct stat st;
    if (stat(source, &st)) return 507;
    if (S_ISREG(st.st_mode)) return copy_sd_file(source, target, s_cfg.free_bytes_cb(s_cfg.free_bytes_ctx));
    if (!S_ISDIR(st.st_mode) || mkdir(target, 0775)) return 507;
    DIR *dir = opendir(source);
    if (!dir) { rmdir(target); return 507; }
    char *names = heap_caps_malloc(1024, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!names) { closedir(dir); rmdir(target); return 507; }
    char *from = names, *to = names + 512;
    int status = 200;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (snprintf(from, 512, "%s/%s", source, entry->d_name) >= 512 ||
            snprintf(to, 512, "%s/%s", target, entry->d_name) >= 512 ||
            (status = copy_sd_tree(from, to, depth + 1)) != 200) break;
    }
    free(names);
    if (closedir(dir)) status = 507;
    if (status != 200) (void)delete_sd_tree(target, depth);
    return status;
}

static void notify_sd_move_tree(const char *old_path, const char *new_path, unsigned depth) {
    if (!s_cfg.file_moved_cb || depth > TRANSFER_TREE_MAX_DEPTH) return;
    struct stat st;
    if (stat(new_path, &st)) return;
    if (S_ISREG(st.st_mode)) {
        s_cfg.file_moved_cb(old_path, new_path,
                            st.st_size >= 0 && (uint64_t)st.st_size <= UINT32_MAX ? (uint32_t)st.st_size : 0);
        return;
    }
    if (!S_ISDIR(st.st_mode)) return;
    if (s_cfg.directory_moved_cb) s_cfg.directory_moved_cb(old_path, new_path);
    DIR *dir = opendir(new_path);
    if (!dir) return;
    char *names = heap_caps_malloc(1024, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!names) { closedir(dir); return; }
    char *old_child = names, *new_child = names + 512;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (snprintf(old_child, 512, "%s/%s", old_path, entry->d_name) >= 512 ||
            snprintf(new_child, 512, "%s/%s", new_path, entry->d_name) >= 512) continue;
        notify_sd_move_tree(old_child, new_child, depth + 1);
    }
    free(names);
    closedir(dir);
}

static bool flat_file_request(const char *body) {
    bool quoted = false, escaped = false;
    unsigned depth = 0;
    for (const char *p = body; *p; ++p) {
        if (quoted) {
            if (escaped) escaped = false;
            else if (*p == '\\') escaped = true;
            else if (*p == '"') quoted = false;
        } else if (*p == '"') quoted = true;
        else if (*p == '{' || *p == '[') { if (++depth > 1) return false; }
        else if (*p == '}' || *p == ']') { if (!depth) return false; --depth; }
    }
    return !quoted && depth == 0;
}

static bool same_or_below(const char *path, const char *directory) {
    size_t n = strlen(directory);
    return !strncmp(path, directory, n) && (!path[n] || path[n] == '/');
}

static void sync_upload_root_after_edit(const char *action, const char *source, const char *target) {
    if (!s_root[0] || !same_or_below(s_root, source)) return;
    if (!strcmp(action, "delete")) {
        (void)mkdir("/sdcard/books", 0775);
        strlcpy(s_root, "/sdcard/books", sizeof(s_root));
    } else if (!strcmp(action, "rename")) {
        char updated[sizeof(s_root)];
        if (snprintf(updated, sizeof(updated), "%s%s", target, s_root + strlen(source)) < (int)sizeof(updated))
            strlcpy(s_root, updated, sizeof(s_root));
        else {
            (void)mkdir("/sdcard/books", 0775);
            strlcpy(s_root, "/sdcard/books", sizeof(s_root));
        }
    }
}

static esp_err_t files_mutation_handler(httpd_req_t *req) {
    if (!management_request(req)) return files_error(req, "403 Forbidden", "{\"error\":\"请从设备显示的地址打开\"}");
    if (s_cfg.is_flash) return files_error(req, "400 Bad Request", "{\"error\":\"TF 卡尚未就绪\"}");
    char body[800] = {0};
    if (!req->content_len || !recv_small_body(req, body, sizeof(body)) ||
        strstr(body, "\\u0000") || !flat_file_request(body))
        return files_error(req, "400 Bad Request", "{\"error\":\"操作参数无效\"}");
    cJSON *json = cJSON_Parse(body);
    const cJSON *action = json ? cJSON_GetObjectItemCaseSensitive(json, "action") : NULL;
    const cJSON *from = json ? cJSON_GetObjectItemCaseSensitive(json, "path") : NULL;
    const cJSON *to = json ? cJSON_GetObjectItemCaseSensitive(json, "target") : NULL;
    if (!cJSON_IsString(action) || !cJSON_IsString(from) ||
        !sd_relative_valid(from->valuestring, !strcmp(action->valuestring, "mkdir")) ||
        (!strcmp(action->valuestring, "delete") ? false :
         (!cJSON_IsString(to) || !sd_relative_valid(to->valuestring, false)))) {
        cJSON_Delete(json);
        return files_error(req, "400 Bad Request", "{\"error\":\"文件路径无效\"}");
    }
    char source[256], target[256] = {0};
    bool has_target = strcmp(action->valuestring, "delete") != 0;
    if (!sd_absolute(from->valuestring, source, sizeof(source)) ||
        (has_target && !sd_absolute(to->valuestring, target, sizeof(target)))) {
        cJSON_Delete(json);
        return files_error(req, "400 Bad Request", "{\"error\":\"文件路径过长\"}");
    }
    bool accepted;
    portENTER_CRITICAL(&s_lock);
    accepted = admission_begin(s_stopping, &s_upload_active);
    if (accepted) s_status.state = READ_PICO_TRANSFER_UPLOADING;
    portEXIT_CRITICAL(&s_lock);
    if (!accepted) { cJSON_Delete(json); return files_error(req, "503 Service Unavailable", "{\"error\":\"设备正忙，请稍后重试\"}"); }
    int status = 400;
    bool may_have_changed = false;
    struct stat st;
    bool source_exists = stat(source, &st) == 0;
    if (!strcmp(action->valuestring, "delete") && from->valuestring[0] && source_exists) {
        unsigned nodes = 0; uint64_t bytes = 0;
        status = inspect_sd_tree(source, 0, &nodes, &bytes);
        if (status == 200) {
            may_have_changed = true;
            status = delete_sd_tree(source, 0);
            if (status == 200) sync_upload_root_after_edit("delete", source, target);
        }
    } else if (!strcmp(action->valuestring, "mkdir") && !source_exists) {
        status = 404;
    } else if (!strcmp(action->valuestring, "mkdir") && !stat(target, &st)) {
        status = 409;
    } else if (!strcmp(action->valuestring, "mkdir")) {
        status = mkdir(target, 0775) == 0 ? 200 : 507;
    } else if ((!strcmp(action->valuestring, "rename") || !strcmp(action->valuestring, "copy")) && source_exists) {
        struct stat dest;
        if (!strcmp(source, target) || !stat(target, &dest)) status = 409;
        else if (errno != ENOENT) status = 507;
        else if (!strcmp(action->valuestring, "rename")) {
            if (S_ISDIR(st.st_mode) && !strncmp(target, source, strlen(source)) && target[strlen(source)] == '/')
                status = 400;
            else {
                unsigned nodes = 0; uint64_t bytes = 0;
                status = S_ISDIR(st.st_mode) ? inspect_sd_tree(source, 0, &nodes, &bytes) : 200;
                if (status == 200) status = rename(source, target) == 0 ? 200 : 507;
            }
            if (status == 200) {
                notify_sd_move_tree(source, target, 0);
                sync_upload_root_after_edit("rename", source, target);
            }
        } else {
            if (S_ISDIR(st.st_mode) && !strncmp(target, source, strlen(source)) && target[strlen(source)] == '/')
                status = 400;
            else {
                unsigned nodes = 0; uint64_t bytes = 0;
                status = inspect_sd_tree(source, 0, &nodes, &bytes);
                if (status == 200 && bytes > s_cfg.free_bytes_cb(s_cfg.free_bytes_ctx)) status = 507;
                if (status == 200) { may_have_changed = true; status = copy_sd_tree(source, target, 0); }
            }
        }
    } else if (!source_exists) status = 404;
    if (status == 200 || may_have_changed) {
        portENTER_CRITICAL(&s_lock);
        s_status.changed_count++;
        portEXIT_CRITICAL(&s_lock);
    }
    portENTER_CRITICAL(&s_lock);
    s_upload_active = false;
    portEXIT_CRITICAL(&s_lock);
    set_error(ESP_OK);
    cJSON_Delete(json);
    if (status == 200) return files_error(req, "200 OK", "{\"ok\":true}");
    if (status == 404) return files_error(req, "404 Not Found", "{\"error\":\"文件或目录不存在\"}");
    if (status == 409) return files_error(req, "409 Conflict", "{\"error\":\"目标文件已存在\"}");
    if (status == 507) return files_error(req, "507 Insufficient Storage", "{\"error\":\"TF 卡空间不足或读写失败\"}");
    return files_error(req, "400 Bad Request", "{\"error\":\"当前操作不支持该文件\"}");
}

/* ---- 生命周期 / Lifecycle ---- */
bool read_pico_transfer_try_stop_if_idle(void) {
    if (!cancel_time_sync(false)) return false;
    portENTER_CRITICAL(&s_lock);
    bool accepted = admission_stop(&s_stopping, s_upload_active);
    portEXIT_CRITICAL(&s_lock);
    if (accepted) read_pico_transfer_stop();
    return accepted;
}

bool read_pico_transfer_pause_for_sleep(void) {
    if (!cancel_time_sync(false)) return false;
    read_pico_transfer_status_t status;
    read_pico_transfer_get_status(&status);
    if (status.state == READ_PICO_TRANSFER_STOPPED) return true;
    if (status.state == READ_PICO_TRANSFER_UPLOADING) return false;
    read_pico_transfer_cfg_t saved = s_cfg;
    if (saved.root_dir) {
        strlcpy(s_sleep_root, saved.root_dir, sizeof(s_sleep_root));
        saved.root_dir = s_sleep_root;
    }
    if (!read_pico_transfer_try_stop_if_idle()) return false;
    s_sleep_cfg = saved;
    s_sleep_paused = true;
    return true;
}

void read_pico_transfer_resume_after_sleep(void) {
    if (!s_sleep_paused) return;
    s_sleep_paused = false;
    esp_err_t err = read_pico_transfer_start(&s_sleep_cfg);
    if (err != ESP_OK) ESP_LOGW("transfer", "resume after sleep: %s", esp_err_to_name(err));
}

void read_pico_transfer_set_rf_blocked(bool blocked) {
    s_rf_blocked = blocked;
}
bool read_pico_transfer_rf_blocked(void) { return s_rf_blocked; }

void read_pico_transfer_stop(void) {
    if (!cancel_time_sync(false)) return;
    portENTER_CRITICAL(&s_lock); s_stopping = true; portEXIT_CRITICAL(&s_lock);
    if (s_http) { httpd_stop(s_http); s_http = NULL; }
    if (s_started) { esp_wifi_stop(); s_started = false; }
    if (s_events) { esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_events); s_events = NULL; }
    if (s_ip_events) { esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, s_ip_events); s_ip_events = NULL; }
    if (s_wifi) { esp_wifi_deinit(); s_wifi = false; }
    if (s_netif) { esp_netif_destroy_default_wifi(s_netif); s_netif = NULL; }
    if (s_loop_owned) { esp_event_loop_delete_default(); s_loop_owned = false; }
    free(s_buffer); s_buffer = NULL;
    if (s_ble_network_reserved) { s_ble_network_reserved = false; ble_pt_network_release(); }
    portENTER_CRITICAL(&s_lock);
    s_status.state = READ_PICO_TRANSFER_STOPPED; s_status.sta_count = 0;
    s_status.network_ready = false; s_status.url[0] = 0;
    s_upload_active = false;
    memset(&s_connection, 0, sizeof(s_connection));
    portEXIT_CRITICAL(&s_lock);
}

esp_err_t read_pico_transfer_start(const read_pico_transfer_cfg_t *cfg) {
    if (!cfg || (cfg->network_only && cfg->mode != READ_PICO_TRANSFER_MODE_STA) ||
        (!cfg->network_only && (!cfg->root_dir || !cfg->free_bytes_cb)) ||
        (cfg->root_dir && strlen(cfg->root_dir) >= sizeof(s_root)) ||
        (cfg->mode != READ_PICO_TRANSFER_MODE_AP && cfg->mode != READ_PICO_TRANSFER_MODE_STA)) return ESP_ERR_INVALID_ARG;
    if (!cancel_time_sync(true)) return ESP_ERR_INVALID_STATE;
    // 飞行模式：保留凭据，只是不让射频起来。/ Flight mode: keep credentials, never raise the radio.
    if (s_rf_blocked) return ESP_ERR_INVALID_STATE;
    if (!claim_config()) return ESP_ERR_INVALID_STATE;
    if (s_wifi || s_netif || s_http) { release_config(); return ESP_ERR_INVALID_STATE; }
    // 先回收 BLE 内存，再创建 WiFi/HTTP；失败不半途启动。/ Reclaim BLE before WiFi/HTTP allocation; never start partially on a failed stop.
    const esp_err_t reserve = ble_pt_network_acquire();
    if (reserve != ESP_OK) { release_config(); return reserve; }
    s_ble_network_reserved = true;
    s_cfg = *cfg;
    if (cfg->root_dir) strcpy(s_root, cfg->root_dir);
    else s_root[0] = 0;
    s_cfg.root_dir = s_root;
    portENTER_CRITICAL(&s_lock);
    memset(&s_status, 0, sizeof(s_status)); s_status.state = READ_PICO_TRANSFER_STARTING;
    s_stopping = s_upload_active = false;
    s_status.mode = cfg->mode;
    portEXIT_CRITICAL(&s_lock);
    transfer_credentials_t saved = {0};
    wifi_config_t wifi = {0};
    esp_err_t err = load_credentials(&saved);
    if (err != ESP_OK && cfg->mode == READ_PICO_TRANSFER_MODE_STA) goto fail;
    publish_credentials(&saved);
    if (cfg->mode == READ_PICO_TRANSFER_MODE_STA && saved.version != 1) { err = ESP_ERR_NOT_FOUND; goto fail; }
    if (!cfg->network_only) {
        struct stat st;
        if (stat(cfg->root_dir, &st) || !S_ISDIR(st.st_mode)) { err = ESP_ERR_NOT_FOUND; goto fail; }
        unsigned removed = 0, restored = 0;
        if (cleanup_interrupted(cfg->root_dir, &removed, &restored)) {
            ESP_LOGW("transfer", "cleanup failed removed=%u restored=%u", removed, restored);
            err = ESP_FAIL; goto fail;
        }
        if (removed || restored) ESP_LOGI("transfer", "cleanup removed=%u restored=%u", removed, restored);
    }
    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) goto fail;
    err = esp_event_loop_create_default();
    if (err == ESP_OK) s_loop_owned = true;
    else if (err != ESP_ERR_INVALID_STATE) goto fail;
    err = transfer_create_netif(cfg->mode == READ_PICO_TRANSFER_MODE_AP, &s_netif);
    if (err != ESP_OK) goto fail;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init); if (err != ESP_OK) goto fail;
    s_wifi = true;
    if (cfg->mode == READ_PICO_TRANSFER_MODE_AP) {
        uint8_t mac[6];
        err = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP); if (err != ESP_OK) goto fail;
        snprintf((char *)wifi.ap.ssid, sizeof(wifi.ap.ssid), "kiikoread-%02X%02X", mac[4], mac[5]);
        strcpy((char *)wifi.ap.password, READ_PICO_TRANSFER_PASSWORD);
        wifi.ap.ssid_len = strlen((char *)wifi.ap.ssid); wifi.ap.channel = 1;
        wifi.ap.max_connection = 1; wifi.ap.authmode = WIFI_AUTH_WPA2_PSK;
        portENTER_CRITICAL(&s_lock); strcpy(s_status.ssid, (char *)wifi.ap.ssid); portEXIT_CRITICAL(&s_lock);
    } else {
        memcpy(wifi.sta.ssid, saved.ssid, strlen(saved.ssid));
        memcpy(wifi.sta.password, saved.password, strlen(saved.password));
        // Accept WPA/WPA2/WPA3 personal networks selected by the scanner. The
        // old WPA2 threshold silently rejected older WPA mixed-mode routers.
        wifi.sta.threshold.authmode = saved.password[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
        wifi.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        wifi.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
        wifi.sta.pmf_cfg.capable = true;
        wifi.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    }
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL, &s_events);
    if (err != ESP_OK) goto fail;
    if (cfg->mode == READ_PICO_TRANSFER_MODE_STA) {
        err = esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, ip_event, NULL, &s_ip_events);
        if (err != ESP_OK) goto fail;
    }
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM); if (err != ESP_OK) goto fail;
    err = esp_wifi_set_mode(cfg->mode == READ_PICO_TRANSFER_MODE_AP ? WIFI_MODE_AP : WIFI_MODE_STA); if (err != ESP_OK) goto fail;
    err = esp_wifi_set_config(cfg->mode == READ_PICO_TRANSFER_MODE_AP ? WIFI_IF_AP : WIFI_IF_STA, &wifi); if (err != ESP_OK) goto fail;
    clear_secret(&wifi, sizeof(wifi)); clear_secret(&saved, sizeof(saved));
    if (!cfg->network_only) {
        s_buffer = heap_caps_malloc(16384, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_buffer) { err = ESP_ERR_NO_MEM; goto fail; }
    }
    if (!cfg->network_only) {
    httpd_config_t http = HTTPD_DEFAULT_CONFIG();
    // 文件元数据和壁纸设置会访问 NVS；HTTP 栈必须在禁用缓存时仍可读取的内部内存。
    // File metadata and wallpaper settings access NVS; the HTTP stack must remain readable while caches are disabled.
    // 在无线电启动前预留网页任务，避免 WiFi 内存碎片导致任务创建失败。
    // Reserve the HTTP task before starting the radio to avoid WiFi heap fragmentation.
    http.stack_size = 12288; http.max_uri_handlers = 18; http.max_open_sockets = 3;
    http.recv_wait_timeout = 15;
    http.send_wait_timeout = 15;
    http.task_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    http.lru_purge_enable = true;
    ESP_LOGI("transfer", "http start internal free=%u largest=%u psram free=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    err = httpd_start(&s_http, &http);
    if (err != ESP_OK) goto fail;
    ESP_LOGI("transfer", "http ready internal free=%u largest=%u psram free=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    const httpd_uri_t routes[] = {
        { .uri = "/", .method = HTTP_GET, .handler = index_handler },
        { .uri = "/info", .method = HTTP_GET, .handler = info_handler },
        { .uri = "/upload", .method = HTTP_PUT, .handler = upload_handler },
        { .uri = "/image-upload", .method = HTTP_PUT, .handler = image_upload_handler },
        { .uri = "/font-upload", .method = HTTP_PUT, .handler = font_upload_handler },
        { .uri = "/file-upload", .method = HTTP_PUT, .handler = file_upload_handler },
        { .uri = "/files", .method = HTTP_GET, .handler = files_list_handler },
        { .uri = "/files", .method = HTTP_POST, .handler = files_mutation_handler },
        { .uri = "/books", .method = HTTP_GET, .handler = books_handler },
        { .uri = "/books", .method = HTTP_DELETE, .handler = books_handler },
        { .uri = "/books", .method = HTTP_POST, .handler = books_handler },
        { .uri = "/signature", .method = HTTP_GET, .handler = signature_handler },
        { .uri = "/signature", .method = HTTP_POST, .handler = signature_handler },
        { .uri = "/clock", .method = HTTP_POST, .handler = clock_handler },
        { .uri = "/titles", .method = HTTP_GET, .handler = titles_handler },
        { .uri = "/titles", .method = HTTP_POST, .handler = titles_handler },
        { .uri = "/wifi", .method = HTTP_POST, .handler = wifi_handler },
        { .uri = "/wifi", .method = HTTP_DELETE, .handler = wifi_handler },
    };
    for (size_t i = 0; i < sizeof(routes)/sizeof(*routes); ++i) {
        err = httpd_register_uri_handler(s_http, &routes[i]); if (err != ESP_OK) goto fail;
    }
    }
    err = esp_wifi_start(); if (err != ESP_OK) goto fail;
    s_started = true;
    if (cfg->mode == READ_PICO_TRANSFER_MODE_STA && !cfg->network_only) {
        esp_err_t power_err = esp_wifi_set_ps(WIFI_PS_NONE);
        if (power_err != ESP_OK) ESP_LOGW("transfer", "disable wifi power save: %s", esp_err_to_name(power_err));
    }
    if (cfg->mode == READ_PICO_TRANSFER_MODE_AP) {
        portENTER_CRITICAL(&s_lock);
        s_status.network_ready = true; strcpy(s_status.url, READ_PICO_TRANSFER_URL);
        portEXIT_CRITICAL(&s_lock);
        set_error(ESP_OK);
        ESP_LOGI("transfer", "ap ready url=%s", READ_PICO_TRANSFER_URL);
    } else {
        int64_t now = esp_timer_get_time() / 1000;
        portENTER_CRITICAL(&s_lock); connection_begin(&s_connection, now); portEXIT_CRITICAL(&s_lock);
        read_pico_transfer_service_poll();
    }
    release_config();
    return ESP_OK;
fail:
    clear_secret(&wifi, sizeof(wifi)); clear_secret(&saved, sizeof(saved));
    read_pico_transfer_stop(); release_config(); set_error(err); return err;
}
#endif
