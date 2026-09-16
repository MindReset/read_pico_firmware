/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 多功能电源按键。看 key_raw_events、STATUS 电平，以及 DOWN/UP/SHORT/LONG。
 *
 * Power key. Watches key_raw_events, STATUS level, and DOWN/UP/SHORT/LONG.
 *
 * 冻结：本页独占 PMU 事件，短按不锁屏；进页整页 GL16，之后只刷变化块；
 * 页眉圆角块跟按下/抬起；旧事件变灰；底栏只切 raw / 清空。
 * KEY3 走主循环菜单把手。
 * Frozen: this page owns PMU events; a short press does not lock; enter with
 * full-page GL16, then only changed rectangles; header pill follows down/up;
 * older events fade; bar only toggles raw / clear. KEY3 is the loop menu handle.
 */

#include <stdio.h>
#include <string.h>

#include "app.h"
#include "display.h"
#include "e0470_epaper_waveform.h"
#include "esp_log.h"
#include "read_pico_pmu.h"
#include "read_pico_pmu_protocol.h"
#include "ttf_font.h"
#include "ui_kit.h"
#include "ui_menu.h"

#define TAG "app_key"
#define KEY_TITLE "电源按键 Power Key"
#define KEY_POLL_MS 50
#define KEY_LOG_FLUSH_MS 200
#define KEY_DOT 24
#define KEY_DOT_R 6
#define KEY_LOG_N 12

typedef struct {
    uint8_t type;
    uint32_t arg0;
} key_evt_t;

typedef struct {
    int evt_y;
    int rows;
    EpdRect raw;
    EpdRect clear;
    EpdRect dot;
    EpdRect rows_r;
    EpdRect sub_r;
    EpdRect bar_r;
} key_geom_t;

static bool s_down;
static bool s_raw;
static bool s_pmu_ok;
static key_evt_t s_log[KEY_LOG_N];
static int s_log_n;
static bool s_log_dirty;
static int64_t s_log_flush_ms;
static int64_t s_last_poll_ms;
static EpdRect s_areas[2];
static int s_area_n;

static void fmt_sub(char* buf, size_t n) {
    if (!s_pmu_ok) {
        snprintf(buf, n, "未连上 Offline");
        return;
    }
    snprintf(
        buf, n, "raw %s　%s",
        s_raw ? "开 On" : "关 Off",
        s_down ? "按下 Down" : "抬起 Up"
    );
}

static EpdRect pad_area(EpdRect r) {
    int x = r.x > 4 ? r.x - 4 : 0;
    int y = r.y > 4 ? r.y - 4 : 0;
    return (EpdRect){
        .x = x, .y = y, .width = r.width + 8, .height = r.height + 8,
    };
}

static void set_area(EpdRect r) {
    s_areas[0] = pad_area(r);
    s_area_n = 1;
}

static void add_area(EpdRect r) {
    if (s_area_n >= 2) return;
    s_areas[s_area_n++] = pad_area(r);
}

static key_geom_t key_geom(void) {
    char sub[48];
    fmt_sub(sub, sizeof(sub));
    const ui_header_skel_t head = ui_header_skel(KEY_TITLE, sub, KEY_DOT);
    const int evt_y = UI_CONTENT_TOP;
    int rows = (UI_CONTENT_BOTTOM - evt_y - UI_SEC_HEAD) / UI_ROW_H;
    if (rows < 1) rows = 1;
    if (rows > KEY_LOG_N) rows = KEY_LOG_N;
    const EpdRect raw = ui_bar_rect(0, 2);
    const EpdRect clear = ui_bar_rect(1, 2);
    return (key_geom_t){
        .evt_y = evt_y,
        .rows = rows,
        .raw = raw,
        .clear = clear,
        .dot = head.accessory,
        .rows_r = {
            .x = UI_MARGIN,
            .y = evt_y + UI_SEC_HEAD,
            .width = ui_content_width(),
            .height = rows * UI_ROW_H,
        },
        .sub_r = {
            .x = UI_MARGIN,
            .y = UI_HEADER_SUB_Y,
            .width = ui_content_width(),
            .height = UI_HEADER_RULE_Y - UI_HEADER_SUB_Y,
        },
        .bar_r = {
            .x = raw.x,
            .y = raw.y,
            .width = (clear.x + clear.width) - raw.x,
            .height = raw.height,
        },
    };
}

static uint8_t age_ink(int i) {
    if (i <= 0) return UI_INK_BLACK;
    int ink = i * 3;
    return (uint8_t)(ink > 12 ? 12 : ink);
}

static const char* evt_name(uint8_t type) {
    switch (type) {
        case PMU_EVT_KEY_DOWN: return "按下 Down";
        case PMU_EVT_KEY_UP: return "抬起 Up";
        case PMU_EVT_KEY_SHORT: return "短按 Short";
        case PMU_EVT_KEY_LONG: return "长按 Long";
        case PMU_EVT_KEY_FORCE_OFF: return "强制关机 Force";
        default: return "事件 Event";
    }
}

static void draw_evt_row(
    uint8_t* fb, int y, const char* label, const char* value, uint8_t ink
) {
    const int center = y + UI_ROW_H / 2;
    int above = 0;
    int below = 0;
    if (label != NULL && label[0] != '\0') {
        ttf_measure_line_px(UI_PX_LABEL, label, &above, &below);
        ttf_draw_text_px(
            fb, UI_MARGIN, center + (above - below) / 2, UI_PX_LABEL, label,
            EPD_DRAW_ALIGN_LEFT, ink, UI_INK_WHITE
        );
    }
    if (value != NULL && value[0] != '\0') {
        ttf_measure_line_px(UI_PX_VALUE, value, &above, &below);
        ttf_draw_text_px(
            fb, ui_content_right(), center + (above - below) / 2, UI_PX_VALUE,
            value, EPD_DRAW_ALIGN_RIGHT, ink, UI_INK_WHITE
        );
    }
    ui_hairline(fb, y + UI_ROW_H - 1, UI_MARGIN, ui_content_width(), UI_GRAY_LIGHT);
}

static void draw_events(uint8_t* fb, const key_geom_t* g) {
    ui_clear_rect_fast(fb, g->rows_r);
    char val[32];
    int y = g->rows_r.y;
    for (int i = 0; i < g->rows; i++) {
        if (i < s_log_n) {
            snprintf(val, sizeof(val), "%u ms", (unsigned)s_log[i].arg0);
            draw_evt_row(fb, y, evt_name(s_log[i].type), val, age_ink(i));
        } else if (i == 0) {
            draw_evt_row(fb, y, "记录 Log", "无 None", age_ink(2));
        } else {
            draw_evt_row(fb, y, "", NULL, UI_INK_BLACK);
        }
        y += UI_ROW_H;
    }
}

static void draw_dot(uint8_t* fb, EpdRect r, bool down) {
    if (fb == NULL || r.width < 8) return;
    ui_clear_rect_fast(fb, r);
    ui_fill_round_rect(fb, r, KEY_DOT_R, UI_GRAY_BLACK);
    if (down) return;
    int inner_r = KEY_DOT_R - 2;
    if (inner_r < 2) inner_r = 2;
    ui_fill_round_rect(fb, ui_inset_rect(r, 2), inner_r, UI_GRAY_WHITE);
}

static void draw_page(uint8_t* fb) {
    const key_geom_t g = key_geom();
    char sub[48];
    fmt_sub(sub, sizeof(sub));
    ui_clear_page(fb);
    ui_header_skel_t head = ui_header_skel(KEY_TITLE, sub, KEY_DOT);
    ui_draw_header_skel(fb, &head, KEY_TITLE, sub);
    draw_dot(fb, g.dot, s_down);
    ui_draw_section(fb, g.evt_y, "事件 Events");
    draw_events(fb, &g);
    ui_draw_button(fb, g.raw, "原始 Raw", s_raw);
    ui_draw_button(fb, g.clear, "清空 Clear", false);
    ui_draw_menu_handle(fb, false);
}

static void push_evt(uint8_t type, uint32_t arg0) {
    if (s_log_n > 0) {
        int n = s_log_n < KEY_LOG_N ? s_log_n : KEY_LOG_N - 1;
        memmove(&s_log[1], &s_log[0], (size_t)n * sizeof(s_log[0]));
    }
    s_log[0] = (key_evt_t){ .type = type, .arg0 = arg0 };
    if (s_log_n < KEY_LOG_N) s_log_n++;
}

static bool key_is_down(const pmu_snapshot_t* s) {
    return (s->key_state & 0x01) != 0
        || (s->flags & PMU_STATUS_KEY_PRESSED) != 0;
}

static bool ingest(void) {
    if (!read_pico_pmu_ready()) {
        s_pmu_ok = false;
        s_down = false;
        return false;
    }
    s_pmu_ok = true;
    bool ev = false;
    for (int i = 0; i < PMU_EVENT_FIFO_DEPTH; i++) {
        if (read_pico_pmu_poll() != ESP_OK) break;
        const pmu_snapshot_t* s = read_pico_pmu_get();
        s_down = key_is_down(s);
        if (!s->event_ok || s->pending_events == 0) break;
        const uint8_t type = s->event.type;
        const uint16_t id = s->event.event_id;
        const uint32_t arg0 = s->event.arg0;
        if (id == 0) break;
        if (type >= PMU_EVT_KEY_DOWN && type <= PMU_EVT_KEY_FORCE_OFF) {
            push_evt(type, arg0);
            ev = true;
        }
        if (read_pico_pmu_event_ack(id) != ESP_OK) break;
    }
    return ev;
}

static void render(app_ctx_t* ctx, uint8_t* fb) {
    (void)ctx;
    draw_page(fb);
}

static void key_on_enter(app_ctx_t* ctx) {
    s_log_n = 0;
    s_log_dirty = false;
    s_log_flush_ms = 0;
    s_area_n = 0;
    s_last_poll_ms = ctx->now_ms;
    s_pmu_ok = read_pico_pmu_ready();
    s_down = false;
    s_raw = false;
    if (!s_pmu_ok) return;
    read_pico_pmu_refresh();
    read_pico_pmu_drain_events();
    const pmu_snapshot_t* s = read_pico_pmu_get();
    s_raw = s->config.key_raw_events != 0;
    s_down = key_is_down(s);
}

static void key_on_exit(app_ctx_t* ctx) {
    (void)ctx;
    if (read_pico_pmu_ready()) read_pico_pmu_drain_events();
    app_lock_ignore_for(APP_LOCK_IGNORE_SELFTEST_MS);
}

static EpdRect area_hint(app_ctx_t* ctx) {
    (void)ctx;
    if (s_area_n > 0) return s_areas[0];
    return pad_area(key_geom().dot);
}

static app_redraw_t on_touch(app_ctx_t* ctx, const cst836u_touch_t* touch) {
    const key_geom_t g = key_geom();
    if (ui_rect_hit(g.raw, touch->x, touch->y) && s_pmu_ok) {
        uint8_t buf[1] = { s_raw ? 0 : 1 };
        read_pico_pmu_cmd(PMU_CMD_CONFIG_SET_KEY_EVENTS, buf, 1);
        read_pico_pmu_cmd(PMU_CMD_CONFIG_GET, NULL, 0);
        s_raw = read_pico_pmu_get()->config.key_raw_events != 0;
        draw_page(ctx->fb);
        set_area(g.sub_r);
        add_area(g.bar_r);
        return APP_REDRAW_AREA;
    }
    if (ui_rect_hit(g.clear, touch->x, touch->y)) {
        s_log_n = 0;
        s_log_dirty = false;
        draw_events(ctx->fb, &g);
        set_area(g.rows_r);
        return APP_REDRAW_AREA;
    }
    return APP_REDRAW_NONE;
}

static app_redraw_t on_tick(app_ctx_t* ctx) {
    if (ctx->now_ms - s_last_poll_ms < KEY_POLL_MS) return APP_REDRAW_NONE;
    s_last_poll_ms = ctx->now_ms;
    const bool was_down = s_down;
    if (ingest()) {
        s_log_dirty = true;
        s_log_flush_ms = ctx->now_ms + KEY_LOG_FLUSH_MS;
    }
    const key_geom_t g = key_geom();
    if (s_log_dirty && ctx->now_ms >= s_log_flush_ms) {
        s_log_dirty = false;
        draw_events(ctx->fb, &g);
        set_area(g.rows_r);
        if (s_down != was_down) {
            draw_dot(ctx->fb, g.dot, s_down);
            add_area(g.dot);
        }
        return APP_REDRAW_AREA;
    }
    if (s_down == was_down) return APP_REDRAW_NONE;
    draw_dot(ctx->fb, g.dot, s_down);
    set_area(g.dot);
    return APP_REDRAW_AREA;
}

static bool key_present(app_ctx_t* ctx, app_redraw_t redraw) {
    if (redraw == APP_REDRAW_NONE || redraw == APP_REDRAW_DONE) return false;
    if (redraw != APP_REDRAW_AREA) {
        draw_page(ctx->fb);
        guard_draw_result(
            ctx->hl, update_display_with(ctx->hl, &E0470_WAVEFORM, MODE_GL16)
        );
        return true;
    }
    if (s_area_n < 1) set_area(key_geom().dot);
    for (int i = 0; i < s_area_n; i++) {
        guard_draw_result(
            ctx->hl,
            update_display_area_with(ctx->hl, &E0470_WAVEFORM, MODE_GL16, s_areas[i])
        );
    }
    return true;
}

const app_desc_t app_key = {
    .title = KEY_TITLE,
    .detail = "原始事件与电平 key_raw_events",
    .holds_pmu = true,
    .enter_full = true,
    .render = render,
    .present = key_present,
    .on_enter = key_on_enter,
    .on_exit = key_on_exit,
    .on_touch = on_touch,
    .on_tick = on_tick,
    .area_hint = area_hint,
};
