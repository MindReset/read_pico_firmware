/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 扩展口。Port-0 八根网线的电平和只读 CFG/INV，后台自动读，没变化不刷。
 *
 * IO expander. Port-0 levels and read-only CFG/INV; polled in the background
 * and not refreshed when nothing changed.
 *
 * 冻结：只读 CFG/INV，不改方向/极性；不拨 SY_EN / VCOM_EN / MODE / XOE；
 * 底栏只脉冲 TP_RST，点一下画低再画高；PGOOD 语义在电源页，这里只看电平。
 * KEY3 走主循环菜单把手。
 * Frozen: CFG/INV are read-only; do not change direction/polarity; do not
 * toggle SY_EN / VCOM_EN / MODE / XOE; bar only pulses TP_RST (draw low then
 * high); PGOOD meaning lives on the power page—here it is a level only.
 * KEY3 is the loop menu handle.
 */

#include <stdio.h>

#include "app.h"
#include "display.h"
#include "esp_log.h"
#include "read_pico_board.h"
#include "ui_kit.h"
#include "ui_menu.h"

#define TAG "app_ioe"
#define IOE_TITLE "扩展口 IOE"
#define IOE_POLL_MS 50
#define IOE_IDLE_MS 400
#define IOE_CFG0_EXPECT 0x64
#define IOE_IN_ROWS 2
#define IOE_OUT_ROWS 3
#define IOE_BIT_MODE 0
#define IOE_BIT_XOE 1
#define IOE_BIT_CW_INT 2
#define IOE_BIT_SY_EN 3
#define IOE_BIT_VCOM_EN 4
#define IOE_BIT_PGOOD 5
#define IOE_BIT_SD_CD 6
#define IOE_BIT_TP_RST 7

typedef struct {
    int in_y;
    int out_y;
    int cfg_y;
    EpdRect tp_rst;
} ioe_geom_t;

static read_pico_status_t s_drawn;
static int64_t s_last_poll_ms;

static ioe_geom_t ioe_geom(void) {
    const int in_y = UI_CONTENT_TOP;
    const int out_y = in_y + UI_SEC_HEAD + IOE_IN_ROWS * UI_ROW_H_SM + UI_SECTION_GAP;
    const int cfg_y = out_y + UI_SEC_HEAD + IOE_OUT_ROWS * UI_ROW_H_SM + UI_SECTION_GAP;
    return (ioe_geom_t){
        .in_y = in_y,
        .out_y = out_y,
        .cfg_y = cfg_y,
        .tp_rst = ui_bar_rect(0, 1),
    };
}

static bool pin_high(uint8_t port, int bit) {
    return (port & (1U << bit)) != 0;
}

static const char* ioe_level(bool high) {
    return high ? "高 High" : "低 Low";
}

static const char* ioe_sd(bool high) {
    return high ? "未插入 Missing" : "已插入 Inserted";
}

static void ioe_dir_text(char* buf, size_t n, uint8_t cfg) {
    if (cfg == 0xFF) {
        snprintf(buf, n, "%02X　全入 In", cfg);
        return;
    }
    if (cfg == 0) {
        snprintf(buf, n, "%02X　全出 Out", cfg);
        return;
    }
    int pos = snprintf(buf, n, "%02X　入 In", cfg);
    for (int i = 0; i < 8 && pos > 0 && pos < (int)n - 3; i++) {
        if (cfg & (1U << i)) {
            pos += snprintf(buf + pos, n - (size_t)pos, " %d", i);
        }
    }
}

static void ioe_inv_text(char* buf, size_t n, uint8_t inv) {
    snprintf(buf, n, inv == 0 ? "%02X　不反 None" : "%02X　反 Inv", inv);
}

static void draw_page(uint8_t* framebuffer, const read_pico_status_t* status) {
    const uint8_t in0 = (uint8_t)status->ioe_input;
    const uint8_t out0 = (uint8_t)status->ioe_output;
    const uint8_t cfg0 = (uint8_t)status->ioe_config;
    const uint8_t cfg1 = (uint8_t)(status->ioe_config >> 8);
    const uint8_t inv0 = (uint8_t)status->ioe_invert;
    const uint8_t inv1 = (uint8_t)(status->ioe_invert >> 8);
    const ioe_geom_t g = ioe_geom();
    const bool rst_low = !pin_high(out0, IOE_BIT_TP_RST);

    char sub[48];
    char left[32];
    char right[24];
    ui_clear_page(framebuffer);
    snprintf(
        sub, sizeof(sub), "IN %02X　OUT %02X　INT %s",
        in0, out0, ioe_level(status->ioe_int_level != 0)
    );
    ui_draw_header(framebuffer, IOE_TITLE, sub);

    ui_draw_section(framebuffer, g.in_y, "输入 Inputs");
    int y = g.in_y + UI_SEC_HEAD;
    y = ui_draw_row2(
        framebuffer, y,
        "P0.2 CW_INT", ioe_level(pin_high(in0, IOE_BIT_CW_INT)),
        "P0.5 PGOOD", ioe_level(pin_high(in0, IOE_BIT_PGOOD))
    );
    ui_draw_row2(
        framebuffer, y, "P0.6 SD_CD", ioe_sd(pin_high(in0, IOE_BIT_SD_CD)), NULL, NULL
    );

    ui_draw_section(framebuffer, g.out_y, "输出 Outputs");
    y = g.out_y + UI_SEC_HEAD;
    y = ui_draw_row2(
        framebuffer, y,
        "P0.0 MODE", ioe_level(pin_high(out0, IOE_BIT_MODE)),
        "P0.1 XOE", ioe_level(pin_high(out0, IOE_BIT_XOE))
    );
    y = ui_draw_row2(
        framebuffer, y,
        "P0.3 SY_EN", ioe_level(pin_high(out0, IOE_BIT_SY_EN)),
        "P0.4 VCOM_EN", ioe_level(pin_high(out0, IOE_BIT_VCOM_EN))
    );
    ui_draw_row2(
        framebuffer, y,
        "P0.7 TP_RST", ioe_level(pin_high(out0, IOE_BIT_TP_RST)), NULL, NULL
    );

    ui_draw_section(framebuffer, g.cfg_y, "芯片配置 Config");
    y = g.cfg_y + UI_SEC_HEAD;
    ioe_dir_text(left, sizeof(left), cfg0);
    if (cfg1 == 0xFF) {
        snprintf(right, sizeof(right), "%02X　未接 Unused", cfg1);
    } else {
        ioe_dir_text(right, sizeof(right), cfg1);
    }
    y = ui_draw_row2(framebuffer, y, "CFG0", left, "CFG1", right);
    ioe_inv_text(left, sizeof(left), inv0);
    ioe_inv_text(right, sizeof(right), inv1);
    y = ui_draw_row2(framebuffer, y, "INV0", left, "INV1", right);
    snprintf(
        left, sizeof(left), "GPIO%d　%s",
        READ_PICO_IOE_INT_GPIO, ioe_level(status->ioe_int_level != 0)
    );
    snprintf(right, sizeof(right), "%02X", IOE_CFG0_EXPECT);
    ui_draw_row2(framebuffer, y, "INT#", left, "期望 CFG", right);

    ui_draw_button(framebuffer, g.tp_rst, "复位触摸 Reset TP", rst_low);
    ui_draw_menu_handle(framebuffer, false);
}

static bool status_changed(const read_pico_status_t* a, const read_pico_status_t* b) {
    return a->ioe_input != b->ioe_input
        || a->ioe_output != b->ioe_output
        || a->ioe_invert != b->ioe_invert
        || a->ioe_config != b->ioe_config
        || a->ioe_int_level != b->ioe_int_level;
}

static void show(app_ctx_t* ctx) {
    read_pico_get_ioe_status(&s_drawn);
    draw_page(ctx->fb, &s_drawn);
    guard_draw_result(ctx->hl, update_display_mode(ctx->hl, APP_PAGE_REFRESH_MODE));
}

static void render(app_ctx_t* ctx, uint8_t* fb) {
    (void)ctx;
    read_pico_get_ioe_status(&s_drawn);
    draw_page(fb, &s_drawn);
}

static void on_enter(app_ctx_t* ctx) {
    esp_err_t err = read_pico_get_ioe_status_full(&s_drawn);
    if (err != ESP_OK) ESP_LOGW(TAG, "IOE status: %s", esp_err_to_name(err));
    s_last_poll_ms = ctx->now_ms;
}

static EpdRect area_hint(app_ctx_t* ctx) {
    (void)ctx;
    return ui_content_refresh_area();
}

static app_redraw_t on_touch(app_ctx_t* ctx, const cst836u_touch_t* touch) {
    const ioe_geom_t g = ioe_geom();
    if (!ui_rect_hit(g.tp_rst, touch->x, touch->y)) return APP_REDRAW_NONE;

    read_pico_tp_rst(false);
    show(ctx);
    read_pico_tp_rst(true);
    show(ctx);
    ESP_LOGI(TAG, "TP_RST pulsed");
    s_last_poll_ms = ctx->now_ms;
    return APP_REDRAW_DONE;
}

static app_redraw_t on_tick(app_ctx_t* ctx) {
    const int wait = read_pico_ioe_int_level() == 0 ? IOE_POLL_MS : IOE_IDLE_MS;
    if (ctx->now_ms - s_last_poll_ms < wait) return APP_REDRAW_NONE;
    s_last_poll_ms = ctx->now_ms;

    read_pico_status_t status = { 0 };
    if (read_pico_get_ioe_status(&status) != ESP_OK) return APP_REDRAW_NONE;
    if (!status_changed(&status, &s_drawn)) return APP_REDRAW_NONE;
    s_drawn = status;
    draw_page(ctx->fb, &status);
    return APP_REDRAW_AREA;
}

const app_desc_t app_ioe = {
    .title = IOE_TITLE,
    .detail = "Port-0 电平与中断 Pin Levels & IRQ",
    .render = render,
    .on_enter = on_enter,
    .on_touch = on_touch,
    .on_tick = on_tick,
    .area_hint = area_hint,
};
