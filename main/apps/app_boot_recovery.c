/* SPDX-License-Identifier: Apache-2.0
 * 中文：启动中断的图书提示及明确确认后的单文件删除；不解析故障书、不自动删除。
 * English: Show interrupted book opening and offer confirmed single-file deletion; never parse the failed book or delete automatically.
 * 冻结：只处理启动记录中确切的普通 TXT/EPUB；无卡/删除失败可重试；绘制无文件操作。
 * Frozen: Handle only the exact regular TXT/EPUB in the startup record; allow retries after absent media or failed deletion. Rendering has no file operations.
 */
#include "app.h"
#include "app_registry.h"
#include "boot_state.h"
#include "book_progress.h"
#include "book_store.h"
#include "read_pico_sd.h"
#include "ui_gesture.h"
#include "ui_kit.h"
#include "ui_nav.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static char s_path[PICO_BOOT_PATH_MAX], s_message[128];
static bool s_confirm, s_removed;
static void fit(char *text, int width) {
    while (*text && ui_text_fixed_width_px(ui_text_effective_px(27), text) > width) {
        size_t n = strlen(text) - 1;
        while (n && ((unsigned char)text[n] & 0xc0) == 0x80) --n;
        text[n] = 0;
    }
}
static void render(app_ctx_t *ctx, uint8_t *fb) {
    (void)ctx;
    ui_clear_page(fb);
    ui_nav_status(fb);
    ui_nav_back(fb, 58, 117);
    ui_text_vc(fb, 342, 117, 36, "启动恢复", EPD_DRAW_ALIGN_CENTER, false);
    ui_fill_rect_fast((EpdRect){36, 195, 612, 2}, 0x58, fb);
    EpdRect card = {36, 244, 612, 320};
    ui_fill_round_rect(fb, card, 24, 0xf0);
    ui_draw_round_rect(fb, card, 24, 0x38);
    ui_text(fb, 58, 270, 30, s_removed ? "书籍已删除" : s_confirm ? "确认删除这本书？" : "上次打开此书时中断", EPD_DRAW_ALIGN_LEFT, false);
    const char *name = strrchr(s_path, '/');
    char label[288]; snprintf(label, sizeof(label), "%s", name ? name + 1 : s_path);
    fit(label, 560);
    ui_text(fb, 58, 345, 27, label, EPD_DRAW_ALIGN_LEFT, false);
    ui_text(fb, 58, 423, 24, s_confirm ? "删除后需要重新导入这本书。" : "可能是开书时断电或解析异常。", EPD_DRAW_ALIGN_LEFT, false);
    ui_text(fb, 58, 482, 24, s_confirm ? "其他书籍和设置会保留。" : "已停止自动重试，可先返回首页。", EPD_DRAW_ALIGN_LEFT, false);
    if (s_message[0]) ui_text(fb, 58, 609, 24, s_message, EPD_DRAW_ALIGN_LEFT, false);
    EpdRect back = {36, 710, 612, 82}, remove = {36, 820, 612, 82};
    ui_fill_round_rect(fb, back, 20, 0xf0); ui_draw_round_rect(fb, back, 20, 0x38);
    ui_text_vc(fb, 342, 751, 28, s_confirm ? "取消删除" : "返回首页", EPD_DRAW_ALIGN_CENTER, false);
    if (!s_removed) {
        ui_fill_round_rect(fb, remove, 20, UI_GRAY_BLACK);
        ui_text_vc(fb, 342, 861, 28, s_confirm ? "确认删除" : "删除这本书", EPD_DRAW_ALIGN_CENTER, true);
    }
    ui_nav_draw(fb, 0);
}
static esp_err_t delete_book(void) {
    char recorded[PICO_BOOT_PATH_MAX];
    if (!pico_boot_interrupted_book(recorded, sizeof(recorded)) || strcmp(recorded, s_path)) return ESP_ERR_INVALID_STATE;
    const char *name = strrchr(s_path, '/'), *ext = strrchr(s_path, '.');
    if (!name || !ext || (strcasecmp(ext, ".epub") && strcasecmp(ext, ".txt"))) return ESP_ERR_INVALID_ARG;
    if (!strncmp(s_path, "/sdcard/", 8)) {
        read_pico_sd_info_t sd = {0}; read_pico_sd_get_info(&sd);
        if (!sd.present || !sd.mounted) return ESP_ERR_INVALID_STATE;
    } else if (!book_store_flash_ready()) {
        book_store_root_t roots[BOOK_STORE_ROOT_MAX]; int n;
        if (book_store_roots(roots, &n) != ESP_OK || !book_store_flash_ready()) return ESP_ERR_INVALID_STATE;
    }
    struct stat st;
#ifdef ESP_PLATFORM
    int found = stat(s_path, &st);
#else
    int found = lstat(s_path, &st);
#endif
    if (found && errno != ENOENT) return ESP_FAIL;
    if (!found && !S_ISREG(st.st_mode)) return ESP_ERR_INVALID_ARG;
    // 同名替换备份也先移除，防止传书恢复流程重新生成故障书。
    // Remove a same-name replacement backup first so transfer recovery cannot resurrect the book.
    if (strlen(name + 1) + strlen(".rename-backup") <= 255) {
        char backup[PICO_BOOT_PATH_MAX + 16];
        snprintf(backup, sizeof(backup), "%s.rename-backup", s_path);
#ifdef ESP_PLATFORM
        int have_backup = stat(backup, &st);
#else
        int have_backup = lstat(backup, &st);
#endif
        if (!have_backup && (!S_ISREG(st.st_mode) || unlink(backup))) return ESP_FAIL;
        if (have_backup && errno != ENOENT) return ESP_FAIL;
    }
    if (!found && unlink(s_path)) return ESP_FAIL;
    s_removed = true;
    book_store_notify_changed();
    esp_err_t err = book_progress_forget(s_path);
    pico_boot_forget_interrupted_book();
    return err;
}
static void on_enter(app_ctx_t *ctx) {
    (void)ctx;
    s_confirm = s_removed = false;
    s_message[0] = 0;
    if (!pico_boot_interrupted_book(s_path, sizeof(s_path))) s_path[0] = 0;
}
static app_redraw_t on_gesture(app_ctx_t *ctx, const ui_gesture_event_t *ev) {
    if (ev->type != UI_GESTURE_TAP) return APP_REDRAW_NONE;
    int tab = ui_nav_hit(ev->x0, ev->y0);
    if (tab >= 0) { ui_nav_request(ctx, tab); return APP_REDRAW_NONE; }
    if (ui_rect_hit((EpdRect){26, 82, 64, 64}, ev->x0, ev->y0) ||
        ui_rect_hit((EpdRect){36, 710, 612, 82}, ev->x0, ev->y0)) {
        if (s_confirm) { s_confirm = false; return APP_REDRAW_PAGE; }
        ctx->request_app = app_home_page(); return APP_REDRAW_NONE;
    }
    if (!s_removed && ui_rect_hit((EpdRect){36, 820, 612, 82}, ev->x0, ev->y0)) {
        if (!s_confirm) { s_confirm = true; return APP_REDRAW_PAGE; }
        esp_err_t err = delete_book();
        snprintf(s_message, sizeof(s_message), "%s", err == ESP_OK ? "已删除，可返回首页" :
            s_removed ? "已删除，阅读记录清理失败" : err == ESP_ERR_INVALID_STATE ? "存储未就绪，请插卡后重试" : "删除失败，请重试或用文件管理处理");
        if (s_removed) s_confirm = false;
        return APP_REDRAW_PAGE;
    }
    return APP_REDRAW_NONE;
}
static app_redraw_t on_key(app_ctx_t *ctx, int key) {
    (void)key; ctx->request_app = app_home_page(); return APP_REDRAW_NONE;
}
const app_desc_t app_boot_recovery = {
    .title = "启动恢复", .detail = "中断图书恢复", .owns_keys = true,
    .on_enter = on_enter, .render = render, .on_gesture = on_gesture, .on_key = on_key,
};
