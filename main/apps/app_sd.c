/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 存储卡与蜂鸣器。卡状态、重新挂载、格式化，蜂鸣单独一栏。
 * 格式化和重挂载都会重扫字体，没字体时这一页是唯一能自救的入口。
 *
 * Storage card and buzzer. Card status, remount, format; buzzer is its own
 * section. Format and remount both rescan fonts; with no font this page is
 * the only self-rescue entry.
 *
 * 冻结：无字体走 ui_draw_no_font_page；格式化两次确认；
 * 维护顶边跟卡状态高度走，放不下两行按钮就并成一排；
 * 曲谱固定明亮/衰/+24，按钮「曲谱 Score」；探针 A/B/1b/4k；
 * 底栏只留菜单把手。KEY3 走主循环菜单把手。
 * Frozen: no-font uses ui_draw_no_font_page; format needs two confirm taps;
 * maintain section top follows card-status height; if two stacked buttons do
 * not fit, put them in one row; score is fixed bright/decay/+24, button
 * "曲谱 Score"; probes A/B/1b/4k; bar is the menu handle only.
 * KEY3 is the loop menu handle.
 */

#include <stdio.h>
#include <string.h>

#include "app.h"
#include "display.h"
#include "esp_log.h"
#include "read_pico_buzzer.h"
#include "read_pico_sd.h"
#include "ttf_font.h"
#include "ui_kit.h"
#include "ui_menu.h"

extern const app_desc_t app_sd;

#define TAG "app_sd"

#define SD_TITLE "TF 卡与蜂鸣器 Storage"
#define SD_PROBE_POLL_MS 500
#define SD_READ_TIMEOUT_MS 8000
#define SD_BTN_GAP UI_GAP
#define SD_DESC_H (UI_PX_BODY + 10 + UI_PX_CAPTION)

#define SD_HIT_NONE (-1)
#define SD_HIT_REMOUNT 0
#define SD_HIT_FORMAT 1
#define SD_HIT_BUZZER 2
#define SD_HIT_PROBE_A 3
#define SD_HIT_PROBE_B 4
#define SD_HIT_PROBE_1B 5
#define SD_HIT_PROBE_4K 6
#define SD_PROBE_H 56
#define SD_PROBE_N 4

typedef struct {
    int status_y;
    int maint_y;
    int buzz_y;
    int desc_y;
    bool pair;
    EpdRect remount;
    EpdRect format;
    EpdRect beep;
    EpdRect probe[SD_PROBE_N];
} sd_geom_t;

static bool s_probe_pending;
static bool s_format_confirm;
static int64_t s_last_probe_ms;
static int64_t s_read_t0_ms;
static char s_status[24];
// 无字体时整页由 ui_kit 画，命中也要转给它，否则点到的是文字版矩形。/ No-font page is drawn by ui_kit; hit-test must go there too, or taps land on the text-layout rects.
static bool s_sd_no_font;

static const char* present_text(bool present) {
    return present ? "已插入 Inserted" : "未找到 Missing";
}

static const char* fs_text(const read_pico_sd_info_t* info) {
    if (info->error == ESP_ERR_NOT_FINISHED) return "检查中 Checking";
    return info->mounted ? "已挂载 Mounted" : "未挂载 Unmounted";
}

static const char* progress_text(const read_pico_sd_info_t* info, const char* status) {
    if (status != NULL && status[0] != '\0') return status;
    if (info->needs_format) return "请格式化 Format";
    return "-";
}

static EpdRect sd_btn_rect(int y) {
    return (EpdRect){
        .x = UI_MARGIN,
        .y = y,
        .width = ui_content_width(),
        .height = UI_BTN_H,
    };
}

static int status_h(const read_pico_sd_info_t* info, const char* status) {
    const char* progress = progress_text(info, status);
    int h = UI_SEC_HEAD + 2 * UI_ROW_H;
    if (info->mounted) return h + 2 * UI_ROW_H_SM;
    if (info->error != ESP_ERR_NOT_FINISHED && info->error != ESP_OK) {
        return h + UI_ROW_H_SM;
    }
    if (progress[0] != '-') return h + UI_ROW_H;
    return h;
}

static sd_geom_t sd_geom(const read_pico_sd_info_t* info, const char* status) {
    const int desc_y = UI_CONTENT_BOTTOM - UI_SEC_HEAD - SD_DESC_H;
    const int buzz_h = UI_SEC_HEAD + UI_BTN_H + SD_BTN_GAP + SD_PROBE_H;
    const int buzz_y = desc_y - UI_SECTION_GAP - buzz_h;
    const int maint_y = UI_CONTENT_TOP + status_h(info, status) + UI_SECTION_GAP;
    const int room = buzz_y - UI_SECTION_GAP - maint_y;
    const int stacked = UI_SEC_HEAD + 2 * UI_BTN_H + SD_BTN_GAP;
    const bool pair = room < stacked;
    const int btn0_y = maint_y + UI_SEC_HEAD;
    const int btn1_y = btn0_y + UI_BTN_H + SD_BTN_GAP;
    const int beep_y = buzz_y + UI_SEC_HEAD;
    const int probe_y = beep_y + UI_BTN_H + SD_BTN_GAP;
    const int gap = 6;
    const int pw = (ui_content_width() - (SD_PROBE_N - 1) * gap) / SD_PROBE_N;
    sd_geom_t g = {
        .status_y = UI_CONTENT_TOP,
        .maint_y = maint_y,
        .buzz_y = buzz_y,
        .desc_y = desc_y,
        .pair = pair,
        .remount = pair ? ui_row_rect(0, 2, btn0_y, UI_BTN_H) : sd_btn_rect(btn0_y),
        .format = pair ? ui_row_rect(1, 2, btn0_y, UI_BTN_H) : sd_btn_rect(btn1_y),
        .beep = sd_btn_rect(beep_y),
    };
    for (int i = 0; i < SD_PROBE_N; i++) {
        g.probe[i] = (EpdRect){
            .x = UI_MARGIN + i * (pw + gap),
            .y = probe_y,
            .width = pw,
            .height = SD_PROBE_H,
        };
    }
    return g;
}

static void draw_status(
    uint8_t* framebuffer, int y, const read_pico_sd_info_t* info, const char* status
) {
    char cap[24];
    char avail[24];
    const char* progress = progress_text(info, status);

    y = ui_draw_section(framebuffer, y, "卡状态 Card");
    y = ui_draw_row(framebuffer, y, "卡片 Card", present_text(info->present));
    y = ui_draw_row(framebuffer, y, "文件系统 FS", fs_text(info));
    if (info->mounted) {
        snprintf(
            cap, sizeof(cap), "%llu MB",
            (unsigned long long)(info->capacity_bytes / (1024 * 1024))
        );
        snprintf(
            avail, sizeof(avail), "%llu MB",
            (unsigned long long)(info->free_bytes / (1024 * 1024))
        );
        y = ui_draw_row2(
            framebuffer, y, "名称 Name", info->name, "容量 Size", cap
        );
        ui_draw_row2(
            framebuffer, y, "剩余 Free", avail, "进度 Status", progress
        );
        return;
    }
    if (info->error != ESP_ERR_NOT_FINISHED && info->error != ESP_OK) {
        snprintf(cap, sizeof(cap), "%d", info->error);
        ui_draw_row2(
            framebuffer, y, "错误 Error", cap, "进度 Status", progress
        );
        return;
    }
    if (progress[0] != '-') {
        ui_draw_row(framebuffer, y, "进度 Status", progress);
    }
}

static void draw_sd_page(
    uint8_t* framebuffer, const read_pico_sd_info_t* info, const char* status,
    bool format_confirm
) {
    ui_clear_page(framebuffer);

    if (!ttf_font_ready()) {
        ui_draw_no_font_page(framebuffer, info->present, format_confirm);
        s_sd_no_font = true;
        return;
    }
    s_sd_no_font = false;

    const sd_geom_t g = sd_geom(info, status);
    ui_draw_header(framebuffer, SD_TITLE, app_sd.detail);
    draw_status(framebuffer, g.status_y, info, status);

    ui_draw_section(framebuffer, g.maint_y, "维护 Maintain");
    ui_draw_button(framebuffer, g.remount, "重新读取 Remount", false);
    if (info->present) {
        ui_draw_button(
            framebuffer, g.format,
            format_confirm ? "再点一次确认 Confirm" : "格式化 Format",
            format_confirm || info->needs_format
        );
    } else if (!g.pair) {
        ui_text(
            framebuffer, UI_MARGIN, g.format.y + 24, UI_PX_BODY,
            "请插入有字体目录的存储卡。", EPD_DRAW_ALIGN_LEFT, false
        );
    } else {
        ui_text_vc(
            framebuffer, g.format.x + g.format.width / 2,
            g.format.y + g.format.height / 2, UI_PX_CAPTION,
            "请插入存储卡 Insert card", EPD_DRAW_ALIGN_CENTER, false
        );
    }

    ui_draw_section(framebuffer, g.buzz_y, "蜂鸣器 Buzzer");
    ui_draw_button(framebuffer, g.beep, "曲谱 Score", false);
    ui_draw_button(framebuffer, g.probe[0], "A", false);
    ui_draw_button(framebuffer, g.probe[1], "B", false);
    ui_draw_button(framebuffer, g.probe[2], "1b", false);
    ui_draw_button(framebuffer, g.probe[3], "4k", false);

    int y_desc = ui_draw_section(framebuffer, g.desc_y, "说明 Description");
    ui_text(
        framebuffer, UI_MARGIN, y_desc, UI_PX_BODY,
        "重挂载会重扫字体。格式化清卡。", EPD_DRAW_ALIGN_LEFT, false
    );
    ui_text(
        framebuffer, UI_MARGIN, y_desc + UI_PX_BODY + 10, UI_PX_CAPTION,
        "Remount rescans fonts. Format erases the card.",
        EPD_DRAW_ALIGN_LEFT, false
    );

    ui_draw_menu_handle(framebuffer, false);
}

static int sd_hit_test(const sd_geom_t* g, bool format_ok, uint16_t x, uint16_t y) {
    if (s_sd_no_font) {
        switch (ui_no_font_hit_test(x, y)) {
            case UI_NO_FONT_HIT_REMOUNT: return SD_HIT_REMOUNT;
            case UI_NO_FONT_HIT_FORMAT: return SD_HIT_FORMAT;
            default: return SD_HIT_NONE;
        }
    }
    if (ui_rect_hit(g->remount, x, y)) return SD_HIT_REMOUNT;
    if (format_ok && ui_rect_hit(g->format, x, y)) return SD_HIT_FORMAT;
    if (ui_rect_hit(g->beep, x, y)) return SD_HIT_BUZZER;
    if (ui_rect_hit(g->probe[0], x, y)) return SD_HIT_PROBE_A;
    if (ui_rect_hit(g->probe[1], x, y)) return SD_HIT_PROBE_B;
    if (ui_rect_hit(g->probe[2], x, y)) return SD_HIT_PROBE_1B;
    if (ui_rect_hit(g->probe[3], x, y)) return SD_HIT_PROBE_4K;
    return SD_HIT_NONE;
}

static const char* status_now(void) {
    return s_status[0] != '\0' ? s_status : NULL;
}

static void status_set(const char* text) {
    if (text == NULL || text[0] == '\0') {
        s_status[0] = '\0';
        return;
    }
    strlcpy(s_status, text, sizeof(s_status));
}

static void render(app_ctx_t* ctx, uint8_t* fb) {
    (void)ctx;
    read_pico_sd_info_t info = { 0 };
    read_pico_sd_get_info(&info);
    draw_sd_page(fb, &info, status_now(), s_format_confirm);
}

static void draw_now(app_ctx_t* ctx, const read_pico_sd_info_t* info, const char* status) {
    draw_sd_page(ctx->fb, info, status, s_format_confirm);
    guard_draw_result(ctx->hl, update_display_mode(ctx->hl, APP_PAGE_REFRESH_MODE));
}

static void on_enter(app_ctx_t* ctx) {
    (void)ctx;
    s_format_confirm = false;
    status_set(NULL);
    s_read_t0_ms = 0;
    read_pico_sd_start_probe();
    read_pico_sd_info_t info = { 0 };
    esp_err_t err = read_pico_sd_get_info(&info);
    s_probe_pending = err == ESP_ERR_NOT_FINISHED;
    s_last_probe_ms = 0;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD card status: %s", esp_err_to_name(err));
    }
}

static app_redraw_t on_touch(app_ctx_t* ctx, const cst836u_touch_t* touch) {
    read_pico_sd_info_t info = { 0 };
    read_pico_sd_get_info(&info);
    const sd_geom_t g = sd_geom(&info, NULL);
    int hit = sd_hit_test(&g, info.present, touch->x, touch->y);

    if (hit == SD_HIT_BUZZER) {
        esp_err_t err = read_pico_buzzer_score_play();
        if (err != ESP_OK) ESP_LOGE(TAG, "Buzzer failed: %s", esp_err_to_name(err));
        return APP_REDRAW_NONE;
    }
    if (hit == SD_HIT_PROBE_A || hit == SD_HIT_PROBE_B || hit == SD_HIT_PROBE_1B ||
        hit == SD_HIT_PROBE_4K) {
        read_pico_buzzer_probe_t p = READ_PICO_BUZZER_PROBE_A_15K;
        if (hit == SD_HIT_PROBE_B) p = READ_PICO_BUZZER_PROBE_B_15K;
        if (hit == SD_HIT_PROBE_1B) p = READ_PICO_BUZZER_PROBE_1BIT;
        if (hit == SD_HIT_PROBE_4K) p = READ_PICO_BUZZER_PROBE_4K;
        esp_err_t err = read_pico_buzzer_probe(p);
        if (err != ESP_OK) ESP_LOGE(TAG, "probe %s", esp_err_to_name(err));
        return APP_REDRAW_NONE;
    }
    if (hit == SD_HIT_REMOUNT) {
        s_format_confirm = false;
        status_set("读取 Read");
        s_read_t0_ms = ctx->now_ms;
        draw_now(ctx, &info, status_now());
        ttf_font_unload();
        ttf_font_init();
        esp_err_t err = read_pico_sd_remount();
        s_probe_pending = err == ESP_ERR_NOT_FINISHED;
        s_last_probe_ms = 0;
        ESP_LOGI(TAG, "SD remount %s", esp_err_to_name(err));
        if (!s_probe_pending) {
            status_set(NULL);
            s_read_t0_ms = 0;
            read_pico_sd_get_info(&info);
            draw_now(ctx, &info, NULL);
        }
        return APP_REDRAW_DONE;
    }
    if (hit != SD_HIT_FORMAT) return APP_REDRAW_NONE;

    if (!s_format_confirm) {
        s_format_confirm = true;
        draw_now(ctx, &info, NULL);
        return APP_REDRAW_DONE;
    }
    s_format_confirm = false;
    draw_now(ctx, &info, "正在格式化… Formatting");
    ttf_font_unload();
    esp_err_t err = read_pico_sd_format();
    ttf_font_init();
    read_pico_sd_get_info(&info);
    draw_now(ctx, &info, err == ESP_OK ? "格式化完成 Done" : "格式化失败 Fail");
    ESP_LOGI(TAG, "SD format %s", esp_err_to_name(err));
    return APP_REDRAW_DONE;
}

static app_redraw_t on_tick(app_ctx_t* ctx) {
    if (!s_probe_pending) return APP_REDRAW_NONE;
    if (ctx->now_ms - s_last_probe_ms < SD_PROBE_POLL_MS) return APP_REDRAW_NONE;
    s_last_probe_ms = ctx->now_ms;

    read_pico_sd_info_t info = { 0 };
    esp_err_t err = read_pico_sd_get_info(&info);
    if (err == ESP_ERR_NOT_FINISHED) {
        if (s_read_t0_ms != 0
            && ctx->now_ms - s_read_t0_ms >= SD_READ_TIMEOUT_MS) {
            s_read_t0_ms = 0;
            status_set("超时 Timeout");
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    s_probe_pending = false;
    s_read_t0_ms = 0;
    status_set(NULL);
    if (info.mounted) ttf_font_init();
    ESP_LOGI(TAG, "SD probe complete: %s", esp_err_to_name(err));
    return APP_REDRAW_PAGE;
}

const app_desc_t app_sd = {
    .title = SD_TITLE,
    .detail = "存储卡与蜂鸣器 Storage & Buzzer",
    .render = render,
    .on_enter = on_enter,
    .on_touch = on_touch,
    .on_tick = on_tick,
};
