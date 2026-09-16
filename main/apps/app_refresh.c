/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 墨水屏刷新。四个按钮 2×2：整屏全刷、局部快刷、16 灰阶图（默认裁剪表）、
 * 8 灰阶图（原厂 30 相快表），各自报耗时。
 *
 * EPD refresh. Four buttons in 2×2: full-screen, partial, 16-gray (default
 * cropped table), 8-gray (factory 30-phase fast table); each reports timing.
 */

#include <stdio.h>

#include "app.h"
#include "display.h"
#include "e0470_epaper_waveform.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ui_kit.h"
#include "ui_menu.h"

#define TAG "app_refresh"

#define DEMO_ACT_FULL 0
#define DEMO_ACT_AREA 1
#define DEMO_ACT_GRAY16 2
#define DEMO_ACT_GRAY8 3
#define DEMO_ACT_COUNT 4

static int32_t s_demo_ms[DEMO_ACT_COUNT];
static int s_demo_last = -1;
static bool s_demo_invert;
// 测试图正占着整屏。留着不自动退回，方便对着实物看效果；下一次点屏幕才退回页面。
// Pattern is occupying the full screen. Stay there so the panel can be judged; the next tap returns to the page.
static bool s_demo_pattern;

// 四个按钮 2×2 排：下面一行就是底栏（右端仍留给菜单钮），上面一行同宽往上挪一格。
// Four buttons in 2×2: the lower row is the bar (right end still the menu button); the upper row is the same width, one cell up.
static EpdRect demo_btn_rect(int i) {
    EpdRect rect = ui_bar_rect(i % 2, 2);
    if (i < 2) rect.y -= UI_BAR_H + UI_GAP;
    return rect;
}

static int demo_btn_hit(uint16_t x, uint16_t y) {
    for (int i = 0; i < DEMO_ACT_COUNT; i++) {
        if (ui_rect_hit(demo_btn_rect(i), x, y)) return i;
    }
    return -1;
}

// 局部快刷示范的区域：耗时四行下面一条，够看清 DU 的边界又不占满整页。/ Partial-refresh demo band: one strip under the four timing rows; enough to see the DU edge without filling the page.
static EpdRect demo_area(void) {
    const int w = ui_content_width();
    return (EpdRect){
        .x = UI_MARGIN,
        .y = UI_CONTENT_TOP + UI_SEC_HEAD + DEMO_ACT_COUNT * UI_ROW_H,
        .width = w,
        .height = 220,
    };
}

static void draw_pattern_page(uint8_t* framebuffer, const char* caption) {
    const int width = epd_rotated_display_width();
    const int inner = width - 2 * UI_MARGIN;

    ui_clear_page(framebuffer);
    int y = ui_draw_header(framebuffer, "刷新图案 Refresh Pattern", caption);

    const int bar_h = 240;
    const int step = inner / 16;
    for (int i = 0; i < 16; i++) {
        epd_fill_rect(
            (EpdRect){
                .x = UI_MARGIN + i * step,
                .y = y,
                .width = step,
                .height = bar_h,
            },
            (uint8_t)(i * 17),
            framebuffer
        );
    }
    y += bar_h + 12;
    ui_text(
        framebuffer, UI_MARGIN, y, UI_PX_CAPTION, "灰阶 0 到 15 Gray 0 to 15",
        EPD_DRAW_ALIGN_LEFT, false
    );
    y += 44;

    const int block_h = 200;
    epd_fill_rect(
        (EpdRect){
            .x = UI_MARGIN,
            .y = y,
            .width = inner / 2 - 10,
            .height = block_h,
        },
        UI_GRAY_BLACK,
        framebuffer
    );
    epd_draw_rect(
        (EpdRect){
            .x = UI_MARGIN + inner / 2 + 10,
            .y = y,
            .width = inner / 2 - 10,
            .height = block_h,
        },
        UI_GRAY_BLACK,
        framebuffer
    );
    y += block_h + 40;

    const int cell = 40;
    const int board_h = 160;
    for (int row = 0; row * cell < board_h; row++) {
        for (int col = 0; col * cell < inner; col++) {
            if ((row + col) % 2) continue;
            epd_fill_rect(
                (EpdRect){
                    .x = UI_MARGIN + col * cell,
                    .y = y + row * cell,
                    .width = cell,
                    .height = cell,
                },
                UI_GRAY_BLACK,
                framebuffer
            );
        }
    }
    y += board_h + 30;

    for (int i = 0; i < 20; i++) {
        epd_fill_rect(
            (EpdRect){
                .x = UI_MARGIN,
                .y = y + i * 6,
                .width = inner,
                .height = 1,
            },
            UI_GRAY_BLACK,
            framebuffer
        );
    }
    y += 20 * 6 + 24;
    ui_text(
        framebuffer, UI_MARGIN, y, 30, "0123456789 文字锐度 Text Sharpness",
        EPD_DRAW_ALIGN_LEFT, false
    );
    ui_draw_menu_handle(framebuffer, false);
}

static void draw_demo_page(uint8_t* fb) {
    static const char* const names[DEMO_ACT_COUNT] = {
        "整屏全刷 Full", "局部快刷 Partial", "16 灰阶图 16-Gray", "8 灰阶图 8-Gray",
    };
    char line[48];
    ui_clear_page(fb);
    int y = ui_draw_header(fb, "墨水屏刷新 EPD Refresh", "刷新耗时 Refresh Timing");
    y = ui_draw_section(fb, y, "耗时 Timing");
    for (int i = 0; i < DEMO_ACT_COUNT; i++) {
        if (s_demo_ms[i] > 0) {
            snprintf(line, sizeof(line), "%ld ms", (long)s_demo_ms[i]);
        } else {
            snprintf(line, sizeof(line), "—");
        }
        y = ui_draw_row(fb, y, names[i], line);
    }

    EpdRect area = demo_area();
    epd_fill_rect(area, s_demo_invert ? UI_GRAY_BLACK : UI_GRAY_WHITE, fb);
    epd_draw_rect(area, UI_GRAY_BLACK, fb);
    ui_text_vc(
        fb, area.x + area.width / 2, area.y + area.height / 2, UI_PX_SUB,
        "局部快刷区 Partial Area", EPD_DRAW_ALIGN_CENTER, s_demo_invert
    );

    for (int i = 0; i < DEMO_ACT_COUNT; i++) {
        ui_draw_button(fb, demo_btn_rect(i), names[i], s_demo_last == i);
    }
    ui_draw_menu_handle(fb, false);
}

static void demo_render(app_ctx_t* ctx, uint8_t* fb) {
    (void)ctx;
    s_demo_pattern = false;
    draw_demo_page(fb);
}

// 中间灰不能当参考帧。先 GC16 铺白，再从白底出下一屏。/ Mid-gray cannot be a reference frame. GC16 to white first, then present the next screen from white.
static enum EpdDrawError flash_white(app_ctx_t* ctx) {
    display_hold_white_exit(false);
    return update_display_white(ctx->hl);
}

static enum EpdDrawError leave_gray_to_demo(app_ctx_t* ctx) {
    s_demo_pattern = false;
    enum EpdDrawError err = flash_white(ctx);
    if (err != EPD_DRAW_SUCCESS) return err;
    draw_demo_page(ctx->fb);
    return update_display_from_white(ctx->hl);
}

static void demo_on_exit(app_ctx_t* ctx) {
    s_demo_pattern = false;
    if (display_take_white_exit()) {
        guard_draw_result(ctx->hl, update_display_white(ctx->hl));
    }
}

static bool demo_present(app_ctx_t* ctx, app_redraw_t redraw) {
    if (redraw == APP_REDRAW_DONE) return true;
    if (redraw != APP_REDRAW_PAGE && redraw != APP_REDRAW_FULL) return false;
    if (s_demo_pattern && display_take_white_exit()) {
        guard_draw_result(ctx->hl, leave_gray_to_demo(ctx));
        return true;
    }
    s_demo_pattern = false;
    return false;
}

static app_redraw_t demo_touch(app_ctx_t* ctx, const cst836u_touch_t* touch) {
    if (s_demo_pattern) {
        guard_draw_result(ctx->hl, leave_gray_to_demo(ctx));
        return APP_REDRAW_DONE;
    }

    int hit = demo_btn_hit(touch->x, touch->y);
    if (hit < 0) return APP_REDRAW_NONE;
    s_demo_last = hit;

    int64_t started_us;
    enum EpdDrawError err = EPD_DRAW_SUCCESS;
    switch (hit) {
        case DEMO_ACT_FULL:
            draw_demo_page(ctx->fb);
            started_us = esp_timer_get_time();
            err = update_display_full(ctx->hl);
            break;
        case DEMO_ACT_AREA:
            s_demo_invert = !s_demo_invert;
            draw_demo_page(ctx->fb);
            started_us = esp_timer_get_time();
            err = update_display_area_with(ctx->hl, &E0470_WAVEFORM, MODE_DU, demo_area());
            break;
        case DEMO_ACT_GRAY8:
        case DEMO_ACT_GRAY16:
            guard_draw_result(ctx->hl, flash_white(ctx));
            draw_pattern_page(
                ctx->fb,
                hit == DEMO_ACT_GRAY8
                    ? "8 灰阶 30 相 8-Gray 30P　点按返回 Tap to Return"
                    : "16 灰阶 36 相 16-Gray 36P　点按返回 Tap to Return"
            );
            s_demo_pattern = true;
            display_hold_white_exit(true);
            started_us = esp_timer_get_time();
            err = hit == DEMO_ACT_GRAY8
                ? update_display_from_white_with(
                    ctx->hl, &E0470_GRAY8_WAVEFORM, MODE_GC16
                )
                : update_display_from_white(ctx->hl);
            break;
        default:
            return APP_REDRAW_NONE;
    }
    s_demo_ms[hit] = (int32_t)((esp_timer_get_time() - started_us + 500) / 1000);
    guard_draw_result(ctx->hl, err);
    ESP_LOGI(TAG, "Refresh demo %d took %ldms", hit, (long)s_demo_ms[hit]);
    return APP_REDRAW_DONE;
}

const app_desc_t app_refresh = {
    .title = "墨水屏刷新 EPD Refresh",
    .detail = "刷新方式与耗时 Refresh Modes & Timing",
    .render = demo_render,
    .present = demo_present,
    .on_touch = demo_touch,
    .on_exit = demo_on_exit,
};
