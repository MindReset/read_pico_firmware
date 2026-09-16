/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 设备功能自检。门闩真测，命令看 ACK；断电项只在后台记结果，屏上不列。
 *
 * Device self-test. Gate items are real measurements; commands watch ACK.
 * Power-cut items only record results in the backend and are not listed on screen.
 *
 * 冻结：底栏只留探测 / 清空；不等闹钟到点、不目视灯、不断电续跑。
 * KEY3 走主循环菜单把手。
 * Frozen: bar is probe / clear only; do not wait for an alarm, inspect LEDs,
 * or resume across a power cut. KEY3 is the loop menu handle.
 */

#include <stdio.h>

#include "app.h"
#include "esp_log.h"
#include "pmu_selftest.h"
#include "read_pico_pmu.h"
#include "ui_kit.h"
#include "ui_menu.h"

#define TAG "app_selftest"
#define ST_TITLE "设备功能自检 Device"
#define ST_GATE_ROWS 2
#define ST_CMD_ROWS 7

typedef struct {
    int gate_y;
    int cmd_y;
    EpdRect probe;
    EpdRect reset;
} st_geom_t;

static st_geom_t st_geom(void) {
    const int gate_y = UI_CONTENT_TOP;
    const int cmd_y = gate_y + UI_SEC_HEAD + ST_GATE_ROWS * UI_ROW_H_SM
        + UI_SECTION_GAP;
    return (st_geom_t){
        .gate_y = gate_y,
        .cmd_y = cmd_y,
        .probe = ui_bar_rect(0, 2),
        .reset = ui_bar_rect(1, 2),
    };
}

static const char* st_cell(uint8_t res) {
    if (res == PMU_ST_RES_PASS) return "通过 Pass";
    if (res == PMU_ST_RES_FAIL) return "失败 Fail";
    return "-";
}

static void draw_pairs(uint8_t* fb, int y, int begin, int end) {
    const pmu_st_view_t* v = pmu_selftest_view();
    for (int i = begin; i < end; i += 2) {
        y = ui_draw_row2(
            fb, y,
            pmu_selftest_name(i), st_cell(v->results[i]),
            i + 1 < end ? pmu_selftest_name(i + 1) : "",
            i + 1 < end ? st_cell(v->results[i + 1]) : NULL
        );
    }
}

static void draw_page(uint8_t* fb) {
    const pmu_st_view_t* v = pmu_selftest_view();
    const st_geom_t g = st_geom();
    unsigned pass = 0, fail = 0, pending = 0;
    for (int i = 0; i < PMU_ST_CMD_END; i++) {
        if (v->results[i] == PMU_ST_RES_PASS) pass++;
        else if (v->results[i] == PMU_ST_RES_FAIL) fail++;
        else if (v->results[i] == PMU_ST_RES_NONE) pending++;
    }
    char sub[80];
    ui_clear_page(fb);
    snprintf(
        sub, sizeof(sub), "通过 %u　失败 %u　待测 %u",
        pass, fail, pending
    );
    ui_draw_header(fb, ST_TITLE, v->hint[0] != '\0' ? v->hint : sub);

    ui_draw_section(fb, g.gate_y, "探活 Probe");
    draw_pairs(fb, g.gate_y + UI_SEC_HEAD, 0, PMU_ST_GATE_END);

    ui_draw_section(fb, g.cmd_y, "命令 Commands");
    draw_pairs(fb, g.cmd_y + UI_SEC_HEAD, PMU_ST_GATE_END, PMU_ST_CMD_END);
    if (v->last_msg[0] != '\0') {
        ui_text(
            fb, UI_MARGIN,
            g.cmd_y + UI_SEC_HEAD + ST_CMD_ROWS * UI_ROW_H_SM + 8,
            UI_PX_CAPTION, v->last_msg, EPD_DRAW_ALIGN_LEFT, false
        );
    }

    ui_draw_button(fb, g.probe, "探测 Probe", false);
    ui_draw_button(fb, g.reset, "清空 Clear", false);
    ui_draw_menu_handle(fb, false);
}

static void render(app_ctx_t* ctx, uint8_t* fb) {
    (void)ctx;
    draw_page(fb);
}

static void st_on_exit(app_ctx_t* ctx) {
    (void)ctx;
    read_pico_pmu_drain_events();
    app_lock_ignore_for(APP_LOCK_IGNORE_SELFTEST_MS);
}

static app_redraw_t on_touch(app_ctx_t* ctx, const cst836u_touch_t* touch) {
    (void)ctx;
    const st_geom_t g = st_geom();
    if (ui_rect_hit(g.probe, touch->x, touch->y)) {
        pmu_selftest_run_probe();
        ESP_LOGI(TAG, "probe done");
        return APP_REDRAW_PAGE;
    }
    if (ui_rect_hit(g.reset, touch->x, touch->y)) {
        pmu_selftest_reset();
        return APP_REDRAW_PAGE;
    }
    return APP_REDRAW_NONE;
}

const app_desc_t app_selftest = {
    .title = ST_TITLE,
    .detail = "探活与命令 Probe & Commands",
    .holds_pmu = true,
    .render = render,
    .on_exit = st_on_exit,
    .on_touch = on_touch,
};
