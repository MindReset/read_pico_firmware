/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 热点与已有WiFi、HTTP接收和临时文件提交；容量策略由页面注入。
 * AP/STA networking, HTTP reception and file commit; page injects capacity policy.
 * 冻结：图书 TXT/EPUB；为完整字库部署增加独立 TF 字体目录的 TTF 上传。
 * Frozen: TXT/EPUB books; deploy complete fonts through TTF uploads to a separate TF font directory.
 * 冻结：UTF-8 名字不转写；先验限额，字体提交前校验，失败清理临时文件。
 * Frozen: Preserve UTF-8 names; check limits first, validate fonts before commit and remove failed parts.
 * 冻结：热点网页或停服设备触屏可配置网络，不自动切模式；凭据只存单个NVS blob，状态不含密码。
 * Frozen: AP webpage or stopped-service device UI may provision without switching mode; one NVS blob holds secrets outside public status.
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
#include <dirent.h>
#include "transfer_font.h"

/* ---- 请求与存储 / Requests and storage ---- */
static int hex_value(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool decode_name_type(const char *encoded, char *out, size_t cap, bool font) {
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
    if (!ext || (font ? strcasecmp(ext, ".ttf") : (strcasecmp(ext, ".txt") && strcasecmp(ext, ".epub")))) return false;
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

static bool decode_name_limit(const char *encoded, char *out, size_t cap) { return decode_name_type(encoded, out, cap, false); }
static bool decode_name(const char *encoded, char out[121]) { return decode_name_limit(encoded, out, 121); }
static bool decode_font_name(const char *encoded, char out[121]) { return decode_name_type(encoded, out, 121, true); }

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
    return decode_name_limit(encoded, name, cap);
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
    return decode_name(encoded, name) || decode_font_name(encoded, name);
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
    const char *ext = strrchr(path, '.');
    if (!error && ext && !strcasecmp(ext, ".ttf") && !valid_ttf(part)) error = 422;
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
#include "transfer_policy.h"
#include "transfer_credentials_store.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static read_pico_transfer_status_t s_status;
static read_pico_transfer_cfg_t s_cfg;
static char s_root[160];
static char s_font_root[160];
static httpd_handle_t s_http;
static esp_netif_t *s_netif;
static esp_event_handler_instance_t s_events, s_ip_events;
static bool s_wifi, s_started, s_loop_owned;
static char *s_buffer;
static bool s_stopping, s_upload_active;
static bool s_config_busy;
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

esp_err_t read_pico_transfer_save_wifi(const char *ssid, const char *password) {
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

esp_err_t read_pico_transfer_scan_wifi(read_pico_transfer_network_t out[READ_PICO_TRANSFER_SCAN_MAX], size_t *count) {
    if (!out || !count) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out) * READ_PICO_TRANSFER_SCAN_MAX); *count = 0;
    read_pico_transfer_status_t status;
    read_pico_transfer_get_status(&status);
    if (s_wifi || s_http || s_netif || status.state != READ_PICO_TRANSFER_STOPPED) return ESP_ERR_INVALID_STATE;
    bool loop_owned = false, initialized = false, started = false;
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if (err == ESP_OK) loop_owned = true;
    else if (err != ESP_ERR_INVALID_STATE) return err;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init); if (err != ESP_OK) goto cleanup;
    initialized = true;
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM); if (err != ESP_OK) goto cleanup;
    err = esp_wifi_set_mode(WIFI_MODE_STA); if (err != ESP_OK) goto cleanup;
    // 仅驱动扫描，不创建STA网络接口或注册自动连接回调。/ Driver-only scan: no STA netif or automatic-connect callback.
    err = esp_wifi_start(); if (err != ESP_OK) goto cleanup;
    started = true;
    wifi_scan_config_t scan = {.show_hidden = false, .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active = {.min = 100, .max = 200}};
    err = esp_wifi_scan_start(&scan, true); if (err != ESP_OK) goto cleanup;
    uint16_t found = 0;
    err = esp_wifi_scan_get_ap_num(&found); if (err != ESP_OK) goto cleanup;
    for (uint16_t i = 0; i < found; ++i) {
        wifi_ap_record_t ap;
        err = esp_wifi_scan_get_ap_record(&ap); if (err != ESP_OK) goto cleanup;
        read_pico_transfer_network_t item = {.rssi = ap.rssi, .authmode = (uint8_t)ap.authmode,
            .requires_password = ap.authmode != WIFI_AUTH_OPEN && ap.authmode != WIFI_AUTH_OWE};
        memcpy(item.ssid, ap.ssid, sizeof(item.ssid) - 1);
        item.supported = ap.authmode == WIFI_AUTH_OPEN || ap.authmode == WIFI_AUTH_WPA2_PSK ||
            ap.authmode == WIFI_AUTH_WPA_WPA2_PSK || ap.authmode == WIFI_AUTH_WPA3_PSK ||
            ap.authmode == WIFI_AUTH_WPA2_WPA3_PSK;
        scan_offer(out, count, &item);
    }
cleanup:
    if (started) { esp_wifi_scan_stop(); esp_wifi_clear_ap_list(); }
    if (started) {
        esp_err_t stop_err = esp_wifi_stop();
        if (err == ESP_OK && stop_err != ESP_OK) err = stop_err;
    }
    if (initialized) {
        esp_err_t deinit_err = esp_wifi_deinit();
        if (err == ESP_OK && deinit_err != ESP_OK) err = deinit_err;
    }
    if (loop_owned) {
        esp_err_t loop_err = esp_event_loop_delete_default();
        if (err == ESP_OK && loop_err != ESP_OK) err = loop_err;
    }
    if (err != ESP_OK) { memset(out, 0, sizeof(*out) * READ_PICO_TRANSFER_SCAN_MAX); *count = 0; }
    ESP_LOGI("transfer", "wifi scan result=%s count=%u", esp_err_to_name(err), (unsigned)*count);
    return err;
}

esp_err_t read_pico_transfer_forget_wifi(void) {
    read_pico_transfer_status_t status;
    read_pico_transfer_get_status(&status);
    if (status.state == READ_PICO_TRANSFER_UPLOADING ||
        (status.state != READ_PICO_TRANSFER_STOPPED && status.mode == READ_PICO_TRANSFER_MODE_STA)) return ESP_ERR_INVALID_STATE;
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
    const char *status = code == 422 ? "422 Unprocessable Content" : code == 413 ? "413 Content Too Large" : code == 507 ? "507 Insufficient Storage" :
                         code == 408 ? "408 Request Timeout" : code == 409 ? "409 Conflict" :
                         code == 404 ? "404 Not Found" : code == 500 ? "500 Internal Server Error" : "400 Bad Request";
    const char *body = code == 422 ? "{\"error\":\"字体校验失败，需要完整的 TrueType TTF（不支持 OTF/CFF、TTC 或 WOFF）\"}" :
                       code == 413 ? "{\"error\":\"文件超过单文件上限\"}" :
                       code == 507 ? "{\"error\":\"存储空间不足或写入失败\"}" :
                       code == 408 ? "{\"error\":\"连接中断或接收超时，请重试\"}" :
                       code == 409 ? "{\"error\":\"同名文件已存在，请明确选择替换或跳过\",\"conflict\":true}" :
                       code == 404 ? "{\"error\":\"文件不存在\"}" :
                       code == 500 ? "{\"error\":\"操作失败，请重试\"}" : "{\"error\":\"文件名或请求无效：图书仅 TXT/EPUB，字体仅 TTF\"}";
    set_error(code == 408 ? ESP_ERR_TIMEOUT : code == 507 ? ESP_FAIL : ESP_ERR_INVALID_ARG);
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
    cJSON_AddBoolToObject(json, "fonts_enabled", s_font_root[0] != 0);
    cJSON_AddStringToObject(json, "font_root", s_font_root);
    cJSON_AddNumberToObject(json, "font_limit", TRANSFER_FONT_MAX);
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
    httpd_resp_sendstr(req, body);
    return ESP_FAIL;
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
            cJSON_AddStringToObject(json, "warning", "文件已保存，旧文件备份清理失败；备份已保留，请勿重复上传");
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
    if (named && !decode_name_limit(encoded, name, sizeof(name))) return respond_error(req, 400);
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

static esp_err_t upload_handler(httpd_req_t *req) {
    if (!management_request(req)) return wifi_response(req, "403 Forbidden", "{\"error\":\"请从设备显示的地址打开管理页面\"}");
    char query[512], encoded[361], name[121], path[288], part[296], replace[8];
    bool font = req->user_ctx != NULL;
    if (font && !s_font_root[0]) return wifi_response(req, "409 Conflict", "{\"error\":\"字体需要 TF 卡，请挂载后重新进入传书\"}");
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "name", encoded, sizeof(encoded)) != ESP_OK ||
        !(font ? decode_font_name(encoded, name) : decode_name(encoded, name)))
        return respond_error(req, 400);
    int resolved = resolve_mutation_path(font ? s_font_root : s_root, name, path, sizeof(path));
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
    file_result_t result = upload_managed(path, part, req->content_len, font ? TRANSFER_FONT_MAX : s_cfg.file_limit,
        s_cfg.free_bytes_cb(s_cfg.free_bytes_ctx), overwrite, s_buffer, 16384, receive_http, req, upload_progress,
        font ? NULL : s_cfg.file_changed_cb);
    portENTER_CRITICAL(&s_lock);
    s_upload_active = false;
    if (result.changed) { s_status.done_count++; s_status.changed_count++; }
    portEXIT_CRITICAL(&s_lock);
    if (result.changed) ESP_LOGI("transfer", "committed name=%s bytes=%u elapsed_ms=%lld", name,
             (unsigned)req->content_len, (esp_timer_get_time() - started) / 1000);
    return file_response(req, result, false, path);
}

static esp_err_t font_info_handler(httpd_req_t *req) {
    if (!management_request(req)) return wifi_response(req, "403 Forbidden", "{\"error\":\"请从设备显示的地址打开管理页面\"}");
    if (!s_font_root[0]) return wifi_response(req, "409 Conflict", "{\"error\":\"字体需要 TF 卡，请挂载后重新进入传书\"}");
    char query[512], encoded[361], name[121], path[288];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "name", encoded, sizeof(encoded)) != ESP_OK || !decode_font_name(encoded, name))
        return respond_error(req, 400);
    int resolved = resolve_mutation_path(s_font_root, name, path, sizeof(path));
    if (resolved) return respond_error(req, resolved);
    struct stat st;
    if (stat(path, &st)) return respond_error(req, errno == ENOENT ? 404 : 507);
    if (!S_ISREG(st.st_mode) || st.st_size < 0) return respond_error(req, 400);
    cJSON *json = cJSON_CreateObject();
    if (!json) return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(json, "name", strrchr(path, '/') + 1);
    cJSON_AddNumberToObject(json, "size", (double)st.st_size);
    return send_json(req, json);
}

/* ---- 生命周期 / Lifecycle ---- */
bool read_pico_transfer_try_stop_if_idle(void) {
    portENTER_CRITICAL(&s_lock);
    bool accepted = admission_stop(&s_stopping, s_upload_active);
    portEXIT_CRITICAL(&s_lock);
    if (accepted) read_pico_transfer_stop();
    return accepted;
}

void read_pico_transfer_stop(void) {
    portENTER_CRITICAL(&s_lock); s_stopping = true; portEXIT_CRITICAL(&s_lock);
    if (s_http) { httpd_stop(s_http); s_http = NULL; }
    if (s_started) { esp_wifi_stop(); s_started = false; }
    if (s_events) { esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_events); s_events = NULL; }
    if (s_ip_events) { esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, s_ip_events); s_ip_events = NULL; }
    if (s_wifi) { esp_wifi_deinit(); s_wifi = false; }
    if (s_netif) { esp_netif_destroy_default_wifi(s_netif); s_netif = NULL; }
    if (s_loop_owned) { esp_event_loop_delete_default(); s_loop_owned = false; }
    free(s_buffer); s_buffer = NULL;
    portENTER_CRITICAL(&s_lock);
    s_status.state = READ_PICO_TRANSFER_STOPPED; s_status.sta_count = 0;
    s_status.network_ready = false; s_status.url[0] = 0;
    s_upload_active = false;
    memset(&s_connection, 0, sizeof(s_connection));
    portEXIT_CRITICAL(&s_lock);
}

esp_err_t read_pico_transfer_start(const read_pico_transfer_cfg_t *cfg) {
    if (!cfg || !cfg->root_dir || !cfg->free_bytes_cb || strlen(cfg->root_dir) >= sizeof(s_root) ||
        (cfg->font_dir && (cfg->is_flash || !cfg->font_dir[0] || strlen(cfg->font_dir) >= sizeof(s_font_root))) ||
        (cfg->mode != READ_PICO_TRANSFER_MODE_AP && cfg->mode != READ_PICO_TRANSFER_MODE_STA)) return ESP_ERR_INVALID_ARG;
    if (s_wifi || s_netif || s_http) return ESP_ERR_INVALID_STATE;
    s_cfg = *cfg; strcpy(s_root, cfg->root_dir); s_cfg.root_dir = s_root;
    snprintf(s_font_root, sizeof(s_font_root), "%s", cfg->font_dir ? cfg->font_dir : "");
    s_cfg.font_dir = s_font_root[0] ? s_font_root : NULL;
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
    struct stat st;
    if (stat(cfg->root_dir, &st) || !S_ISDIR(st.st_mode)) { err = ESP_ERR_NOT_FOUND; goto fail; }
    unsigned removed = 0, restored = 0;
    if (cleanup_interrupted(cfg->root_dir, &removed, &restored)) {
        ESP_LOGW("transfer", "cleanup failed removed=%u restored=%u", removed, restored);
        err = ESP_FAIL; goto fail;
    }
    if (s_font_root[0]) {
        if ((mkdir(s_font_root, 0755) && errno != EEXIST) || stat(s_font_root, &st) || !S_ISDIR(st.st_mode) ||
            cleanup_interrupted(s_font_root, &removed, &restored)) { err = ESP_FAIL; goto fail; }
    }
    if (removed || restored) ESP_LOGI("transfer", "cleanup removed=%u restored=%u", removed, restored);
    err = esp_netif_init();
    if (err != ESP_OK) goto fail;
    err = esp_event_loop_create_default();
    if (err == ESP_OK) s_loop_owned = true;
    else if (err != ESP_ERR_INVALID_STATE) goto fail;
    s_netif = cfg->mode == READ_PICO_TRANSFER_MODE_AP ? esp_netif_create_default_wifi_ap() : esp_netif_create_default_wifi_sta();
    if (!s_netif) { err = ESP_ERR_NO_MEM; goto fail; }
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init); if (err != ESP_OK) goto fail;
    s_wifi = true;
    if (cfg->mode == READ_PICO_TRANSFER_MODE_AP) {
        uint8_t mac[6];
        err = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP); if (err != ESP_OK) goto fail;
        snprintf((char *)wifi.ap.ssid, sizeof(wifi.ap.ssid), "ReadPico-%02X%02X", mac[4], mac[5]);
        strcpy((char *)wifi.ap.password, READ_PICO_TRANSFER_PASSWORD);
        wifi.ap.ssid_len = strlen((char *)wifi.ap.ssid); wifi.ap.channel = 1;
        wifi.ap.max_connection = 2; wifi.ap.authmode = WIFI_AUTH_WPA2_PSK;
        portENTER_CRITICAL(&s_lock); strcpy(s_status.ssid, (char *)wifi.ap.ssid); portEXIT_CRITICAL(&s_lock);
    } else {
        memcpy(wifi.sta.ssid, saved.ssid, strlen(saved.ssid));
        memcpy(wifi.sta.password, saved.password, strlen(saved.password));
        wifi.sta.threshold.authmode = saved.password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
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
    s_buffer = heap_caps_malloc(16384, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_buffer) { err = ESP_ERR_NO_MEM; goto fail; }
    err = esp_wifi_start(); if (err != ESP_OK) goto fail;
    s_started = true;
    httpd_config_t http = HTTPD_DEFAULT_CONFIG();
    http.stack_size = 12288; http.max_uri_handlers = 10; http.recv_wait_timeout = 5;
    http.lru_purge_enable = true;
    err = httpd_start(&s_http, &http); if (err != ESP_OK) goto fail;
    const httpd_uri_t routes[] = {
        { .uri = "/", .method = HTTP_GET, .handler = index_handler },
        { .uri = "/info", .method = HTTP_GET, .handler = info_handler },
        { .uri = "/upload", .method = HTTP_PUT, .handler = upload_handler },
        { .uri = "/fonts", .method = HTTP_PUT, .handler = upload_handler, .user_ctx = (void *)1 },
        { .uri = "/fonts", .method = HTTP_GET, .handler = font_info_handler },
        { .uri = "/books", .method = HTTP_GET, .handler = books_handler },
        { .uri = "/books", .method = HTTP_DELETE, .handler = books_handler },
        { .uri = "/books", .method = HTTP_POST, .handler = books_handler },
        { .uri = "/wifi", .method = HTTP_POST, .handler = wifi_handler },
        { .uri = "/wifi", .method = HTTP_DELETE, .handler = wifi_handler },
    };
    for (size_t i = 0; i < sizeof(routes)/sizeof(*routes); ++i) {
        err = httpd_register_uri_handler(s_http, &routes[i]); if (err != ESP_OK) goto fail;
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
    return ESP_OK;
fail:
    clear_secret(&wifi, sizeof(wifi)); clear_secret(&saved, sizeof(saved));
    read_pico_transfer_stop(); set_error(err); return err;
}
#endif
