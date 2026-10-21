/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 图片页：从 TF 卡读取 JPG/PNG，使用独立的完整灰阶刷新。
 * Image page: show SD JPG/PNG with a dedicated full-grayscale refresh.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "app.h"
#include "app_registry.h"
#include "app_content_open.h"
#include "book_cover.h"
#include "display.h"
#include "esp_heap_caps.h"
#include "read_pico_sd.h"
#include "ui_gesture.h"
#include "ui_image_dither.h"
#include "ui_kit.h"
#include "ui_menu.h"
#include "ui_nav.h"

#define IMAGE_MAX 96
#define IMAGE_BYTES_MAX BOOK_IMAGE_FILE_MAX
#define IMAGE_ROWS 8
extern const uint8_t display_test_png_start[] asm("_binary_display_test_png_start");
extern const uint8_t display_test_png_end[] asm("_binary_display_test_png_end");

typedef struct { char name[128]; char path[256]; bool png; } image_item_t;
// 图片列表放 PSRAM，理由同文件列表：内部 RAM 留给只能用内部 RAM 的东西。
// The image list lives in PSRAM for the same reason as the file list: internal RAM is kept for
// what can only live there.
static image_item_t *s_items;

static bool items_alloc(void) {
    if (!s_items)
        s_items = heap_caps_calloc(IMAGE_MAX, sizeof(image_item_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return s_items != NULL;
}
static int s_count, s_page, s_selected;
static uint8_t *s_gray;
static unsigned s_width, s_height;
static char s_message[96];
static bool s_viewing;
static char s_requested_path[256];

bool app_image_request_open(const char *path) {
    if (!path || !path[0] || strnlen(path, sizeof(s_requested_path)) >= sizeof(s_requested_path)) return false;
    snprintf(s_requested_path, sizeof(s_requested_path), "%s", path);
    return true;
}

static bool image_name(const char *name, bool *png) {
    if (!name[0] || name[0] == '.') return false; // 跳过 macOS 辅助文件。/ Skip macOS sidecar files.
    const char *ext = strrchr(name, '.');
    if (!ext) return false;
    *png = !strcasecmp(ext, ".png");
    return *png || !strcasecmp(ext, ".jpg") || !strcasecmp(ext, ".jpeg");
}

static void scan_dir(const char *root) {
    if (!items_alloc()) return;
    DIR *dir = opendir(root);
    if (!dir) return;
    struct dirent *entry;
    while (s_count < IMAGE_MAX && (entry = readdir(dir))) {
        bool png;
        if (!image_name(entry->d_name, &png) || strlen(entry->d_name) >= sizeof(s_items[0].name)) continue;
        image_item_t *item = &s_items[s_count];
        if (snprintf(item->path, sizeof(item->path), "%s/%s", root, entry->d_name) >= (int)sizeof(item->path)) continue;
        struct stat st;
        if (stat(item->path, &st) || !S_ISREG(st.st_mode)) continue;
        // 直接打开的图片已放在首项，目录扫描不再重复加入。/ Do not duplicate the directly opened first item.
        if (s_count && !strcmp(s_items[0].path, item->path)) continue;
        strcpy(item->name, entry->d_name);
        item->png = png;
        ++s_count;
    }
    closedir(dir);
}

static void scan_images(void) {
    s_count = 0;
    if (!items_alloc()) {
        strcpy(s_message, "图片列表内存不足，请返回后重试");
        return;
    }
    // 先分配再访问列表；首次从文件管理进入时此前会解引用空指针。
    // Allocate before touching the list; the first Files entry previously dereferenced NULL.
    if (s_requested_path[0]) {
        const char *name = strrchr(s_requested_path, '/');
        bool png;
        if (!name || !image_name(name + 1, &png)) {
            strcpy(s_message, "图片格式不支持");
            return;
        }
        s_count = 1;
        snprintf(s_items[0].path, sizeof(s_items[0].path), "%s", s_requested_path);
        snprintf(s_items[0].name, sizeof(s_items[0].name), "%s", name + 1);
        s_items[0].png = png;
        char root[256];
        size_t length = (size_t)(name - s_requested_path);
        memcpy(root, s_requested_path, length);
        root[length] = 0;
        scan_dir(root);
        return;
    }
    s_count = 1;
    memset(s_items, 0, sizeof(s_items[0]));
    strcpy(s_items[0].name, "内置灰阶测试图");
    s_items[0].png = true;
    read_pico_sd_info_t sd = {0};
    read_pico_sd_get_info(&sd);
    if (sd.mounted) {
        scan_dir("/sdcard/pictures");
        scan_dir("/sdcard/images");
    }
    if (s_page * IMAGE_ROWS >= s_count) s_page = 0;
}

static bool load_image(int index) {
    free(s_gray); s_gray = NULL;
    s_viewing = false;
    if (!s_items || index < 0 || index >= s_count) return false;
    const image_item_t *item = &s_items[index];
    const uint8_t *data = display_test_png_start;
    size_t size = (size_t)(display_test_png_end - display_test_png_start);
    unsigned width = 0, height = 0;
    bool from_file = item->path[0] != 0;
    if (from_file) {
        struct stat st;
        if (stat(item->path, &st) || st.st_size <= 0 || (uint64_t)st.st_size > IMAGE_BYTES_MAX) {
            strcpy(s_message, "图片超过 50 MB 或无法读取"); return false;
        }
    }
    bool dimensions_ok = from_file ? book_image_file_dimensions(item->path, item->png, &width, &height)
                                  : book_image_dimensions(data, size, item->png, &width, &height);
    if (!dimensions_ok || !width || !height) { strcpy(s_message, "图片格式不支持"); return false; }
    unsigned fit_w = UI_LOCK_WIDTH, fit_h = (uint64_t)height * fit_w / width;
    if (fit_h > UI_LOCK_HEIGHT) { fit_h = UI_LOCK_HEIGHT; fit_w = (uint64_t)width * fit_h / height; }
    if (!fit_w) fit_w = 1;
    if (!fit_h) fit_h = 1;
    s_gray = heap_caps_malloc((size_t)fit_w * fit_h, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    bool ok = s_gray && (from_file ? book_image_file_grayscale(item->path, item->png, fit_w, fit_h, s_gray)
                                  : book_image_grayscale(data, size, item->png, fit_w, fit_h, s_gray));
    if (!ok) { free(s_gray); s_gray = NULL; strcpy(s_message, "解码失败或内存不足"); return false; }
    s_width = fit_w; s_height = fit_h; s_selected = index; s_viewing = true;
    s_message[0] = 0;
    return true;
}

static void render(app_ctx_t *ctx, uint8_t *fb) {
    (void)ctx;
    ui_clear_page(fb);
    if (s_viewing && s_gray) {
        int x0 = (UI_LOCK_WIDTH - s_width) / 2, y0 = (UI_LOCK_HEIGHT - s_height) / 2;
        for (unsigned y = 0; y < s_height; ++y)
            for (unsigned x = 0; x < s_width; ++x)
                epd_draw_pixel(x0 + x, y0 + y,
                    ui_image_dither_gray(s_gray[(size_t)y * s_width + x], x0 + x, y0 + y), fb);
        return;
    }
    ui_nav_status(fb);
    ui_nav_back(fb, 36, 79);
    ui_text_vc(fb, 342, 107, 34, "图片", EPD_DRAW_ALIGN_CENTER, false);
    char caption[64];
    snprintf(caption, sizeof(caption), "%d 张 · JPG / PNG", s_count);
    ui_text(fb, 36, 165, 21, caption, EPD_DRAW_ALIGN_LEFT, false);
    ui_hairline(fb, 202, 36, 612, UI_GRAY_LIGHT);
    for (int row = 0; row < IMAGE_ROWS; ++row) {
        int idx = s_page * IMAGE_ROWS + row;
        if (idx >= s_count) break;
        EpdRect box = {36, 214 + row * 101, 612, 84};
        ui_fill_round_rect(fb, box, 17, UI_GRAY_WHITE);
        ui_text_vc(fb, 58, box.y + 42, 24, s_items[idx].name, EPD_DRAW_ALIGN_LEFT, false);
        ui_text_vc(fb, 623, box.y + 42, 25, "›", EPD_DRAW_ALIGN_RIGHT, false);
    }
    if (s_message[0]) ui_text(fb, 36, 1035, 21, s_message, EPD_DRAW_ALIGN_LEFT, false);
    ui_nav_draw(fb, 3);
}

static bool present(app_ctx_t *ctx, app_redraw_t redraw) {
    if (redraw == APP_REDRAW_NONE) return true;
    if (!s_viewing) return false;
    render(ctx, ctx->fb);
    guard_draw_result(ctx->hl, update_display_image_gray(ctx->hl));
    return true;
}
static void on_enter(app_ctx_t *ctx) {
    (void)ctx;
    s_page = 0; s_viewing = false; s_message[0] = 0; scan_images();
    if (s_requested_path[0]) {
        if (s_count) load_image(0);
        s_requested_path[0] = 0;
    }
}
static void image_on_exit(app_ctx_t *ctx) { (void)ctx; free(s_gray); s_gray = NULL; s_viewing = false; }
static void on_media_lost(app_ctx_t *ctx) { (void)ctx; free(s_gray); s_gray = NULL; s_viewing = false; scan_images(); }
static app_redraw_t move_image(int delta) {
    int next = s_selected + delta;
    if (next < 0 || next >= s_count) return APP_REDRAW_NONE;
    if (!load_image(next)) s_viewing = false;
    return APP_REDRAW_FULL;
}
static app_redraw_t on_key(app_ctx_t *ctx, int key) {
    if (key == UI_KEY_2) { ctx->request_app = app_home_page(); return APP_REDRAW_NONE; }
    if (s_viewing) {
        s_viewing = false; free(s_gray); s_gray = NULL; return APP_REDRAW_PAGE;
    }
    ctx->request_return = true;
    return APP_REDRAW_NONE;
}
static app_redraw_t on_gesture(app_ctx_t *ctx, const ui_gesture_event_t *event) {
    (void)ctx;
    if (s_viewing) {
        if (event->type == UI_GESTURE_SWIPE_L) return move_image(1);
        if (event->type == UI_GESTURE_SWIPE_R) return move_image(-1);
        if (event->type != UI_GESTURE_TAP) return APP_REDRAW_NONE;
        if (event->x0 < UI_LOCK_WIDTH / 3) return move_image(-1);
        if (event->x0 > UI_LOCK_WIDTH * 2 / 3) return move_image(1);
        s_viewing = false; free(s_gray); s_gray = NULL; return APP_REDRAW_PAGE;
    }
    if (event->type == UI_GESTURE_SWIPE_L && (s_page + 1) * IMAGE_ROWS < s_count) {
        ++s_page;
        return APP_REDRAW_PAGE;
    }
    if (event->type == UI_GESTURE_SWIPE_R && s_page > 0) {
        --s_page;
        return APP_REDRAW_PAGE;
    }
    if (event->type != UI_GESTURE_TAP) return APP_REDRAW_NONE;
    if (event->y0 < 160 && event->x0 < 120) { ctx->request_return = true; return APP_REDRAW_NONE; }
    int tab = ui_nav_hit(event->x0, event->y0);
    if (tab >= 0) { ui_nav_request(ctx, tab); return APP_REDRAW_NONE; }
    for (int row = 0; row < IMAGE_ROWS; ++row) {
        int idx = s_page * IMAGE_ROWS + row;
        if (idx >= s_count) break;
        if (ui_rect_hit((EpdRect){36, 214 + row * 101, 612, 84}, event->x0, event->y0)) {
            if (!load_image(idx)) s_viewing = false;
            return APP_REDRAW_FULL;
        }
    }
    return APP_REDRAW_NONE;
}
static bool menu_handle_enabled(app_ctx_t *ctx) { (void)ctx; return false; }
const app_desc_t app_image = {
    .title = "图片对比 Images", .detail = "灰阶测试图 / TF 卡 JPG PNG", .enter_full = false,
    .owns_keys = true, .menu_handle_enabled = menu_handle_enabled,
    .on_enter = on_enter, .on_exit = image_on_exit, .on_media_lost = on_media_lost,
    .render = render, .present = present, .on_key = on_key,
    .on_gesture = on_gesture,
};
