/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * CST836U 触摸。手指在缩略图里拖动时用连续 DU 逐相位压黑圆点，抬手再整页
 * GC16 定稿。芯片原始寄存器不上屏，只进日志。
 *
 * CST836U touch. Dragging in the thumbnail uses continuous DU to darken dots
 * one phase at a time; lift settles the page with GC16. Raw chip registers
 * stay off-screen and go to the log only.
 */

#include <stdio.h>
#include <string.h>

#include "app.h"
#include "continuous_du.h"
#include "display.h"
#include "e0470_epaper_waveform.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ui_kit.h"
#include "ui_menu.h"

#define TOUCH_TITLE "触摸 Touch"
#define TOUCH_SLEEP_BTN "触摸屏睡眠 Touchscreen Sleep"
#define TOUCH_UPDATE_INTERVAL_MS 15
#define TAP_SLEEP_MS 10000
#define TAP_SLEEP_S (TAP_SLEEP_MS / 1000)
#define TOUCH_DOT_R 7
#define TOUCH_DOT_INSET 8
#define TOUCH_MAP_W 220
#define TOUCH_MAP_TOP (UI_CONTENT_TOP + UI_SEC_HEAD + 4 * UI_ROW_H \
                       + UI_SECTION_GAP + UI_SEC_HEAD)
#define TOUCH_KEY_H 34
#define TOUCH_KEY_GAP 10
#define TOUCH_FOLLOW_PAD_X 24
#define TOUCH_FOLLOW_PAD_Y 28

static const char* TAG = "app_touch";

static bool s_asleep;
static int64_t s_sleep_started_ms;
static cst836u_touch_t s_drawn;
static int64_t s_last_update_ms;
static bool s_continuous;
// 进页那一下点菜单的坐标会留在触摸状态里，第一次按下之前不当成本页落点。/ Menu-tap coords from entering the page stay in touch state; ignore them until the first press on this page.
static bool s_stale_touch;
static bool s_key_down[UI_KEY_COUNT];
static int s_dot_x[CST836U_MAX_POINTS];
static int s_dot_y[CST836U_MAX_POINTS];
static int s_dot_count;
static int s_scans;
static int64_t s_scan_us;

typedef struct {
    EpdRect map;
    EpdRect follow;
    EpdRect refresh;
    EpdRect key[UI_KEY_COUNT];
    int disp_w;
    int disp_h;
} touch_geom_t;

static touch_geom_t touch_geom(void) {
    const int disp_w = epd_rotated_display_width();
    const int disp_h = epd_rotated_display_height();
    const EpdRect map = {
        .x = (disp_w - TOUCH_MAP_W) / 2,
        .y = TOUCH_MAP_TOP,
        .width = TOUCH_MAP_W,
        .height = TOUCH_MAP_W * disp_h / disp_w,
    };
    const int key_w = (map.width - 2 * TOUCH_KEY_GAP) / UI_KEY_COUNT;
    int fx = map.x - TOUCH_FOLLOW_PAD_X;
    int fw = map.width + TOUCH_FOLLOW_PAD_X * 2;
    if (fx < 0) fx = 0;
    if (fx + fw > disp_w) fw = disp_w - fx;
    const int y0 = UI_HEADER_RULE_Y + 4;
    touch_geom_t g = {
        .map = map,
        .follow = {
            .x = fx,
            .y = map.y - TOUCH_FOLLOW_PAD_Y,
            .width = fw,
            .height = map.height + TOUCH_FOLLOW_PAD_Y * 2 + TOUCH_KEY_GAP + TOUCH_KEY_H,
        },
        .refresh = {
            .x = UI_MARGIN - 5,
            .y = y0,
            .width = ui_content_width() + 10,
            .height = UI_CONTENT_BOTTOM - y0,
        },
        .disp_w = disp_w,
        .disp_h = disp_h,
    };
    for (int i = 0; i < UI_KEY_COUNT; i++) {
        g.key[i] = (EpdRect){
            .x = map.x + i * (key_w + TOUCH_KEY_GAP),
            .y = map.y + map.height + TOUCH_KEY_GAP,
            .width = key_w,
            .height = TOUCH_KEY_H,
        };
    }
    return g;
}

static bool touch_point_is_key(const cst836u_point_t* pt) {
    return pt->y >= UI_KEY_AREA_TOP;
}

static bool touch_point_visible(const cst836u_point_t* pt) {
    if (s_stale_touch) return false;
    if (touch_point_is_key(pt)) return false;
    // 空槽常是 (0,0) 还带着按下；那是原点，不是落点。/ Empty slots are often (0,0) still marked pressed; that is the origin, not a hit.
    return pt->x != 0 || pt->y != 0;
}

static const char* touch_event_name(uint8_t event) {
    if (event == 0) return "按下 Press";
    if (event == 1) return "抬起 Release";
    if (event == 2) return "移动 Move";
    return "---";
}

static void remember_touch(const cst836u_touch_t* touch, int64_t now_ms) {
    s_drawn = *touch;
    s_last_update_ms = now_ms;
}

static int clamp_i(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void map_touch_xy(
    const touch_geom_t* g, const cst836u_point_t* pt, int* px, int* py
) {
    const EpdRect m = g->map;
    int x = m.x + (int)pt->x * (m.width - 1) / (g->disp_w - 1);
    int y = m.y + (int)pt->y * (m.height - 1) / (g->disp_h - 1);
    *px = clamp_i(x, m.x + TOUCH_DOT_INSET, m.x + m.width - TOUCH_DOT_INSET - 1);
    *py = clamp_i(y, m.y + TOUCH_DOT_INSET, m.y + m.height - TOUCH_DOT_INSET - 1);
}

static void touch_keys_down(const cst836u_touch_t* touch, bool* down) {
    for (int k = 0; k < UI_KEY_COUNT; k++) down[k] = false;
    for (int i = 0; i < CST836U_MAX_POINTS; i++) {
        const cst836u_point_t* pt = &touch->points[i];
        if (!pt->active || !touch_point_is_key(pt)) continue;
        const int k = ui_key_hit_test(pt->x, pt->y);
        if (k >= 0) down[k] = true;
    }
}

static void draw_touch_keys(
    uint8_t* framebuffer, const touch_geom_t* g, const bool* down
) {
    static const char* const labels[UI_KEY_COUNT] = { "1", "2", "3" };
    for (int k = 0; k < UI_KEY_COUNT; k++) {
        const EpdRect r = g->key[k];
        if (down[k]) {
            epd_fill_rect(r, UI_GRAY_BLACK, framebuffer);
        } else {
            epd_draw_rect(r, UI_GRAY_BLACK, framebuffer);
        }
        ui_text_vc(
            framebuffer, r.x + r.width / 2, r.y + r.height / 2,
            UI_PX_CAPTION, labels[k], EPD_DRAW_ALIGN_CENTER, down[k]
        );
    }
}

static void draw_touch_map(uint8_t* framebuffer, const cst836u_touch_t* touch) {
    const touch_geom_t g = touch_geom();
    int px[CST836U_MAX_POINTS];
    int py[CST836U_MAX_POINTS];
    bool visible[CST836U_MAX_POINTS];

    epd_draw_rect(g.map, UI_GRAY_BLACK, framebuffer);

    for (int i = 0; i < CST836U_MAX_POINTS; i++) {
        visible[i] = touch_point_visible(&touch->points[i]);
        if (!visible[i]) continue;
        map_touch_xy(&g, &touch->points[i], &px[i], &py[i]);
    }
    if (visible[0] && visible[1]
        && touch->points[0].active && touch->points[1].active) {
        epd_draw_line(px[0], py[0], px[1], py[1], UI_GRAY_BLACK, framebuffer);
    }

    static const char* const labels[CST836U_MAX_POINTS] = { "1", "2" };
    for (int i = 0; i < CST836U_MAX_POINTS; i++) {
        if (!visible[i]) continue;
        if (touch->points[i].active) {
            epd_fill_circle(px[i], py[i], TOUCH_DOT_R, UI_GRAY_BLACK, framebuffer);
        } else {
            epd_draw_circle(px[i], py[i], TOUCH_DOT_R, UI_GRAY_BLACK, framebuffer);
        }
        ui_text(
            framebuffer, px[i] + 14, py[i] - 30, UI_PX_CAPTION, labels[i],
            EPD_DRAW_ALIGN_LEFT, false
        );
    }

    bool down[UI_KEY_COUNT];
    touch_keys_down(touch, down);
    draw_touch_keys(framebuffer, &g, down);
}

static int touch_map_dots(
    const cst836u_touch_t* touch, int* xs, int* ys, bool active_only
) {
    const touch_geom_t g = touch_geom();
    int count = 0;
    for (int i = 0; i < CST836U_MAX_POINTS; i++) {
        const cst836u_point_t* pt = &touch->points[i];
        if (!touch_point_visible(pt)) continue;
        if (active_only && !pt->active) continue;
        map_touch_xy(&g, pt, &xs[count], &ys[count]);
        count++;
    }
    return count;
}

static void draw_touch_follow(uint8_t* framebuffer, const cst836u_touch_t* touch) {
    ui_clear_rect_fast(framebuffer, touch_geom().follow);
    draw_touch_map(framebuffer, touch);
}

static int draw_touch_point_row(
    uint8_t* framebuffer, int y, int index, const cst836u_point_t* pt
) {
    char label[8];
    char value[48];
    snprintf(label, sizeof(label), "P%d", index + 1);
    if (!touch_point_visible(pt)) {
        return ui_draw_row(framebuffer, y, label, "—");
    }
    snprintf(
        value, sizeof(value), "ID%u  %s  %u, %u",
        pt->id, touch_event_name(pt->event), pt->x, pt->y
    );
    return ui_draw_row(framebuffer, y, label, value);
}

static void draw_chrome(uint8_t* framebuffer) {
    ui_draw_button(framebuffer, ui_bar_rect(0, 1), TOUCH_SLEEP_BTN, s_asleep);
    ui_draw_menu_handle(framebuffer, false);
}

// 进页残留不当成触点：状态抬起、点位清空，缩略图也不画圆。/ Stale enter-page coords are not a hit: force released, clear points, and draw no dots on the thumbnail.
static void touch_for_draw(const cst836u_touch_t* src, cst836u_touch_t* dst) {
    if (src == NULL) {
        memset(dst, 0, sizeof(*dst));
        return;
    }
    *dst = *src;
    if (!s_stale_touch) return;
    dst->touched = false;
    dst->count = 0;
    for (int i = 0; i < CST836U_MAX_POINTS; i++) {
        memset(&dst->points[i], 0, sizeof(dst->points[i]));
    }
}

static void draw_touch_page(
    uint8_t* framebuffer, const cst836u_touch_t* touch, const cst836u_info_t* info
) {
    cst836u_touch_t shown;
    touch_for_draw(touch, &shown);
    touch = &shown;

    char line[64];
    ui_clear_page(framebuffer);
    if (info != NULL) {
        snprintf(
            line, sizeof(line),
            "控制器 Controller CST836U　固件 Firmware %02X", info->fw
        );
    } else {
        snprintf(line, sizeof(line), "控制器 Controller CST836U");
    }
    int y = ui_draw_header(framebuffer, TOUCH_TITLE, line);

    if (s_asleep) {
        y = ui_draw_section(framebuffer, y, "深度睡眠 Deep Sleep");
        ui_text(
            framebuffer, UI_MARGIN, y, UI_PX_BODY,
            "芯片已停止响应总线 Chip stopped responding",
            EPD_DRAW_ALIGN_LEFT, false
        );
        y += UI_ROW_H;
        snprintf(
            line, sizeof(line),
            "%d 秒后自动复位 Auto-reset after %d s", TAP_SLEEP_S, TAP_SLEEP_S
        );
        ui_text(
            framebuffer, UI_MARGIN, y, UI_PX_BODY, line,
            EPD_DRAW_ALIGN_LEFT, false
        );
        draw_chrome(framebuffer);
        return;
    }

    y = ui_draw_section(framebuffer, y, "触点 Touch Points");
    y = ui_draw_row(
        framebuffer, y, "状态 Status",
        touch->touched ? "按下 Pressed" : "抬起 Released"
    );
    snprintf(line, sizeof(line), "%u", touch->count);
    y = ui_draw_row(framebuffer, y, "点数 Count", line);
    y = draw_touch_point_row(framebuffer, y, 0, &touch->points[0]);
    draw_touch_point_row(framebuffer, y, 1, &touch->points[1]);

    ui_draw_section(framebuffer, TOUCH_MAP_TOP - UI_SEC_HEAD, "位置 Position");
    draw_touch_map(framebuffer, touch);
    draw_chrome(framebuffer);
}

static void du_mark_dot(int lx, int ly, int r, int phases) {
    int cx, cy;
    continuous_du_from_logical(lx, ly, &cx, &cy);
    continuous_du_mark_circle(cx, cy, r, phases);
}

static void on_enter(app_ctx_t* ctx) {
    s_continuous = false;
    s_stale_touch = true;
    memset(s_key_down, 0, sizeof(s_key_down));
    s_dot_count = 0;
    remember_touch(ctx->touch, ctx->now_ms);
}

static void on_leave(app_ctx_t* ctx) {
    (void)ctx;
    if (s_continuous) {
        continuous_du_reset();
        s_continuous = false;
    }
    s_dot_count = 0;
}

static void render(app_ctx_t* ctx, uint8_t* fb) {
    draw_touch_page(fb, ctx->touch, ctx->touch_info);
    // 整页重画已经把按着的键框画成实心，连续 DU 不用再压一次。/ Full-page paint already fills held key boxes; continuous DU need not darken them again.
    touch_keys_down(ctx->touch, s_key_down);
    remember_touch(ctx->touch, ctx->now_ms);
}

static app_redraw_t on_touch(app_ctx_t* ctx, const cst836u_touch_t* touch) {
    if (ui_bar_hit(touch->x, touch->y, 1) != 0) return APP_REDRAW_NONE;
    if (cst836u_set_mode(ctx->tp, CST836U_MODE_DEEPSLEEP) == ESP_OK) {
        s_asleep = true;
        s_sleep_started_ms = ctx->now_ms;
    }
    return APP_REDRAW_PAGE;
}

static app_redraw_t tick_sleep(app_ctx_t* ctx) {
    if (ctx->now_ms - s_sleep_started_ms < TAP_SLEEP_MS) return APP_REDRAW_NONE;
    esp_err_t err = cst836u_wake(ctx->tp);
    s_asleep = false;
    ESP_LOGI(TAG, "Touch woken by reset: %s", esp_err_to_name(err));
    return APP_REDRAW_PAGE;
}

static app_redraw_t tick_follow_continuous(app_ctx_t* ctx) {
    const cst836u_touch_t* touch = ctx->touch;
    const int light = -continuous_du_light_phases();
    const int dark = continuous_du_dark_phases();
    if (!s_continuous) {
        // 上一次定稿的空心圆和编号连续 DU 不认识，进来先擦白，和新圆点压黑并行。/ Hollow circles and labels from the last settle are unknown to continuous DU; erase them to white in parallel with darkening the new dots.
        for (int i = 0; i < s_dot_count; i++) {
            du_mark_dot(s_dot_x[i], s_dot_y[i], TOUCH_DOT_R + 1, light);
            continuous_du_mark_rect(
                continuous_du_rect_from_logical((EpdRect){
                    .x = s_dot_x[i] + 8,
                    .y = s_dot_y[i] - 36,
                    .width = 40,
                    .height = UI_PX_CAPTION + 18,
                }),
                light
            );
        }
        s_dot_count = 0;
        s_continuous = true;
    }

    s_stale_touch = false;

    const touch_geom_t g = touch_geom();
    bool down[UI_KEY_COUNT];
    touch_keys_down(touch, down);
    for (int k = 0; k < UI_KEY_COUNT; k++) {
        if (down[k] == s_key_down[k]) continue;
        continuous_du_mark_rect(
            continuous_du_rect_from_logical(g.key[k]),
            down[k] ? dark : light
        );
        s_key_down[k] = down[k];
    }

    int xs[CST836U_MAX_POINTS];
    int ys[CST836U_MAX_POINTS];
    int count = touch_map_dots(touch, xs, ys, true);
    bool dots_moved = count != s_dot_count;
    for (int i = 0; i < count && !dots_moved; i++) {
        if (xs[i] != s_dot_x[i] || ys[i] != s_dot_y[i]) dots_moved = true;
    }
    if (dots_moved) {
        for (int i = 0; i < s_dot_count; i++) {
            du_mark_dot(s_dot_x[i], s_dot_y[i], TOUCH_DOT_R, light);
        }
        for (int i = 0; i < count; i++) {
            du_mark_dot(xs[i], ys[i], TOUCH_DOT_R, dark);
            s_dot_x[i] = xs[i];
            s_dot_y[i] = ys[i];
        }
        s_dot_count = count;
    }

    if (continuous_du_busy()) {
        rails_keepalive();
        epd_poweron();
        const int64_t t0 = esp_timer_get_time();
        enum EpdDrawError result = continuous_du_scan(
            ctx->hl, continuous_du_rect_from_logical(g.follow)
        );
        s_scan_us += esp_timer_get_time() - t0;
        s_scans++;
        guard_draw_result(ctx->hl, result);
    }
    remember_touch(touch, ctx->now_ms);
    return APP_REDRAW_DONE;
}

// 抬手定稿：先把缩略图这块参考帧对齐成真实状态，diff 才会把框和分隔线重画回来。/ On lift settle: first align this thumbnail's reference frame to the real state so the diff redraws the box and divider.
static app_redraw_t tick_settle_continuous(app_ctx_t* ctx) {
    const touch_geom_t g = touch_geom();
    epd_fill_rect(g.follow, 0xFF, ctx->hl->back_fb);
    for (int i = 0; i < s_dot_count; i++) {
        epd_fill_circle(
            s_dot_x[i], s_dot_y[i], TOUCH_DOT_R, 0x00, ctx->hl->back_fb
        );
    }
    for (int k = 0; k < UI_KEY_COUNT; k++) {
        if (s_key_down[k]) epd_fill_rect(g.key[k], 0x00, ctx->hl->back_fb);
        s_key_down[k] = false;
    }
    continuous_du_reset();
    s_continuous = false;

    draw_touch_page(ctx->fb, ctx->touch, ctx->touch_info);
    remember_touch(ctx->touch, ctx->now_ms);
    s_dot_count = touch_map_dots(&s_drawn, s_dot_x, s_dot_y, false);
    guard_draw_result(
        ctx->hl,
        update_display_area_with(
            ctx->hl, &E0470_WAVEFORM, APP_SETTLE_REFRESH_MODE, g.refresh
        )
    );
    ESP_LOGI(
        TAG, "Tap settle: %d scans, %lldms scan total, %.1fms/scan %s",
        s_scans, s_scan_us / 1000,
        s_scans > 0 ? (double)s_scan_us / s_scans / 1000.0 : 0.0,
        APP_SETTLE_REFRESH_MODE == MODE_DU ? "DU" : "GC16"
    );
    s_scans = 0;
    s_scan_us = 0;
    return APP_REDRAW_DONE;
}

static app_redraw_t tick_follow_plain(app_ctx_t* ctx) {
    const cst836u_touch_t* touch = ctx->touch;
    bool moved = touch->count != s_drawn.count || touch->touched != s_drawn.touched;
    for (int i = 0; i < CST836U_MAX_POINTS; i++) {
        const cst836u_point_t* a = &touch->points[i];
        const cst836u_point_t* b = &s_drawn.points[i];
        if (a->active != b->active || a->event != b->event || a->id != b->id
            || a->x != b->x || a->y != b->y) {
            moved = true;
        }
    }
    if (!moved) return APP_REDRAW_NONE;
    const bool due = ctx->now_ms - s_last_update_ms >= TOUCH_UPDATE_INTERVAL_MS;
    if (!(ctx->pressed || ctx->released || touch->count != s_drawn.count
          || (touch->touched && due))) {
        return APP_REDRAW_NONE;
    }

    const bool follow = touch->touched;
    if (follow) s_stale_touch = false;
    const touch_geom_t g = touch_geom();
    const int64_t ui0 = esp_timer_get_time();
    if (follow) {
        draw_touch_follow(ctx->fb, touch);
    } else {
        draw_touch_page(ctx->fb, touch, ctx->touch_info);
    }
    const int32_t ui_ms = (int32_t)((esp_timer_get_time() - ui0) / 1000);
    remember_touch(touch, ctx->now_ms);

    const int64_t upd0 = esp_timer_get_time();
    enum EpdDrawError result = update_display_area_with(
        ctx->hl, follow ? &E0470_FOLLOW_WAVEFORM : &E0470_WAVEFORM,
        follow ? APP_DYNAMIC_REFRESH_MODE : APP_SETTLE_REFRESH_MODE,
        follow ? g.follow : g.refresh
    );
    const int32_t upd_ms = (int32_t)((esp_timer_get_time() - upd0) / 1000);
    guard_draw_result(ctx->hl, result);
    ESP_LOGI(
        TAG, "Tap %s %u,%u ui %dms upd %dms %s",
        touch->touched ? "DN" : "UP", touch->points[0].x, touch->points[0].y,
        ui_ms, upd_ms, follow ? "follow"
                              : (APP_SETTLE_REFRESH_MODE == MODE_DU ? "settle DU" : "settle GC16")
    );
    return APP_REDRAW_DONE;
}

static app_redraw_t on_tick(app_ctx_t* ctx) {
    if (s_asleep) return tick_sleep(ctx);
    if (s_stale_touch) {
        // 进页那次点菜单不算。等这页上一次新的、未被别处吃掉的按下。/ The enter-page menu tap does not count. Wait for a new press on this page that nothing else consumed.
        if (!ctx->pressed || ctx->consumed) return APP_REDRAW_NONE;
        s_stale_touch = false;
    }
    if (ctx->consumed) return APP_REDRAW_NONE;
    if (ctx->continuous_ready) {
        if (ctx->touch->touched) return tick_follow_continuous(ctx);
        if (s_continuous) return tick_settle_continuous(ctx);
        return APP_REDRAW_NONE;
    }
    return tick_follow_plain(ctx);
}

const app_desc_t app_touch = {
    .title = TOUCH_TITLE,
    .detail = "触点跟踪与唤醒 Touch Tracking & Wake",
    .render = render,
    .on_enter = on_enter,
    .on_exit = on_leave,
    .on_touch = on_touch,
    .on_tick = on_tick,
};
