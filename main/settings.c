/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * NVS 读写。打开失败就用深睡默认值，不擦除整个分区。
 *
 * 用户修订：底部上滑是固定工具栏入口，短按和长按可自由设置；启动与恢复不再强制改成工具栏，提交失败仍保留原映射。
 * User revision: bottom-edge swipes provide a permanent toolbar entry, freeing all short/hold mappings. Boot/restore no longer force a toolbar key; failed commits retain previous mappings.
 * 用户修订：三键短按映射单独持久化；备份v11追加长按映射，旧备份缺少长按映射时恢复返回首页。
 * User revision: persist short key mappings; backup v11 adds a checked hold action; older backups without a hold mapping default to home. Five reading stroke levels are sealed in backup v13.
 * 用户修订：首行缩进微调-20..20px单独持久化，备份v12追加校验块；旧备份恢复0，不更改密码凭据。
 * User revision: persist a -20..20px indent adjustment; v12 adds a checked block, legacy backups default to zero and leave PIN credentials intact.
 * NVS load/store. A failed open keeps the deep-sleep default; the
 * partition is not erased.
 */

#include "settings.h"

#include <stdio.h>
#include <stddef.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "read_pico_sd.h"
#include "read_pico_transfer.h"
#include "book/book_history_backup.h"

#define TAG "settings"
#define NVS_NS "read_pico"
#define NVS_KEY_SLEEP "sleep"
#define NVS_KEY_SHUTDOWN_MODE "shutdown"
#define NVS_KEY_HOME_FULL "home_full"
#define NVS_KEY_HOME_COMPONENT "home_widget"
#define NVS_KEY_HOME_GOAL "home_goal"
#define NVS_KEY_MAIN_REFRESH "main_mode"
#define NVS_KEY_AUTO_LOCK "idle_lock"
#define NVS_KEY_DEVICE_NAME "dev_name"
#define NVS_KEY_AVATAR "dev_avatar"
#define NVS_KEY_STATUS_SIGNATURE "status_sig"
#define NVS_KEY_FONT "font"
#define NVS_KEY_SYS_FONT "sys_font"
#define NVS_KEY_SYS_SIZE "sys_size"
#define NVS_KEY_SYS_CONTRAST "sys_contrast"
#define NVS_KEY_LOCK_STYLE "lock_ui"
#define NVS_KEY_WALLPAPER "wallpaper"
#define NVS_KEY_WAKE "lwake"
#define NVS_KEY_BOOT "lboot"
#define NVS_KEY_PICKUP "pickup"
#define NVS_KEY_BOOK_PX "bk_px"
#define NVS_KEY_BOOK_WEIGHT "bk_weight"
#define NVS_KEY_BOOK_WEIGHT_FINE "bk_wfine"
#define NVS_KEY_BOOK_AA "bk_aa"
#define NVS_KEY_BOOK_BACKGROUND "bk_bg"
#define NVS_KEY_READER_FULL "rd_gc16"
#define NVS_KEY_READER_TURN "rd_turn"
#define NVS_KEY_POWER_TURN "rd_power"
#define NVS_KEY_IMMERSIVE "rd_immersive"
#define NVS_KEY_HIDE_IMAGES "rd_no_image"
#define NVS_KEY_HOLD_REFRESH "rd_hold_full"
#define NVS_KEY_HOLD_ACTION "rd_key_hold"
#define NVS_KEY_READER_KEYS_V2 "rd_keys_v2"
#define NVS_KEY_VERTICAL_TURN "rd_vertical"
#define NVS_KEY_LANDSCAPE "rd_landscape"
#define NVS_KEY_TWO_COLUMN "rd_2col"
#define NVS_KEY_BLE_TURNER "ble_turn"
#define NVS_KEY_FLIGHT "flight"
#define NVS_KEY_SHELF_RECENT "shelf_recent"
#define NVS_KEY_BOOK_LINE "bk_line"
#define NVS_KEY_BOOK_PARA "bk_para"
#define NVS_KEY_BOOK_MARGIN "bk_margin"
#define NVS_KEY_BOOK_TRACK "bk_track"
#define NVS_KEY_BOOK_INDENT "bk_indent"
#define NVS_KEY_BOOK_INDENT_ADJUST "bk_indent_adj"
#define NVS_KEY_BOOK_RULE "bk_rule"
#define NVS_KEY_BOOK_RULE_OFFSET "bk_rule_y"
#define NVS_KEY_SHELF_STYLE "shelf_ui"
#define NVS_KEY_SHELF_V22 "shelf_v22"
#define NVS_KEY_BOOKS_DIR "books_dir"
#define NVS_KEY_FONTS_DIR "fonts_dir"
#define NVS_KEY_BOOK_FONTS "bk_fonts"
#define NVS_KEY_BOOK_VERTICAL "bk_vert"
#define FONT_PATH_MAX 160
#define MEDIA_DIR_MAX 96

static app_sleep_mode_t s_sleep = APP_SLEEP_DEEP;
static bool s_staged_shutdown;
static bool s_home_full_refresh;
static app_home_component_t s_home_component = APP_HOME_FOLDER;
static uint8_t s_home_goal_minutes = 30;
static app_main_refresh_mode_t s_main_refresh;
static char s_device_name[64] = "kiikoread";
static char s_avatar[288];
static char s_status_signature[96];
static char s_font[FONT_PATH_MAX];
static char s_system_font[FONT_PATH_MAX];
static uint8_t s_system_size = 120;
static uint8_t s_system_contrast = 100;
static uint8_t s_lock_style;
static char s_wallpaper[288];
static uint8_t s_last_wake;
static uint8_t s_last_boot;
static bool s_pickup_wake;
static uint8_t s_book_px = 48;
static uint8_t s_book_weight = 2;
static int8_t s_book_weight_fine;
static bool s_book_antialias = true;
static char s_book_background[288];
static uint8_t s_reader_full_pages = 15;
static uint8_t s_reader_turn_effect;
static uint8_t s_book_line = 130, s_book_para = 50, s_book_margin = 24;
static bool s_reader_power_turn;
static bool s_reader_immersive;
static bool s_reader_hide_images;
static bool s_reader_hold_refresh;
static const char *s_reader_key_names[] = {"rd_key_l", "rd_key_m", "rd_key_r"};
static uint8_t s_reader_hold_action = APP_READER_KEY_HOME;
static uint8_t s_reader_keys[3] = {APP_READER_KEY_PREV, APP_READER_KEY_TOOLS, APP_READER_KEY_NEXT};
static bool s_reader_vertical_turn;
static bool s_reader_landscape;
static bool s_reader_two_column;
static bool s_ble_turner;
static bool s_flight_mode;
static bool s_shelf_recent_sort;
// 书内自带字体默认开启：多数带字体的书排版就是靠它才对得上版。
// Embedded book faces are on by default: most books that ship a face only typeset correctly
// with it.
static bool s_book_fonts = true;
// 竖排默认关：只有台湾、日本等竖排读者会主动打开，横排排版不受影响。
// Vertical setting defaults off: only readers who want it turn it on, and horizontal type is
// untouched.
static bool s_book_vertical;
static uint8_t s_book_tracking = 2, s_book_reading_line, s_book_rule_offset = 4;
static uint8_t s_book_indent = 2;
static uint8_t s_book_indent_adjust = 20;
static uint8_t s_auto_lock_minutes;
static uint8_t s_shelf_style = 2;
static char s_books_dir[MEDIA_DIR_MAX] = "/sdcard/books";
static char s_fonts_dir[MEDIA_DIR_MAX] = "/sdcard/fonts";
static void nvs_put_u8(const char* key, uint8_t value);

static bool valid_media_dir(const char* path) {
    if (!path || strncmp(path, "/sdcard/", 8) || !path[8] ||
        strlen(path) >= MEDIA_DIR_MAX || path[strlen(path) - 1] == '/') return false;
    const char *segment = path + 8;
    for (const unsigned char *p = (const unsigned char *)segment; *p; ++p) {
        if (*p < 32 || *p == 127 || *p == '\\' || *p == ':') return false;
        if (*p == '/' && (p == (const unsigned char *)segment || p[-1] == '/')) return false;
    }
    for (const char *p = segment; *p;) {
        const char *end = strchr(p, '/');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if ((n == 1 && p[0] == '.') || (n == 2 && p[0] == '.' && p[1] == '.')) return false;
        if (!end) break;
        p = end + 1;
    }
    return true;
}

static bool set_media_dir(char *dst, const char *key, const char *path) {
    if (!valid_media_dir(path)) return false;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err = nvs_set_str(h, key, path);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) return false;
    strlcpy(dst, path, MEDIA_DIR_MAX);
    return true;
}

static uint8_t valid_book_px(uint8_t px) {
    return px >= 36 && px <= 72 ? px : 48;
}

void app_settings_init(void) {
    esp_err_t err = nvs_flash_init();
    // 设置分区异常时保留原数据供恢复，绝不因空间不足自动擦除用户资料。
    // Preserve settings on NVS errors; a full partition must never silently erase profile data.
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs init %s, use deep sleep", esp_err_to_name(err));
        return;
    }

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    uint8_t raw = APP_SLEEP_DEEP;
    if (nvs_get_u8(h, NVS_KEY_SLEEP, &raw) == ESP_OK && raw <= APP_SLEEP_OFF) {
        s_sleep = (app_sleep_mode_t)raw;
    }
    raw = 0;
    if (nvs_get_u8(h, NVS_KEY_SHUTDOWN_MODE, &raw) == ESP_OK)
        s_staged_shutdown = raw == 1;
    raw = 0;
    if (nvs_get_u8(h, NVS_KEY_HOME_FULL, &raw) == ESP_OK) s_home_full_refresh = raw == 1;
    raw = 0;
    s_home_component = nvs_get_u8(h, NVS_KEY_HOME_COMPONENT, &raw) == ESP_OK && raw < APP_HOME_COMPONENT_COUNT
        ? (raw == 3 ? APP_HOME_CLOCK : (app_home_component_t)raw) : APP_HOME_FOLDER;
    raw = 30;
    s_home_goal_minutes = nvs_get_u8(h, NVS_KEY_HOME_GOAL, &raw) == ESP_OK && raw >= 5 && raw <= 180 && raw % 5 == 0 ? raw : 30;
    raw = 0;
    s_main_refresh = nvs_get_u8(h, NVS_KEY_MAIN_REFRESH, &raw) == ESP_OK && raw <= APP_MAIN_REFRESH_WATER
        ? (app_main_refresh_mode_t)raw : APP_MAIN_REFRESH_NORMAL;
    raw = 0;

    raw = 0;
    if (nvs_get_u8(h, NVS_KEY_AUTO_LOCK, &raw) == ESP_OK && (raw == 1 || raw == 5 || raw == 10)) s_auto_lock_minutes = raw;
    size_t value_len = sizeof(s_device_name);
    if (nvs_get_str(h, NVS_KEY_DEVICE_NAME, s_device_name, &value_len) != ESP_OK || !s_device_name[0])
        strlcpy(s_device_name, "kiikoread", sizeof(s_device_name));
    // 仅迁移旧默认名字，保留用户填写的姓名。/ Migrate only the old default, preserving custom names.
    if (!strcmp(s_device_name, "Pico")) strlcpy(s_device_name, "kiikoread", sizeof(s_device_name));
    value_len = sizeof(s_avatar);
    if (nvs_get_str(h, NVS_KEY_AVATAR, s_avatar, &value_len) != ESP_OK ||
        (s_avatar[0] && strncmp(s_avatar, "/sdcard/", 8))) s_avatar[0] = 0;
    value_len = sizeof(s_status_signature);
    if (nvs_get_str(h, NVS_KEY_STATUS_SIGNATURE, s_status_signature, &value_len) != ESP_OK)
        s_status_signature[0] = 0;
    size_t font_len = sizeof(s_font);
    if (nvs_get_str(h, NVS_KEY_FONT, s_font, &font_len) != ESP_OK) {
        s_font[0] = '\0';
    }
    font_len = sizeof(s_system_font);
    if (nvs_get_str(h, NVS_KEY_SYS_FONT, s_system_font, &font_len) != ESP_OK)
        s_system_font[0] = 0;
    uint8_t system_size = 120;
    if (nvs_get_u8(h, NVS_KEY_SYS_SIZE, &system_size) == ESP_OK &&
        system_size >= 100 && system_size <= 200 && system_size % 10 == 0)
        s_system_size = system_size;
    uint8_t system_contrast = 100;
    if (nvs_get_u8(h, NVS_KEY_SYS_CONTRAST, &system_contrast) == ESP_OK &&
        system_contrast >= 100 && system_contrast <= 140 && system_contrast % 10 == 0)
        s_system_contrast = system_contrast;
    uint8_t lock_style = 0;
    if (nvs_get_u8(h, NVS_KEY_LOCK_STYLE, &lock_style) == ESP_OK && lock_style <= 2)
        s_lock_style = lock_style;
    size_t wallpaper_len = sizeof(s_wallpaper);
    if (nvs_get_str(h, NVS_KEY_WALLPAPER, s_wallpaper, &wallpaper_len) != ESP_OK ||
        strncmp(s_wallpaper, "/sdcard/", 8)) s_wallpaper[0] = 0;
    uint8_t wake = 0;
    if (nvs_get_u8(h, NVS_KEY_WAKE, &wake) == ESP_OK) s_last_wake = wake;
    uint8_t boot = 0;
    if (nvs_get_u8(h, NVS_KEY_BOOT, &boot) == ESP_OK) s_last_boot = boot;
    uint8_t pickup = 0;
    if (nvs_get_u8(h, NVS_KEY_PICKUP, &pickup) == ESP_OK) s_pickup_wake = pickup != 0;
    uint8_t weight = 2;
    s_book_weight = nvs_get_u8(h, NVS_KEY_BOOK_WEIGHT, &weight) == ESP_OK && weight <= 4 ? weight : 2;
    s_book_weight_fine = ((int)s_book_weight-2)*APP_BOOK_WEIGHT_SUBSTEPS;
    uint8_t fine = 32;
    if(nvs_get_u8(h,NVS_KEY_BOOK_WEIGHT_FINE,&fine)==ESP_OK && fine<=64)
        s_book_weight_fine=(int)fine-APP_BOOK_WEIGHT_FINE_MAX;
    uint8_t antialias = 1;
    s_book_antialias = nvs_get_u8(h, NVS_KEY_BOOK_AA, &antialias) != ESP_OK || antialias != 0;
    uint8_t book_px = 48;
    if (nvs_get_u8(h, NVS_KEY_BOOK_PX, &book_px) == ESP_OK) s_book_px = valid_book_px(book_px);
    size_t background_len = sizeof(s_book_background);
    if (nvs_get_str(h, NVS_KEY_BOOK_BACKGROUND, s_book_background, &background_len) != ESP_OK ||
        (s_book_background[0] && strncmp(s_book_background, "/sdcard/pictures/", 17))) s_book_background[0] = 0;
    uint8_t reader_full_pages = 15;
    if (nvs_get_u8(h, NVS_KEY_READER_FULL, &reader_full_pages) == ESP_OK &&
        (reader_full_pages == 0 || reader_full_pages == 5 || reader_full_pages == 10 ||
         reader_full_pages == 15 || reader_full_pages == 30))
        s_reader_full_pages = reader_full_pages;
    uint8_t reader_turn_effect = 0;
    if (nvs_get_u8(h, NVS_KEY_READER_TURN, &reader_turn_effect) == ESP_OK && reader_turn_effect <= 1)
        s_reader_turn_effect = reader_turn_effect;
    uint8_t power_turn = 0;
    if (nvs_get_u8(h, NVS_KEY_POWER_TURN, &power_turn) == ESP_OK) s_reader_power_turn = power_turn == 1;
    uint8_t immersive = 0;
    if (nvs_get_u8(h, NVS_KEY_IMMERSIVE, &immersive) == ESP_OK) s_reader_immersive = immersive == 1;
    uint8_t hold_refresh = 0;
    if (nvs_get_u8(h, NVS_KEY_HOLD_REFRESH, &hold_refresh) == ESP_OK) s_reader_hold_refresh = hold_refresh == 1;
    for (unsigned i = 0; i < 3; ++i) {
        uint8_t value;
        if (nvs_get_u8(h, s_reader_key_names[i], &value) == ESP_OK && value < APP_READER_KEY_COUNT)
            s_reader_keys[i] = value;
    }
    uint8_t hold_action = APP_READER_KEY_HOME;
    s_reader_hold_action = nvs_get_u8(h, NVS_KEY_HOLD_ACTION, &hold_action) == ESP_OK && hold_action < APP_READER_KEY_COUNT
        ? hold_action : APP_READER_KEY_HOME;
    uint8_t keys_v2 = 0;
    bool migrate_reader_defaults = nvs_get_u8(h, NVS_KEY_READER_KEYS_V2, &keys_v2) != ESP_OK || keys_v2 != 1;
    bool old_default_hold = migrate_reader_defaults && s_reader_hold_action == APP_READER_KEY_REFRESH &&
        s_reader_keys[0] == APP_READER_KEY_PREV && s_reader_keys[1] == APP_READER_KEY_TOOLS &&
        s_reader_keys[2] == APP_READER_KEY_NEXT;
    uint8_t vertical_turn = 0;
    if (nvs_get_u8(h, NVS_KEY_VERTICAL_TURN, &vertical_turn) == ESP_OK) s_reader_vertical_turn = vertical_turn == 1;
    // 横屏阅读偏好（我方）；上游 r91 另在此处载入书内字体开关 book_fonts。
    // Landscape reading preference (ours); upstream r91 also loads in-book fonts here.
    uint8_t landscape = 0;
    if (nvs_get_u8(h, NVS_KEY_LANDSCAPE, &landscape) == ESP_OK) s_reader_landscape = landscape == 1;
    // 横屏双栏偏好（第 4 步）：默认关（决策①）。/ Two-column preference (step 4); off by default.
    uint8_t two_column = 0;
    if (nvs_get_u8(h, NVS_KEY_TWO_COLUMN, &two_column) == ESP_OK) s_reader_two_column = two_column == 1;
    uint8_t hide_images = 0, recent_sort = 0, ble_turner = 0, book_fonts = 1, book_vertical = 0;
    if (nvs_get_u8(h, NVS_KEY_HIDE_IMAGES, &hide_images) == ESP_OK) s_reader_hide_images = hide_images == 1;
    if (nvs_get_u8(h, NVS_KEY_BLE_TURNER, &ble_turner) == ESP_OK) s_ble_turner = ble_turner == 1;
    uint8_t flight = 0;
    if (nvs_get_u8(h, NVS_KEY_FLIGHT, &flight) == ESP_OK) s_flight_mode = flight != 0;
    if (nvs_get_u8(h, NVS_KEY_SHELF_RECENT, &recent_sort) == ESP_OK) s_shelf_recent_sort = recent_sort == 1;
    if (nvs_get_u8(h, NVS_KEY_BOOK_FONTS, &book_fonts) == ESP_OK) s_book_fonts = book_fonts == 1;
    if (nvs_get_u8(h, NVS_KEY_BOOK_VERTICAL, &book_vertical) == ESP_OK) s_book_vertical = book_vertical == 1;
    uint8_t tracking = 2, reading_line = 0, rule_offset = 4, indent = 2, indent_adjust = 20;
    if (nvs_get_u8(h, NVS_KEY_BOOK_TRACK, &tracking) == ESP_OK && tracking <= 4)
        s_book_tracking = tracking;
    if (nvs_get_u8(h, NVS_KEY_BOOK_RULE, &reading_line) == ESP_OK && reading_line <= 2)
        s_book_reading_line = reading_line;
    if (nvs_get_u8(h, NVS_KEY_BOOK_RULE_OFFSET, &rule_offset) == ESP_OK && rule_offset <= 8)
        s_book_rule_offset = rule_offset;
    if (nvs_get_u8(h, NVS_KEY_BOOK_INDENT, &indent) == ESP_OK && indent <= 3)
        s_book_indent = indent;
    s_book_indent_adjust = nvs_get_u8(h, NVS_KEY_BOOK_INDENT_ADJUST, &indent_adjust) == ESP_OK && indent_adjust <= 40
        ? indent_adjust : 20;
    uint8_t line = 130, para = 50, margin = 24;
    if (nvs_get_u8(h, NVS_KEY_BOOK_LINE, &line) == ESP_OK) {
        if (line >= 110 && line <= 150) s_book_line = line;
        else if (line == 180) s_book_line = 150;
    }
    if (nvs_get_u8(h, NVS_KEY_BOOK_PARA, &para) == ESP_OK && para <= 75 && para % 25 == 0) s_book_para = para;
    if (nvs_get_u8(h, NVS_KEY_BOOK_MARGIN, &margin) == ESP_OK && margin <= 60)
        s_book_margin = margin > 48 ? 48 : margin;
    uint8_t shelf_style = 2;
    if (nvs_get_u8(h, NVS_KEY_SHELF_STYLE, &shelf_style) == ESP_OK && shelf_style >= 1 && shelf_style <= 5)
        s_shelf_style = shelf_style == 3 || shelf_style == 4 ? 2 : shelf_style;
    uint8_t shelf_v22 = 0;
    bool migrate_shelf = nvs_get_u8(h, NVS_KEY_SHELF_V22, &shelf_v22) != ESP_OK || shelf_v22 != 1;
    bool retired_shelf = shelf_style == 3 || shelf_style == 4;
    if (migrate_shelf) s_shelf_style = 2;
    char folder[MEDIA_DIR_MAX];
    size_t folder_len = sizeof(folder);
    if (nvs_get_str(h, NVS_KEY_BOOKS_DIR, folder, &folder_len) == ESP_OK && valid_media_dir(folder))
        strlcpy(s_books_dir, folder, sizeof(s_books_dir));
    folder_len = sizeof(folder);
    if (nvs_get_str(h, NVS_KEY_FONTS_DIR, folder, &folder_len) == ESP_OK && valid_media_dir(folder))
        strlcpy(s_fonts_dir, folder, sizeof(s_fonts_dir));
    nvs_close(h);
    if ((migrate_shelf || retired_shelf) && nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        // 首次刷入此版只切换书架外观；随后尊重用户手动选择。/ Set acrylic once on upgrade, then preserve manual choices.
        esp_err_t saved = nvs_set_u8(h, NVS_KEY_SHELF_STYLE, 2);
        if (saved == ESP_OK) saved = nvs_set_u8(h, NVS_KEY_SHELF_V22, 1);
        if (saved == ESP_OK) (void)nvs_commit(h);
        nvs_close(h);
    }
    // Migrate only the former default preset once; keep all other user mappings.
    // Committed marker makes a later explicit full-refresh choice remain untouched.
    if (migrate_reader_defaults && nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        esp_err_t saved = old_default_hold ? nvs_set_u8(h, NVS_KEY_HOLD_ACTION, APP_READER_KEY_HOME) : ESP_OK;
        if (saved == ESP_OK) saved = nvs_set_u8(h, NVS_KEY_READER_KEYS_V2, 1);
        if (saved == ESP_OK) saved = nvs_commit(h);
        nvs_close(h);
        if (saved == ESP_OK && old_default_hold) s_reader_hold_action = APP_READER_KEY_HOME;
    }
    ESP_LOGI(
        TAG, "sleep mode %s, font %s, reader weight %d, middle hold %u",
        app_sleep_mode_name(s_sleep),
        s_font[0] != '\0' ? s_font : "(builtin)",
        app_settings_book_weight(), (unsigned)s_reader_hold_action
    );
}

app_sleep_mode_t app_settings_sleep_mode(void) {
    return s_sleep;
}

const char* app_sleep_mode_name(app_sleep_mode_t mode) {
    switch (mode) {
        case APP_SLEEP_LIGHT: return "light";
        case APP_SLEEP_DEEP: return "deep";
        case APP_SLEEP_OFF: return "off";
        default: return "?";
    }
}

void app_settings_set_sleep_mode(app_sleep_mode_t mode) {
    if (mode > APP_SLEEP_OFF) mode = APP_SLEEP_DEEP;
    s_sleep = mode;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, NVS_KEY_SLEEP, (uint8_t)mode);
    nvs_commit(h);
    nvs_close(h);
}

bool app_settings_staged_shutdown(void) { return s_staged_shutdown; }
void app_settings_set_staged_shutdown(bool staged) {
    if (staged == s_staged_shutdown) return;
    s_staged_shutdown = staged;
    nvs_put_u8(NVS_KEY_SHUTDOWN_MODE, staged ? 1 : 0);
}

app_home_component_t app_settings_home_component(void) { return s_home_component; }
uint8_t app_settings_home_goal_minutes(void) { return s_home_goal_minutes; }
static esp_err_t home_preference_save(const char *key, uint8_t value) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(h, key, value);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}
esp_err_t app_settings_set_home_component(app_home_component_t component) {
    if ((unsigned)component >= APP_HOME_COMPONENT_COUNT || component == 3) return ESP_ERR_INVALID_ARG;
    esp_err_t err = home_preference_save(NVS_KEY_HOME_COMPONENT, component);
    if (err == ESP_OK) s_home_component = component;
    return err;
}
esp_err_t app_settings_set_home_goal_minutes(uint8_t minutes) {
    if (minutes < 5 || minutes > 180 || minutes % 5) return ESP_ERR_INVALID_ARG;
    esp_err_t err = home_preference_save(NVS_KEY_HOME_GOAL, minutes);
    if (err == ESP_OK) s_home_goal_minutes = minutes;
    return err;
}

bool app_settings_home_full_refresh(void) { return s_home_full_refresh; }
app_main_refresh_mode_t app_settings_main_refresh_mode(void) { return s_main_refresh; }
bool app_settings_main_fast_refresh(void) { return s_main_refresh == APP_MAIN_REFRESH_FAST; }
void app_settings_set_main_refresh_mode(app_main_refresh_mode_t mode) {
    if (mode < APP_MAIN_REFRESH_NORMAL || mode > APP_MAIN_REFRESH_WATER || mode == s_main_refresh) return;
    s_main_refresh = mode;
    nvs_put_u8(NVS_KEY_MAIN_REFRESH, (uint8_t)mode);
}
uint8_t app_settings_auto_lock_minutes(void) { return s_auto_lock_minutes; }
void app_settings_set_auto_lock_minutes(uint8_t minutes) {
    if (minutes != 0 && minutes != 1 && minutes != 5 && minutes != 10) return;
    if (s_auto_lock_minutes == minutes) return;
    s_auto_lock_minutes = minutes;
    nvs_put_u8(NVS_KEY_AUTO_LOCK, minutes);
}

void app_settings_set_home_full_refresh(bool enabled) {
    if (enabled == s_home_full_refresh) return;
    s_home_full_refresh = enabled;
    nvs_put_u8(NVS_KEY_HOME_FULL, enabled ? 1 : 0);
}

static bool nvs_put_str(const char *key, const char *value) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) { ESP_LOGE(TAG, "save %s open: %s", key, esp_err_to_name(err)); return false; }
    err = nvs_set_str(h, key, value);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) ESP_LOGE(TAG, "save %s: %s", key, esp_err_to_name(err));
    return err == ESP_OK;
}

const char *app_settings_device_name(void) { return s_device_name; }
void app_settings_set_device_name(const char *name) {
    if (!name || !name[0] || strnlen(name, sizeof(s_device_name)) >= sizeof(s_device_name)) return;
    if (nvs_put_str(NVS_KEY_DEVICE_NAME, name)) strlcpy(s_device_name, name, sizeof(s_device_name));
}
const char *app_settings_avatar_path(void) { return s_avatar; }
void app_settings_set_avatar_path(const char *path) {
    if (!path || (path[0] && strncmp(path, "/sdcard/", 8)) ||
        strnlen(path, sizeof(s_avatar)) >= sizeof(s_avatar)) return;
    if (nvs_put_str(NVS_KEY_AVATAR, path)) strlcpy(s_avatar, path, sizeof(s_avatar));
}
const char *app_settings_status_signature(void) { return s_status_signature; }
void app_settings_set_status_signature(const char *signature) {
    if (!signature || strnlen(signature, sizeof(s_status_signature)) >= sizeof(s_status_signature)) return;
    if (nvs_put_str(NVS_KEY_STATUS_SIGNATURE, signature))
        strlcpy(s_status_signature, signature, sizeof(s_status_signature));
}

const char* app_settings_font_path(void) {
    return s_font;
}

const char* app_settings_system_font_path(void) { return s_system_font; }
void app_settings_set_system_font_path(const char* path) {
    if(!path)path="";
    if(strnlen(path,sizeof(s_system_font))>=sizeof(s_system_font))return;
    if(nvs_put_str(NVS_KEY_SYS_FONT,path))strlcpy(s_system_font,path,sizeof(s_system_font));
}

uint8_t app_settings_system_font_size(void) { return s_system_size; }
void app_settings_set_system_font_size(uint8_t percent) {
    if (percent < 100 || percent > 200 || percent % 10 || percent == s_system_size) return;
    nvs_handle_t h;
    if(nvs_open(NVS_NS,NVS_READWRITE,&h)!=ESP_OK)return;
    esp_err_t err=nvs_set_u8(h,NVS_KEY_SYS_SIZE,percent);
    if(err==ESP_OK)err=nvs_commit(h);
    nvs_close(h);
    if(err==ESP_OK)s_system_size=percent;
}
uint8_t app_settings_system_contrast(void) { return s_system_contrast; }
void app_settings_set_system_contrast(uint8_t percent) {
    if (percent < 100 || percent > 140 || percent % 10 || percent == s_system_contrast) return;
    s_system_contrast = percent;
    nvs_put_u8(NVS_KEY_SYS_CONTRAST, percent);
}
uint8_t app_settings_lock_style(void) { return s_lock_style; }
void app_settings_set_lock_style(uint8_t style) {
    if (style > 2 || style == s_lock_style) return;
    s_lock_style = style;
    nvs_put_u8(NVS_KEY_LOCK_STYLE, style);
}
const char* app_settings_wallpaper_path(void) { return s_wallpaper; }
void app_settings_set_wallpaper_path(const char* path) {
    if (!path || (path[0] && strncmp(path, "/sdcard/", 8)) ||
        strnlen(path, sizeof(s_wallpaper)) >= sizeof(s_wallpaper)) return;
    strlcpy(s_wallpaper, path, sizeof(s_wallpaper));
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, NVS_KEY_WALLPAPER, s_wallpaper);
    nvs_commit(h);
    nvs_close(h);
}

static void nvs_put_u8(const char* key, uint8_t value) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, key, value);
    nvs_commit(h);
    nvs_close(h);
}

uint8_t app_settings_last_wake(void) {
    return s_last_wake;
}

void app_settings_set_last_wake(uint8_t src) {
    if (src == s_last_wake) return;
    s_last_wake = src;
    nvs_put_u8(NVS_KEY_WAKE, src);
}

uint8_t app_settings_last_boot(void) {
    return s_last_boot;
}

void app_settings_set_last_boot(uint8_t reason) {
    if (reason == 0 || reason == s_last_boot) return;
    s_last_boot = reason;
    nvs_put_u8(NVS_KEY_BOOT, reason);
}

bool app_settings_pickup_wake(void) {
    return s_pickup_wake;
}

void app_settings_set_pickup_wake(bool on) {
    if (s_pickup_wake == on) return;
    s_pickup_wake = on;
    nvs_put_u8(NVS_KEY_PICKUP, on ? 1 : 0);
}

void app_settings_set_font_path(const char* path) {
    if(!path)path="";
    if(strnlen(path,sizeof(s_font))>=sizeof(s_font))return;
    if(nvs_put_str(NVS_KEY_FONT,path))strlcpy(s_font,path,sizeof(s_font));
}

int8_t app_settings_book_weight(void) { return (int8_t)s_book_weight - 2; }
bool app_settings_book_antialias(void) { return s_book_antialias; }
bool app_settings_set_book_antialias(bool enabled) {
    if (enabled == s_book_antialias) return true;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err = nvs_set_u8(h, NVS_KEY_BOOK_AA, enabled ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) s_book_antialias = enabled;
    return err == ESP_OK;
}
int8_t app_settings_book_weight_fine(void) {return s_book_weight_fine;}
bool app_settings_set_book_weight_fine(int8_t steps) {
    if(steps < -APP_BOOK_WEIGHT_FINE_MAX || steps > APP_BOOK_WEIGHT_FINE_MAX)return false;
    if(steps==s_book_weight_fine)return true;
    uint8_t legacy=(uint8_t)(steps/APP_BOOK_WEIGHT_SUBSTEPS+2);
    nvs_handle_t h;
    if(nvs_open(NVS_NS,NVS_READWRITE,&h)!=ESP_OK)return false;
    esp_err_t err=nvs_set_u8(h,NVS_KEY_BOOK_WEIGHT_FINE,steps+APP_BOOK_WEIGHT_FINE_MAX);
    if(err==ESP_OK)err=nvs_set_u8(h,NVS_KEY_BOOK_WEIGHT,legacy);
    if(err==ESP_OK)err=nvs_commit(h);
    nvs_close(h);
    if(err==ESP_OK){s_book_weight=legacy;s_book_weight_fine=steps;}
    return err==ESP_OK;
}
bool app_settings_set_book_weight(int8_t level) {
    return level>=-2&&level<=2&&app_settings_set_book_weight_fine(level*APP_BOOK_WEIGHT_SUBSTEPS);
}

uint8_t app_settings_book_px(void) {
    return s_book_px;
}

void app_settings_set_book_px(uint8_t px) {
    px = valid_book_px(px);
    if (s_book_px == px) return;
    s_book_px = px;
    nvs_put_u8(NVS_KEY_BOOK_PX, px);
}

const char *app_settings_book_background_path(void) { return s_book_background; }
bool app_settings_set_book_background_path(const char *path) {
    if (!path || (path[0] && strncmp(path, "/sdcard/pictures/", 17)) ||
        strnlen(path, sizeof(s_book_background)) >= sizeof(s_book_background) || strstr(path, "/../")) return false;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err = nvs_set_str(h, NVS_KEY_BOOK_BACKGROUND, path);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) return false;
    strlcpy(s_book_background, path, sizeof(s_book_background));
    return true;
}

uint8_t app_settings_reader_full_pages(void) { return s_reader_full_pages; }
void app_settings_set_reader_full_pages(uint8_t pages) {
    if ((pages != 0 && pages != 5 && pages != 10 && pages != 15 && pages != 30) ||
        pages == s_reader_full_pages) return;
    s_reader_full_pages = pages;
    nvs_put_u8(NVS_KEY_READER_FULL, pages);
}
uint8_t app_settings_reader_turn_effect(void) { return s_reader_turn_effect; }
void app_settings_set_reader_turn_effect(uint8_t effect) {
    if (effect > 1 || effect == s_reader_turn_effect) return;
    s_reader_turn_effect = effect;
    nvs_put_u8(NVS_KEY_READER_TURN, effect);
}
bool app_settings_reader_power_turn(void) { return s_reader_power_turn; }
void app_settings_set_reader_power_turn(bool on) {
    if (s_reader_power_turn == on) return;
    s_reader_power_turn = on;
    nvs_put_u8(NVS_KEY_POWER_TURN, on ? 1 : 0);
}
bool app_settings_reader_immersive(void) { return s_reader_immersive; }
void app_settings_set_reader_immersive(bool on) {
    if (s_reader_immersive == on) return;
    s_reader_immersive = on;
    nvs_put_u8(NVS_KEY_IMMERSIVE, on ? 1 : 0);
}
bool app_settings_reader_hide_images(void) { return s_reader_hide_images; }
void app_settings_set_reader_hide_images(bool on) {
    if (s_reader_hide_images == on) return;
    s_reader_hide_images = on;
    nvs_put_u8(NVS_KEY_HIDE_IMAGES, on ? 1 : 0);
}
bool app_settings_book_fonts(void) { return s_book_fonts; }
void app_settings_set_book_fonts(bool on) {
    if (s_book_fonts == on) return;
    s_book_fonts = on;
    nvs_put_u8(NVS_KEY_BOOK_FONTS, on ? 1 : 0);
}
bool app_settings_book_vertical(void) { return s_book_vertical; }
void app_settings_set_book_vertical(bool on) {
    if (s_book_vertical == on) return;
    s_book_vertical = on;
    nvs_put_u8(NVS_KEY_BOOK_VERTICAL, on ? 1 : 0);
}
bool app_settings_ble_turner(void) { return s_ble_turner; }
app_reader_key_action_t app_settings_reader_key_action(unsigned key) {
    return key < 3 ? (app_reader_key_action_t)s_reader_keys[key] : APP_READER_KEY_NONE;
}
bool app_settings_reader_key_action_allowed(unsigned key, app_reader_key_action_t action) {
    return key < 4 && action >= 0 && action < APP_READER_KEY_COUNT;
}
bool app_settings_set_reader_key_action(unsigned key, app_reader_key_action_t action) {
    if (key >= 3 || !app_settings_reader_key_action_allowed(key, action)) return false;
    if (s_reader_keys[key] == action) return true;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err = nvs_set_u8(h, s_reader_key_names[key], action);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) s_reader_keys[key] = action;
    return err == ESP_OK;
}
app_reader_key_action_t app_settings_reader_hold_action(void) { return (app_reader_key_action_t)s_reader_hold_action; }
bool app_settings_set_reader_hold_action(app_reader_key_action_t action) {
    if (!app_settings_reader_key_action_allowed(3, action)) return false;
    if (s_reader_hold_action == action) return true;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err = nvs_set_u8(h, NVS_KEY_HOLD_ACTION, action);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) s_reader_hold_action = action;
    return err == ESP_OK;
}
void app_settings_set_reader_key_preset(unsigned preset) {
    static const uint8_t values[2][3] = {
        {APP_READER_KEY_PREV, APP_READER_KEY_TOOLS, APP_READER_KEY_NEXT},
        {APP_READER_KEY_HOME, APP_READER_KEY_FULLSCREEN, APP_READER_KEY_TOOLS}
    };
    if (preset < 1 || preset > 2) return;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return;
    for (unsigned i = 0; i < 3 && err == ESP_OK; ++i) err = nvs_set_u8(h, s_reader_key_names[i], values[preset - 1][i]);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_HOLD_ACTION, preset == 1 ? APP_READER_KEY_HOME : APP_READER_KEY_REFRESH);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) {
        memcpy(s_reader_keys, values[preset - 1], sizeof(s_reader_keys));
        s_reader_hold_action = preset == 1 ? APP_READER_KEY_HOME : APP_READER_KEY_REFRESH;
    }
}

bool app_settings_reader_hold_refresh(void) { return s_reader_hold_refresh; }
bool app_settings_reader_vertical_turn(void) { return s_reader_vertical_turn; }
void app_settings_set_reader_vertical_turn(bool on) {
    if (s_reader_vertical_turn == on) return;
    s_reader_vertical_turn = on;
    nvs_put_u8(NVS_KEY_VERTICAL_TURN, on ? 1 : 0);
}
bool app_settings_reader_landscape(void) { return s_reader_landscape; }
void app_settings_set_reader_landscape(bool on) {
    if (s_reader_landscape == on) return;
    s_reader_landscape = on;
    nvs_put_u8(NVS_KEY_LANDSCAPE, on ? 1 : 0);
}
bool app_settings_reader_two_column(void) { return s_reader_two_column; }
void app_settings_set_reader_two_column(bool on) {
    if (s_reader_two_column == on) return;
    s_reader_two_column = on;
    nvs_put_u8(NVS_KEY_TWO_COLUMN, on ? 1 : 0);
}
void app_settings_set_reader_hold_refresh(bool on) {
    if (s_reader_hold_refresh == on) return;
    s_reader_hold_refresh = on;
    nvs_put_u8(NVS_KEY_HOLD_REFRESH, on ? 1 : 0);
}
void app_settings_set_ble_turner(bool on) {
    if (s_ble_turner == on) return;
    s_ble_turner = on;
    nvs_put_u8(NVS_KEY_BLE_TURNER, on ? 1 : 0);
}
bool app_settings_flight_mode(void) { return s_flight_mode; }
void app_settings_set_flight_mode(bool on) {
    if (s_flight_mode == on) return;
    s_flight_mode = on;
    nvs_put_u8(NVS_KEY_FLIGHT, on ? 1 : 0);
}
bool app_settings_shelf_recent_sort(void) { return s_shelf_recent_sort; }
void app_settings_set_shelf_recent_sort(bool on) {
    if (s_shelf_recent_sort == on) return;
    s_shelf_recent_sort = on;
    nvs_put_u8(NVS_KEY_SHELF_RECENT, on ? 1 : 0);
}
uint8_t app_settings_book_tracking(void) { return s_book_tracking; }
void app_settings_set_book_tracking(uint8_t index) {
    if (index > 4 || index == s_book_tracking) return;
    s_book_tracking = index;
    nvs_put_u8(NVS_KEY_BOOK_TRACK, index);
}
uint8_t app_settings_book_indent(void) { return s_book_indent; }
void app_settings_set_book_indent(uint8_t em) {
    if (em > 3 || em == s_book_indent) return;
    s_book_indent = em;
    nvs_put_u8(NVS_KEY_BOOK_INDENT, em);
}
int8_t app_settings_book_indent_adjust(void) { return (int8_t)s_book_indent_adjust - 20; }
void app_settings_set_book_indent_adjust(int8_t px) {
    if (px < -20 || px > 20 || px == app_settings_book_indent_adjust()) return;
    s_book_indent_adjust = (uint8_t)(px + 20);
    nvs_put_u8(NVS_KEY_BOOK_INDENT_ADJUST, s_book_indent_adjust);
}
uint8_t app_settings_book_reading_line(void) { return s_book_reading_line; }
void app_settings_set_book_reading_line(uint8_t style) {
    if (style > 2 || style == s_book_reading_line) return;
    s_book_reading_line = style;
    nvs_put_u8(NVS_KEY_BOOK_RULE, style);
}
int8_t app_settings_book_reading_line_offset(void) { return (int8_t)((int)s_book_rule_offset - 4) * 2; }
void app_settings_set_book_reading_line_offset(int8_t offset_px) {
    if (offset_px < -8 || offset_px > 8 || offset_px % 2) return;
    uint8_t index = (uint8_t)(offset_px / 2 + 4);
    if (index == s_book_rule_offset) return;
    s_book_rule_offset = index;
    nvs_put_u8(NVS_KEY_BOOK_RULE_OFFSET, index);
}
uint8_t app_settings_book_line_spacing(void) { return s_book_line; }
void app_settings_set_book_line_spacing(uint8_t percent) {
    if (percent < 110 || percent > 150) return;
    if (s_book_line == percent) return;
    s_book_line = percent;
    nvs_put_u8(NVS_KEY_BOOK_LINE, percent);
}
uint8_t app_settings_book_margin(void) { return s_book_margin; }
void app_settings_set_book_margin(uint8_t px) {
    if (px > 48 || s_book_margin == px) return;
    s_book_margin = px;
    nvs_put_u8(NVS_KEY_BOOK_MARGIN, px);
}
uint8_t app_settings_book_paragraph_spacing(void) { return s_book_para; }
void app_settings_set_book_paragraph_spacing(uint8_t percent) {
    if (percent > 75 || percent % 25) return;
    if (s_book_para == percent) return;
    s_book_para = percent;
    nvs_put_u8(NVS_KEY_BOOK_PARA, percent);
}
uint8_t app_settings_shelf_style(void) { return s_shelf_style; }
void app_settings_set_shelf_style(uint8_t style) {
    if (style < 1 || style > 5) return;
    if (style == 3 || style == 4) style = 2;
    if (s_shelf_style == style) return;
    s_shelf_style = style;
    nvs_put_u8(NVS_KEY_SHELF_STYLE, style);
}
const char* app_settings_books_dir(void) { return s_books_dir; }
const char* app_settings_fonts_dir(void) { return s_fonts_dir; }
bool app_settings_set_books_dir(const char* path) {
    return set_media_dir(s_books_dir, NVS_KEY_BOOKS_DIR, path);
}
bool app_settings_set_fonts_dir(const char* path) {
    return set_media_dir(s_fonts_dir, NVS_KEY_FONTS_DIR, path);
}

#ifndef APP_SETTINGS_BACKUP_ROOT
#define APP_SETTINGS_BACKUP_ROOT "/sdcard"
#endif
#define BACKUP_FILE APP_SETTINGS_BACKUP_ROOT "/Pico-settings.backup"
#define BACKUP_TEMP APP_SETTINGS_BACKUP_ROOT "/Pico-settings.backup.tmp"
#define BACKUP_PREVIOUS APP_SETTINGS_BACKUP_ROOT "/Pico-settings.backup.previous"

// All fields are bytes, so the v1 disk layout is independent of structure padding.
// 所有字段均为字节，v1 磁盘格式不依赖编译器的结构体填充。
typedef struct {
    char magic[8];
    uint8_t flags[17];
    char font[FONT_PATH_MAX];
    char system_font[FONT_PATH_MAX];
    char wallpaper[288];
    char books_dir[MEDIA_DIR_MAX];
    char fonts_dir[MEDIA_DIR_MAX];
    uint8_t checksum[4];
} settings_backup_v1_t;

typedef struct {
    char device_name[64];
    char avatar[288];
    char status_signature[96];
    uint8_t home_full_refresh;
    uint8_t checksum[4];
} settings_backup_profile_t;

typedef struct {
    read_pico_transfer_wifi_backup_t credentials;
    uint8_t checksum[4];
} settings_backup_wifi_t;

// PICOSETA为第10版八字节标识；映射块独立校验，不改旧网络/阅读数据块。
// PICOSETA is the eight-byte v10 identifier; independently check mappings without changing old network/history blocks.
typedef struct { uint8_t actions[3], checksum[4]; } settings_backup_keys_t;
static uint32_t backup_keys_checksum(const settings_backup_keys_t *keys) {
    uint32_t hash = 2166136261u;
    for (unsigned i = 0; i < 3; ++i) hash = (hash ^ keys->actions[i]) * 16777619u;
    return hash;
}
static bool backup_keys_valid(const settings_backup_keys_t *keys) {
    uint32_t stored = 0;
    for (unsigned i = 0; i < 4; ++i) stored |= (uint32_t)keys->checksum[i] << (8 * i);
    for (unsigned i = 0; i < 3; ++i) if (keys->actions[i] >= APP_READER_KEY_COUNT) return false;
    return stored == backup_keys_checksum(keys);
}

typedef struct { uint8_t action, checksum[4]; } settings_backup_hold_t;
static uint32_t backup_hold_checksum(const settings_backup_hold_t *hold) {
    return (2166136261u ^ hold->action) * 16777619u;
}
static bool backup_hold_valid(const settings_backup_hold_t *hold) {
    uint32_t stored = 0;
    for (unsigned i = 0; i < 4; ++i) stored |= (uint32_t)hold->checksum[i] << (8 * i);
    return hold->action < APP_READER_KEY_COUNT && stored == backup_hold_checksum(hold);
}
// v12 追加独立校验的微调块，保留旧字段偏移与旧备份读取。/ V12 appends a sealed adjustment block, preserving legacy fields and readers.
typedef struct { uint8_t encoded, checksum[4]; } settings_backup_indent_adjust_t;
static uint32_t backup_indent_adjust_checksum(const settings_backup_indent_adjust_t *adjust) {
    return (2166136261u ^ adjust->encoded) * 16777619u;
}
static bool backup_indent_adjust_valid(const settings_backup_indent_adjust_t *adjust) {
    uint32_t stored = 0;
    for (unsigned i = 0; i < 4; ++i) stored |= (uint32_t)adjust->checksum[i] << (8 * i);
    return adjust->encoded <= 40 && stored == backup_indent_adjust_checksum(adjust);
}
// v13 adds a checked reading-weight block; older backups restore native strokes.
typedef struct { uint8_t encoded, checksum[4]; } settings_backup_weight_t;
static uint32_t backup_weight_checksum(const settings_backup_weight_t *weight) {
    return (2166136261u ^ weight->encoded) * 16777619u;
}
static bool backup_weight_valid(const settings_backup_weight_t *weight) {
    uint32_t stored = 0;
    for (unsigned i = 0; i < 4; ++i) stored |= (uint32_t)weight->checksum[i] << (8 * i);
    return (weight->encoded <= 4 || (weight->encoded>=128&&weight->encoded<=192)) && stored == backup_weight_checksum(weight);
}
typedef struct { char path[288]; uint8_t checksum[4]; } settings_backup_background_t;
static uint32_t backup_background_checksum(const settings_backup_background_t *background) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < sizeof(background->path); ++i) hash = (hash ^ (uint8_t)background->path[i]) * 16777619u;
    return hash;
}
static bool backup_background_valid(const settings_backup_background_t *background) {
    uint32_t stored = 0;
    for (unsigned i = 0; i < 4; ++i) stored |= (uint32_t)background->checksum[i] << (i * 8);
    return strnlen(background->path, sizeof(background->path)) < sizeof(background->path) &&
        (!background->path[0] || !strncmp(background->path, "/sdcard/pictures/", 17)) &&
        stored == backup_background_checksum(background);
}
typedef struct { uint8_t component, minutes, checksum[4]; } settings_backup_home_t;
static uint32_t backup_home_checksum(const settings_backup_home_t *v) {
    return ((2166136261u ^ v->component) * 16777619u ^ v->minutes) * 16777619u;
}
static bool backup_home_valid(const settings_backup_home_t *v) {
    uint32_t crc = 0;
    for (unsigned i = 0; i < 4; ++i) crc |= (uint32_t)v->checksum[i] << (8*i);
    return v->component < APP_HOME_COMPONENT_COUNT && v->minutes >= 5 && v->minutes <= 180 && !(v->minutes % 5) && crc == backup_home_checksum(v);
}
static unsigned backup_version(const settings_backup_v1_t *backup) {
    if (memcmp(backup->magic, "PICOSET", 7)) return 0;
    unsigned char c = backup->magic[7];
    return c >= '1' && c <= '9' ? c - '0' : c == 'A' ? 10 : c == 'B' ? 11 : c == 'C' ? 12 : c == 'D' ? 13 : c == 'E' ? 14 : c == 'F' ? 15 : c == 'G' ? 16 : c == 'H' ? 17 : 0;
}

static void backup_erase_secret(void *ptr, size_t size) {
    volatile uint8_t *bytes = ptr;
    while (size--) *bytes++ = 0;
}

static uint32_t backup_wifi_checksum(const settings_backup_wifi_t *wifi) {
    const uint8_t *bytes = (const uint8_t *)wifi;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < offsetof(settings_backup_wifi_t, checksum); ++i)
        hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

static void backup_wifi_seal(settings_backup_wifi_t *wifi) {
    uint32_t hash = backup_wifi_checksum(wifi);
    for (int i = 0; i < 4; ++i) wifi->checksum[i] = (uint8_t)(hash >> (i * 8));
}

static bool backup_wifi_valid(const settings_backup_wifi_t *wifi) {
    uint32_t stored = 0;
    for (int i = 0; i < 4; ++i) stored |= (uint32_t)wifi->checksum[i] << (i * 8);
    return stored == backup_wifi_checksum(wifi) &&
           read_pico_transfer_wifi_backup_valid(&wifi->credentials);
}

enum {
    BK_SLEEP, BK_PICKUP, BK_SYS_SIZE, BK_SYS_CONTRAST, BK_LOCK,
    BK_BOOK_PX, BK_SHAKE, BK_FULL_PAGES, BK_TURN, BK_POWER_TURN,
    BK_IMMERSIVE, BK_TRACKING, BK_READING_LINE, BK_LINE_SPACING,
    BK_MARGIN, BK_PARAGRAPH, BK_SHELF,
};

static uint32_t backup_checksum(const settings_backup_v1_t *backup) {
    const uint8_t *data = (const uint8_t *)backup;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < offsetof(settings_backup_v1_t, checksum); ++i)
        hash = (hash ^ data[i]) * 16777619u;
    return hash;
}

static uint32_t backup_indent_checksum(const settings_backup_v1_t *backup, uint8_t indent) {
    const uint8_t *data = (const uint8_t *)backup;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < sizeof(*backup); ++i) hash = (hash ^ data[i]) * 16777619u;
    return (hash ^ indent) * 16777619u;
}
static uint32_t backup_rule_offset_checksum(const settings_backup_v1_t *backup, uint8_t indent,
                                            uint8_t rule_offset) {
    return (backup_indent_checksum(backup, indent) ^ rule_offset) * 16777619u;
}
static uint32_t backup_shutdown_checksum(const settings_backup_v1_t *backup, uint8_t indent,
                                         uint8_t rule_offset, uint8_t staged_shutdown) {
    return (backup_rule_offset_checksum(backup, indent, rule_offset) ^ staged_shutdown) * 16777619u;
}
static uint32_t backup_profile_checksum(const settings_backup_v1_t *backup, uint8_t indent,
                                        uint8_t rule_offset, uint8_t staged_shutdown,
                                        const settings_backup_profile_t *profile) {
    uint32_t hash = backup_shutdown_checksum(backup, indent, rule_offset, staged_shutdown);
    const uint8_t *data = (const uint8_t *)profile;
    for (size_t i = 0; i < offsetof(settings_backup_profile_t, checksum); ++i)
        hash = (hash ^ data[i]) * 16777619u;
    return hash;
}

static void backup_seal(settings_backup_v1_t *backup) {
    uint32_t value = backup_checksum(backup);
    for (int i = 0; i < 4; ++i) backup->checksum[i] = (uint8_t)(value >> (i * 8));
}

static bool backup_card_ready(void) {
    read_pico_sd_info_t info = {0};
    return read_pico_sd_get_info(&info) == ESP_OK && info.mounted;
}

esp_err_t app_settings_backup_save(void) {
    if (!backup_card_ready()) return ESP_ERR_INVALID_STATE;
    settings_backup_v1_t backup = {0};
    memcpy(backup.magic, "PICOSETH", sizeof(backup.magic));
    uint8_t *f = backup.flags;
    f[BK_SLEEP] = s_sleep;
    f[BK_PICKUP] = s_pickup_wake;
    f[BK_SYS_SIZE] = s_system_size;
    f[BK_SYS_CONTRAST] = s_system_contrast;
    f[BK_LOCK] = s_lock_style;
    f[BK_BOOK_PX] = s_book_px;
    // v15复用已退役的晃动标志保存关闭抗锯齿；旧备份始终恢复开启。
    // v15 reuses the retired shake flag for disabled AA; older backups always restore AA on.
    f[BK_SHAKE] = s_book_antialias ? 0 : 1;
    f[BK_FULL_PAGES] = s_reader_full_pages;
    f[BK_TURN] = s_reader_turn_effect;
    f[BK_POWER_TURN] = s_reader_power_turn;
    f[BK_IMMERSIVE] = s_reader_immersive;
    f[BK_TRACKING] = s_book_tracking;
    f[BK_READING_LINE] = s_book_reading_line;
    f[BK_LINE_SPACING] = s_book_line;
    f[BK_MARGIN] = s_book_margin;
    f[BK_PARAGRAPH] = s_book_para;
    f[BK_SHELF] = s_shelf_style;
    strlcpy(backup.font, !strcmp(s_font, "builtin") ? "" : s_font, sizeof(backup.font));
    strlcpy(backup.system_font, s_system_font, sizeof(backup.system_font));
    strlcpy(backup.wallpaper, s_wallpaper, sizeof(backup.wallpaper));
    strlcpy(backup.books_dir, s_books_dir, sizeof(backup.books_dir));
    strlcpy(backup.fonts_dir, s_fonts_dir, sizeof(backup.fonts_dir));
    backup_seal(&backup);
    uint8_t idle_index = s_auto_lock_minutes == 1 ? 1 : s_auto_lock_minutes == 5 ? 2 : s_auto_lock_minutes == 10 ? 3 : 0;
    uint8_t extension[7] = {s_book_indent, s_book_rule_offset, (s_staged_shutdown ? 1 : 0) | (idle_index << 1) | ((uint8_t)s_main_refresh << 3)};
    uint32_t extension_hash = backup_shutdown_checksum(&backup, s_book_indent,
                                                        s_book_rule_offset, extension[2]);
    for (int i = 0; i < 4; ++i) extension[i + 3] = (uint8_t)(extension_hash >> (i * 8));
    settings_backup_profile_t profile = {0};
    strlcpy(profile.device_name, s_device_name, sizeof(profile.device_name));
    strlcpy(profile.avatar, s_avatar, sizeof(profile.avatar));
    strlcpy(profile.status_signature, s_status_signature, sizeof(profile.status_signature));
    // v5 及之后复用此字段的预留位；v7 在其后追加 WiFi 与阅读资料。
    // V5+ use reserved bits here; v7 appends WiFi/reading records, and v9 stores main mode in the extension.
    profile.home_full_refresh = (s_home_full_refresh ? 1 : 0) |
                                (s_reader_hide_images ? 2 : 0) |
                                (s_shelf_recent_sort ? 4 : 0) |
                                (s_ble_turner ? 8 : 0) |
                                (s_reader_hold_refresh ? 16 : 0) |
                                (s_reader_vertical_turn ? 32 : 0) |
                                (app_settings_main_fast_refresh() ? 128 : 0);
    uint32_t profile_hash = backup_profile_checksum(&backup, extension[0], extension[1], extension[2], &profile);
    for (int i = 0; i < 4; ++i) profile.checksum[i] = (uint8_t)(profile_hash >> (i * 8));

    settings_backup_keys_t keys = {0};
    memcpy(keys.actions, s_reader_keys, sizeof(keys.actions));
    uint32_t keys_hash = backup_keys_checksum(&keys);
    for (unsigned i = 0; i < 4; ++i) keys.checksum[i] = (uint8_t)(keys_hash >> (8 * i));
    settings_backup_hold_t hold = {.action = s_reader_hold_action};
    uint32_t hold_hash = backup_hold_checksum(&hold);
    for (unsigned i = 0; i < 4; ++i) hold.checksum[i] = (uint8_t)(hold_hash >> (8 * i));
    settings_backup_indent_adjust_t adjust = {.encoded = s_book_indent_adjust};
    uint32_t adjust_hash = backup_indent_adjust_checksum(&adjust);
    for (unsigned i = 0; i < 4; ++i) adjust.checksum[i] = (uint8_t)(adjust_hash >> (8 * i));
    settings_backup_weight_t weight = {.encoded = 128+s_book_weight_fine+APP_BOOK_WEIGHT_FINE_MAX};
    uint32_t weight_hash = backup_weight_checksum(&weight);
    for (unsigned i = 0; i < 4; ++i) weight.checksum[i] = (uint8_t)(weight_hash >> (8 * i));
    settings_backup_wifi_t wifi = {0};
    esp_err_t wifi_err = read_pico_transfer_export_wifi_backup(&wifi.credentials);
    if (wifi_err != ESP_OK) { backup_erase_secret(&wifi, sizeof(wifi)); return wifi_err; }
    backup_wifi_seal(&wifi);
    settings_backup_background_t background = {0};
    strlcpy(background.path, s_book_background, sizeof(background.path));
    uint32_t background_hash = backup_background_checksum(&background);
    for (unsigned i = 0; i < 4; ++i) background.checksum[i] = (uint8_t)(background_hash >> (8 * i));

    FILE *file = fopen(BACKUP_TEMP, "wb");
    if (!file) { backup_erase_secret(&wifi, sizeof(wifi)); return ESP_FAIL; }
    bool ok = fwrite(&backup, 1, sizeof(backup), file) == sizeof(backup);
    if (ok) ok = fwrite(extension, 1, sizeof(extension), file) == sizeof(extension);
    if (ok) ok = fwrite(&profile, 1, sizeof(profile), file) == sizeof(profile);
    if (ok) ok = fwrite(&keys, 1, sizeof(keys), file) == sizeof(keys);
    if (ok) ok = fwrite(&hold, 1, sizeof(hold), file) == sizeof(hold);
    if (ok) ok = fwrite(&adjust, 1, sizeof(adjust), file) == sizeof(adjust);
    if (ok) ok = fwrite(&weight, 1, sizeof(weight), file) == sizeof(weight);
    if (ok) ok = fwrite(&wifi, 1, sizeof(wifi), file) == sizeof(wifi);
    if (ok) ok = fwrite(&background, 1, sizeof(background), file) == sizeof(background);
    settings_backup_home_t home = {.component=s_home_component, .minutes=s_home_goal_minutes};
    uint32_t home_hash = backup_home_checksum(&home);
    for (unsigned i = 0; i < 4; ++i) home.checksum[i] = home_hash >> (8*i);
    if (ok) ok = fwrite(&home, 1, sizeof(home), file) == sizeof(home);
    backup_erase_secret(&wifi, sizeof(wifi));
    if (ok) ok = book_history_backup_write(file) == ESP_OK;
    if (ok) ok = fflush(file) == 0;
    if (ok) ok = fsync(fileno(file)) == 0;
    if (fclose(file) != 0) ok = false;
    bool rotated = false;
    if (ok && remove(BACKUP_PREVIOUS) != 0 && errno != ENOENT) ok = false;
    if (ok && rename(BACKUP_FILE, BACKUP_PREVIOUS) == 0) rotated = true;
    else if (ok && errno != ENOENT) ok = false;
    if (ok && rename(BACKUP_TEMP, BACKUP_FILE) != 0) ok = false;
    if (!ok && rotated) (void)rename(BACKUP_PREVIOUS, BACKUP_FILE);
    if (ok && rotated) (void)remove(BACKUP_PREVIOUS);
    if (!ok) { (void)remove(BACKUP_TEMP); return ESP_FAIL; }
    return ESP_OK;
}

static bool backup_path_valid(const char *path, size_t capacity) {
    size_t len = strnlen(path, capacity);
    if (len == capacity) return false;
    if (!len) return true;
    if (strncmp(path, "/sdcard/", 8) || !path[8]) return false;
    for (size_t i = 8; i < len; ++i)
        if ((unsigned char)path[i] < 32 || path[i] == '\\' || path[i] == ':') return false;
    for (const char *part = path + 8; *part;) {
        const char *end = strchr(part, '/');
        size_t n = end ? (size_t)(end - part) : strlen(part);
        if (!n || (n == 1 && part[0] == '.') || (n == 2 && part[0] == '.' && part[1] == '.')) return false;
        if (!end) break;
        part = end + 1;
    }
    return true;
}

static bool backup_valid(const settings_backup_v1_t *backup) {
    const uint8_t *f = backup->flags;
    uint32_t checksum = 0;
    for (int i = 0; i < 4; ++i) checksum |= (uint32_t)backup->checksum[i] << (i * 8);
    if (!backup_version(backup) || checksum != backup_checksum(backup)) return false;
    if (f[BK_SLEEP] > APP_SLEEP_OFF || f[BK_PICKUP] > 1 ||
        f[BK_SYS_SIZE] < 100 || f[BK_SYS_SIZE] > 200 || f[BK_SYS_SIZE] % 10 ||
        f[BK_SYS_CONTRAST] < 100 || f[BK_SYS_CONTRAST] > 140 || f[BK_SYS_CONTRAST] % 10 ||
        f[BK_LOCK] > 2 || f[BK_BOOK_PX] < 36 || f[BK_BOOK_PX] > 72 ||
        f[BK_SHAKE] > 1 || (f[BK_FULL_PAGES] != 0 && f[BK_FULL_PAGES] != 5 &&
                            f[BK_FULL_PAGES] != 10 && f[BK_FULL_PAGES] != 15 &&
                            f[BK_FULL_PAGES] != 30) ||
        f[BK_TURN] > 1 || f[BK_POWER_TURN] > 1 || f[BK_IMMERSIVE] > 1 ||
        f[BK_TRACKING] > 4 || f[BK_READING_LINE] > 2 ||
        f[BK_LINE_SPACING] < 110 || f[BK_LINE_SPACING] > 150 ||
        f[BK_MARGIN] > 60 ||
        f[BK_PARAGRAPH] > 75 || f[BK_PARAGRAPH] % 25 ||
        f[BK_SHELF] < 1 || f[BK_SHELF] > 5) return false;
    if (strnlen(backup->books_dir, sizeof(backup->books_dir)) == sizeof(backup->books_dir) ||
        strnlen(backup->fonts_dir, sizeof(backup->fonts_dir)) == sizeof(backup->fonts_dir)) return false;
    return backup_path_valid(backup->font, sizeof(backup->font)) &&
           backup_path_valid(backup->system_font, sizeof(backup->system_font)) &&
           backup_path_valid(backup->wallpaper, sizeof(backup->wallpaper)) &&
           valid_media_dir(backup->books_dir) && valid_media_dir(backup->fonts_dir);
}

static bool backup_file_exists(const char *path, bool directory) {
    struct stat st;
    return stat(path, &st) == 0 && (directory ? S_ISDIR(st.st_mode) : S_ISREG(st.st_mode));
}

esp_err_t app_settings_backup_restore(void) {
    if (!backup_card_ready()) return ESP_ERR_INVALID_STATE;
    const char *source = BACKUP_FILE;
    FILE *file = fopen(source, "rb");
    if (!file) { source = BACKUP_PREVIOUS; file = fopen(source, "rb"); }
    if (!file) return ESP_ERR_NOT_FOUND;
    settings_backup_v1_t backup;
    bool ok = fread(&backup, 1, sizeof(backup), file) == sizeof(backup);
    unsigned version = ok ? backup_version(&backup) : 0;
    settings_backup_hold_t hold = {.action = APP_READER_KEY_HOME};
    uint8_t indent = 2, rule_offset = 4, staged_shutdown = 0;
    settings_backup_indent_adjust_t adjust = {.encoded = 20};
    settings_backup_weight_t weight = {.encoded = 2};
    settings_backup_profile_t profile = {.device_name = "kiikoread"};
    settings_backup_keys_t keys = {.actions = {APP_READER_KEY_PREV, APP_READER_KEY_TOOLS, APP_READER_KEY_NEXT}};
    settings_backup_wifi_t wifi = {0};
    bool has_wifi = false;
    settings_backup_background_t background = {0};
    settings_backup_home_t home = {.component=APP_HOME_FOLDER, .minutes=30};
    app_main_refresh_mode_t main_refresh = APP_MAIN_REFRESH_NORMAL;
    if (ok && !memcmp(backup.magic, "PICOSET2", 8)) {
        uint8_t extension[5];
        ok = fread(extension, 1, sizeof(extension), file) == sizeof(extension);
        if (ok) {
            indent = extension[0];
            uint32_t stored = 0;
            for (int i = 0; i < 4; ++i) stored |= (uint32_t)extension[i + 1] << (i * 8);
            ok = indent <= 3 && stored == backup_indent_checksum(&backup, indent);
        }
    } else if (ok && !memcmp(backup.magic, "PICOSET3", 8)) {
        uint8_t extension[6];
        ok = fread(extension, 1, sizeof(extension), file) == sizeof(extension);
        if (ok) {
            indent = extension[0]; rule_offset = extension[1];
            uint32_t stored = 0;
            for (int i = 0; i < 4; ++i) stored |= (uint32_t)extension[i + 2] << (i * 8);
            ok = indent <= 3 && rule_offset <= 8 &&
                 stored == backup_rule_offset_checksum(&backup, indent, rule_offset);
        }
    } else if (ok && (version >= 4)) {
        uint8_t extension[7];
        ok = fread(extension, 1, sizeof(extension), file) == sizeof(extension);
        if (ok) {
            indent = extension[0]; rule_offset = extension[1]; staged_shutdown = extension[2];
            uint32_t stored = 0;
            for (int i = 0; i < 4; ++i) stored |= (uint32_t)extension[i + 3] << (i * 8);
            ok = indent <= 3 && rule_offset <= 8 && staged_shutdown <= ((version >= 9) ? 23 : !memcmp(backup.magic, "PICOSET8", 8) ? 7 : 1) &&
                 stored == backup_shutdown_checksum(&backup, indent, rule_offset, staged_shutdown);
        }
        if (ok && (version >= 5)) {
            ok = fread(&profile, 1, sizeof(profile), file) == sizeof(profile);
            if (ok) {
                uint32_t stored = 0;
                for (int i = 0; i < 4; ++i) stored |= (uint32_t)profile.checksum[i] << (i * 8);
                ok = stored == backup_profile_checksum(&backup, indent, rule_offset,
                                                        staged_shutdown, &profile) &&
                     strnlen(profile.device_name, sizeof(profile.device_name)) < sizeof(profile.device_name) &&
                     profile.device_name[0] &&
                     strnlen(profile.status_signature, sizeof(profile.status_signature)) < sizeof(profile.status_signature) &&
                     // 位6仍忽略旧实验模式；v9 在校验扩展内保存三种主页刷新模式。/ Bit 6 still ignores the retired experiment; v9 stores all three main modes in its checked extension.
                     backup_path_valid(profile.avatar, sizeof(profile.avatar));
            }
        }
    }
    if (ok && (version >= 9)) main_refresh = (app_main_refresh_mode_t)(staged_shutdown >> 3);
    else if (ok && (profile.home_full_refresh & 128)) main_refresh = APP_MAIN_REFRESH_FAST;
    if (ok && version >= 10)
        ok = fread(&keys, 1, sizeof(keys), file) == sizeof(keys) && backup_keys_valid(&keys);
    if (ok && version >= 11)
        ok = fread(&hold, 1, sizeof(hold), file) == sizeof(hold) && backup_hold_valid(&hold);
    if (ok && version >= 12)
        ok = fread(&adjust, 1, sizeof(adjust), file) == sizeof(adjust) && backup_indent_adjust_valid(&adjust);
    if (ok && version >= 13)
        ok = fread(&weight, 1, sizeof(weight), file) == sizeof(weight) && backup_weight_valid(&weight) && (version>=17 || weight.encoded<=4);
    long history_position = -1;
    has_wifi = ok && version >= 7;
    if (has_wifi) ok = fread(&wifi, 1, sizeof(wifi), file) == sizeof(wifi) && backup_wifi_valid(&wifi);
    if (ok && version >= 14) ok = fread(&background, 1, sizeof(background), file) == sizeof(background) &&
        backup_background_valid(&background) && backup_path_valid(background.path, sizeof(background.path));
    if (ok && version >= 16) ok = fread(&home, 1, sizeof(home), file) == sizeof(home) && backup_home_valid(&home);
    bool has_history = ok && version >= 6;
    if (has_history) {
        history_position = ftell(file);
        ok = history_position >= 0 && book_history_backup_validate(file);
    } else if (ok) ok = fgetc(file) == EOF && !ferror(file);
    if (fclose(file) != 0) ok = false;
    if (!ok || !backup_valid(&backup)) {
        backup_erase_secret(&wifi, sizeof(wifi));
        return ESP_ERR_INVALID_RESPONSE;
    }

    backup.flags[BK_MARGIN] = backup.flags[BK_MARGIN] > 48 ? 48 : backup.flags[BK_MARGIN];
    if (version < 15) backup.flags[BK_SHAKE] = 0; // 旧晃动标志不改变AA。/ Old shake flags never affect AA.
    // 已删除组件的备份仍可恢复，其余字段不受影响。/ Restore retired widget backups without rejecting other preferences.
    if (home.component == 3) home.component = APP_HOME_CLOCK;
    // The backup contains paths, not the corresponding font or image bytes.
    // 备份只含资源路径；资源已被删除时回退到安全的内建选项。
    if (backup.font[0] && !backup_file_exists(backup.font, false)) backup.font[0] = 0;
    if (backup.system_font[0] && !backup_file_exists(backup.system_font, false)) backup.system_font[0] = 0;
    if (backup.wallpaper[0] && !backup_file_exists(backup.wallpaper, false)) {
        backup.wallpaper[0] = 0;
        if (backup.flags[BK_LOCK] == 1) backup.flags[BK_LOCK] = 0;
    }
    if (!backup.wallpaper[0] && backup.flags[BK_LOCK] == 1) backup.flags[BK_LOCK] = 0;
    // 已移除的书架样式保留旧编号的读取兼容，恢复时改为默认亚克力。
    // Accept retired IDs from old backups and restore the default acrylic style.
    if (backup.flags[BK_SHELF] == 3 || backup.flags[BK_SHELF] == 4) backup.flags[BK_SHELF] = 2;
    if (profile.avatar[0] && !backup_file_exists(profile.avatar, false)) profile.avatar[0] = 0;
    if (!backup_file_exists(backup.books_dir, true)) strlcpy(backup.books_dir, "/sdcard/books", sizeof(backup.books_dir));
    if (!backup_file_exists(backup.fonts_dir, true)) strlcpy(backup.fonts_dir, "/sdcard/fonts", sizeof(backup.fonts_dir));

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) { backup_erase_secret(&wifi, sizeof(wifi)); return err; }
#define BACKUP_SET_U8(key, index) do { if (err == ESP_OK) err = nvs_set_u8(h, key, backup.flags[index]); } while (0)
#define BACKUP_SET_STR(key, value) do { if (err == ESP_OK) err = nvs_set_str(h, key, value); } while (0)
    for (unsigned i = 0; i < 3 && err == ESP_OK; ++i) err = nvs_set_u8(h, s_reader_key_names[i], keys.actions[i]);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_HOLD_ACTION, hold.action);
    BACKUP_SET_U8(NVS_KEY_SLEEP, BK_SLEEP);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_SHUTDOWN_MODE, staged_shutdown & 1);
    uint8_t idle_values[] = {0, 1, 5, 10};
    uint8_t idle_minutes = idle_values[(staged_shutdown >> 1) & 3];
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_AUTO_LOCK, idle_minutes);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_BLE_TURNER, (profile.home_full_refresh & 8) != 0);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_HOLD_REFRESH, (profile.home_full_refresh & 16) != 0);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_VERTICAL_TURN, (profile.home_full_refresh & 32) != 0);

    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_HOME_FULL, profile.home_full_refresh & 1);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_MAIN_REFRESH, (uint8_t)main_refresh);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_HOME_COMPONENT, home.component);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_HOME_GOAL, home.minutes);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_HIDE_IMAGES, (profile.home_full_refresh & 2) != 0);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_SHELF_RECENT, (profile.home_full_refresh & 4) != 0);
    if (err == ESP_OK) err = nvs_set_str(h, NVS_KEY_DEVICE_NAME, profile.device_name);
    if (err == ESP_OK) err = nvs_set_str(h, NVS_KEY_AVATAR, profile.avatar);
    if (err == ESP_OK) err = nvs_set_str(h, NVS_KEY_STATUS_SIGNATURE, profile.status_signature);
    BACKUP_SET_U8(NVS_KEY_PICKUP, BK_PICKUP);
    BACKUP_SET_U8(NVS_KEY_SYS_SIZE, BK_SYS_SIZE);
    BACKUP_SET_U8(NVS_KEY_SYS_CONTRAST, BK_SYS_CONTRAST);
    BACKUP_SET_U8(NVS_KEY_LOCK_STYLE, BK_LOCK);
    BACKUP_SET_U8(NVS_KEY_BOOK_PX, BK_BOOK_PX);

    BACKUP_SET_U8(NVS_KEY_READER_FULL, BK_FULL_PAGES);
    BACKUP_SET_U8(NVS_KEY_READER_TURN, BK_TURN);
    BACKUP_SET_U8(NVS_KEY_POWER_TURN, BK_POWER_TURN);
    BACKUP_SET_U8(NVS_KEY_IMMERSIVE, BK_IMMERSIVE);
    BACKUP_SET_U8(NVS_KEY_BOOK_TRACK, BK_TRACKING);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_BOOK_INDENT, indent);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_BOOK_INDENT_ADJUST, adjust.encoded);
    int fine_weight=weight.encoded<=4?((int)weight.encoded-2)*APP_BOOK_WEIGHT_SUBSTEPS:(int)weight.encoded-128-APP_BOOK_WEIGHT_FINE_MAX;
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_BOOK_WEIGHT, fine_weight/APP_BOOK_WEIGHT_SUBSTEPS+2);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_BOOK_WEIGHT_FINE, fine_weight+APP_BOOK_WEIGHT_FINE_MAX);
    const bool antialias = version < 15 || backup.flags[BK_SHAKE] == 0;
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_BOOK_AA, antialias ? 1 : 0);
    BACKUP_SET_U8(NVS_KEY_BOOK_RULE, BK_READING_LINE);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_BOOK_RULE_OFFSET, rule_offset);
    BACKUP_SET_U8(NVS_KEY_BOOK_LINE, BK_LINE_SPACING);
    BACKUP_SET_U8(NVS_KEY_BOOK_MARGIN, BK_MARGIN);
    BACKUP_SET_U8(NVS_KEY_BOOK_PARA, BK_PARAGRAPH);
    BACKUP_SET_U8(NVS_KEY_SHELF_STYLE, BK_SHELF);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_SHELF_V22, 1);
    if (background.path[0] && !backup_file_exists(background.path, false)) background.path[0] = 0;
    BACKUP_SET_STR(NVS_KEY_BOOK_BACKGROUND, background.path);
    BACKUP_SET_STR(NVS_KEY_FONT, backup.font);
    BACKUP_SET_STR(NVS_KEY_SYS_FONT, backup.system_font);
    BACKUP_SET_STR(NVS_KEY_WALLPAPER, backup.wallpaper);
    BACKUP_SET_STR(NVS_KEY_BOOKS_DIR, backup.books_dir);
    BACKUP_SET_STR(NVS_KEY_FONTS_DIR, backup.fonts_dir);
#undef BACKUP_SET_U8
#undef BACKUP_SET_STR
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) { backup_erase_secret(&wifi, sizeof(wifi)); return err; }

    memcpy(s_reader_keys, keys.actions, sizeof(s_reader_keys));
    s_reader_hold_action = hold.action;
    s_book_weight = fine_weight/APP_BOOK_WEIGHT_SUBSTEPS+2;
    s_book_weight_fine = fine_weight;
    s_book_antialias = antialias;
    const uint8_t *f = backup.flags;
    s_sleep = (app_sleep_mode_t)f[BK_SLEEP];
    s_staged_shutdown = (staged_shutdown & 1) != 0;
    s_auto_lock_minutes = idle_minutes;
    s_ble_turner = (profile.home_full_refresh & 8) != 0;
    s_reader_hold_refresh = (profile.home_full_refresh & 16) != 0;
    s_reader_vertical_turn = (profile.home_full_refresh & 32) != 0;
    s_home_full_refresh = (profile.home_full_refresh & 1) != 0;
    s_main_refresh = main_refresh;
    s_reader_hide_images = (profile.home_full_refresh & 2) != 0;
    s_shelf_recent_sort = (profile.home_full_refresh & 4) != 0;
    strlcpy(s_device_name, profile.device_name, sizeof(s_device_name));
    strlcpy(s_avatar, profile.avatar, sizeof(s_avatar));
    strlcpy(s_status_signature, profile.status_signature, sizeof(s_status_signature));
    s_pickup_wake = f[BK_PICKUP];
    s_system_size = f[BK_SYS_SIZE];
    s_system_contrast = f[BK_SYS_CONTRAST];
    s_lock_style = f[BK_LOCK];
    s_book_px = f[BK_BOOK_PX];

    s_reader_full_pages = f[BK_FULL_PAGES];
    s_reader_turn_effect = f[BK_TURN];
    s_reader_power_turn = f[BK_POWER_TURN];
    s_reader_immersive = f[BK_IMMERSIVE];
    s_book_tracking = f[BK_TRACKING];
    s_book_indent = indent;
    s_book_indent_adjust = adjust.encoded;
    s_book_reading_line = f[BK_READING_LINE];
    s_book_rule_offset = rule_offset;
    s_book_line = f[BK_LINE_SPACING];
    s_book_margin = f[BK_MARGIN];
    s_book_para = f[BK_PARAGRAPH];
    s_shelf_style = f[BK_SHELF];
    strlcpy(s_book_background, background.path, sizeof(s_book_background));
    s_home_component = (app_home_component_t)home.component;
    s_home_goal_minutes = home.minutes;
    strlcpy(s_font, backup.font, sizeof(s_font));
    strlcpy(s_system_font, backup.system_font, sizeof(s_system_font));
    strlcpy(s_wallpaper, backup.wallpaper, sizeof(s_wallpaper));
    strlcpy(s_books_dir, backup.books_dir, sizeof(s_books_dir));
    strlcpy(s_fonts_dir, backup.fonts_dir, sizeof(s_fonts_dir));
    if (has_history) {
        file = fopen(source, "rb");
        if (!file) { backup_erase_secret(&wifi, sizeof(wifi)); return ESP_FAIL; }
        esp_err_t history_err = fseek(file, history_position, SEEK_SET) == 0
            ? book_history_backup_restore(file) : ESP_FAIL;
        if (fclose(file) != 0 && history_err == ESP_OK) history_err = ESP_FAIL;
        if (history_err != ESP_OK) { backup_erase_secret(&wifi, sizeof(wifi)); return history_err; }
    }
    if (has_wifi) {
        esp_err_t wifi_err = read_pico_transfer_import_wifi_backup(&wifi.credentials);
        backup_erase_secret(&wifi, sizeof(wifi));
        if (wifi_err != ESP_OK) return wifi_err;
    }
    return ESP_OK;
}
