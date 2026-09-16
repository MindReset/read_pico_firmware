/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 电源与电池。电池/充电/灯取 CW32 PMU，轨温/故障/配置取 SY7636A。
 *
 * Power and battery. Battery / charge / LED come from the CW32 PMU; rail
 * temperature / fault / config come from SY7636A.
 *
 * 冻结：不关轨；不写 VCOM / VLDO / 放电 / 延时 / VCOMCTL；
 * 底栏只留读取；VCOMCTL 只读（默认外部 VCOM_EN）；
 * 故障行写「代码 xx」；说明栏禁止改出厂 PMU。
 * KEY3 走主循环菜单把手。
 * Frozen: do not cut rails; do not write VCOM / VLDO / discharge / delay /
 * VCOMCTL; bar is read only; VCOMCTL is read-only (default external VCOM_EN);
 * fault row says "代码 xx"; notes forbid changing the factory PMU.
 * KEY3 is the loop menu handle.
 */

#include <stdio.h>
#include <string.h>

#include "app.h"
#include "display.h"
#include "esp_log.h"
#include "read_pico_board.h"
#include "read_pico_pmu.h"
#include "ui_kit.h"
#include "ui_menu.h"

#define TAG "app_power"

#define PWR_TITLE "电源与电池 Power"
#define PWR_POLL_MS 1500
#define PWR_BAT_ROWS 2
#define PWR_RAIL_ROWS 2
#define PWR_CFG_ROWS 3
#define PWR_PGOOD_BIT (1U << 5)
#define PWR_HIT_NONE (-1)
#define PWR_HIT_READ 0

#define PWR_NOTE_ZH0 "屏幕电源参数已在出厂时写入 PMU，"
#define PWR_NOTE_ZH1 "不要更改，否则会永久损坏硬件。"
#define PWR_NOTE_EN "Factory-written in the PMU. Do not change it."

typedef struct {
    int bat_y;
    int rail_y;
    int cfg_y;
    int note_y;
    EpdRect read;
} pwr_geom_t;

static read_pico_status_t s_drawn;
static int64_t s_last_poll_ms;
static int s_vcom_mv;
static bool s_vcom_valid;

static pwr_geom_t pwr_geom(void) {
    const int bat_y = UI_CONTENT_TOP;
    const int rail_y = bat_y + UI_SEC_HEAD + PWR_BAT_ROWS * UI_ROW_H_SM + UI_SECTION_GAP;
    const int cfg_y = rail_y + UI_SEC_HEAD + PWR_RAIL_ROWS * UI_ROW_H_SM + UI_SECTION_GAP;
    const int note_y = cfg_y + UI_SEC_HEAD + PWR_CFG_ROWS * UI_ROW_H_SM + UI_SECTION_GAP;
    return (pwr_geom_t){
        .bat_y = bat_y,
        .rail_y = rail_y,
        .cfg_y = cfg_y,
        .note_y = note_y,
        .read = ui_bar_rect(0, 1),
    };
}

static int pwr_hit(const pwr_geom_t* g, uint16_t x, uint16_t y) {
    return ui_rect_hit(g->read, x, y) ? PWR_HIT_READ : PWR_HIT_NONE;
}

static void refresh_vcom(void) {
    s_vcom_valid = read_pico_pmu_vcom_get(&s_vcom_mv) == ESP_OK;
}

static const char* charge_text(const pmu_snapshot_t* s) {
    if (!s->status_ok) return "未读到 No data";
    switch (s->charge_state) {
        case PMU_CHARGE_NOT_CHARGING: return "未充电 Idle";
        case PMU_CHARGE_CHARGING: return "充电中 Charging";
        case PMU_CHARGE_FULL_INFERRED: return "已满 Full";
        case PMU_CHARGE_FAULT: return "故障 Fault";
        default: return "未知 Unknown";
    }
}

static const char* led_text(const pmu_snapshot_t* s) {
    if (!s->status_ok) return "未读到 No data";
    // STATUS[18] 读到了；高位偶有附加标志，灯色只看低 2 位。/ STATUS[18] is valid; high bits may carry extra flags, so LED color uses the low 2 bits only.
    switch (s->led_state & 0x03) {
        case PMU_LED_RED: return "红 Red";
        case PMU_LED_WHITE: return "白 White";
        case PMU_LED_RED_WHITE: return "红白 Both";
        default: return "关 Off";
    }
}

// OP 里这些位置 1 是关掉该路主动放电。0 为出厂默认，关电时四路都泄放。/ In OP, a 1 bit disables that rail's active discharge. 0 is factory default: all four dump when power is cut.
static void dischg_text(uint8_t dis, char* out, size_t n) {
    static const struct {
        uint8_t bit;
        const char* name;
    } rails[] = {
        { SY7636A_DISCHG_VDDH, "VDDH" },
        { SY7636A_DISCHG_VPOS, "VPOS" },
        { SY7636A_DISCHG_VNEG, "VNEG" },
        { SY7636A_DISCHG_VCOM, "VCOM" },
    };
    unsigned off = 0;
    for (unsigned i = 0; i < 4; i++) {
        if (dis & rails[i].bit) off++;
    }
    if (off == 0) {
        snprintf(out, n, "四路全开 All on");
        return;
    }
    if (off == 4) {
        snprintf(out, n, "四路全关 All off");
        return;
    }
    char on[28] = { 0 };
    char off_s[28] = { 0 };
    for (unsigned i = 0; i < 4; i++) {
        char* dst = (dis & rails[i].bit) ? off_s : on;
        if (dst[0] != '\0') strncat(dst, " ", sizeof(on) - strlen(dst) - 1);
        strncat(dst, rails[i].name, sizeof(on) - strlen(dst) - 1);
    }
    snprintf(out, n, "开 %s　关 %s", on[0] ? on : "-", off_s[0] ? off_s : "-");
}

static const char* sy_fault_name(sy7636a_fault_t fault) {
    switch (fault) {
        case SY7636A_FAULT_NONE: return "正常 OK";
        case SY7636A_FAULT_UVP_VP: return "欠压正 UVP+";
        case SY7636A_FAULT_UVP_VN: return "欠压负 UVP-";
        case SY7636A_FAULT_UVP_VPOS: return "欠压正轨 UVP+";
        case SY7636A_FAULT_UVP_VNEG: return "欠压负轨 UVP-";
        case SY7636A_FAULT_UVP_VDDH: return "欠压高压 UVP";
        case SY7636A_FAULT_UVP_VEE: return "欠压负压 UVP";
        case SY7636A_FAULT_SCP_VP: return "短路正 SCP+";
        case SY7636A_FAULT_SCP_VN: return "短路负 SCP-";
        case SY7636A_FAULT_SCP_VPOS: return "短路正轨 SCP+";
        case SY7636A_FAULT_SCP_VNEG: return "短路负轨 SCP-";
        case SY7636A_FAULT_SCP_VDDH: return "短路高压 SCP";
        case SY7636A_FAULT_SCP_VEE: return "短路负压 SCP";
        case SY7636A_FAULT_SCP_VCOM: return "短路 VCOM";
        case SY7636A_FAULT_OTP: return "过温 OTP";
        default: return "故障 Fault";
    }
}

static int pwr_draw_notes(uint8_t* framebuffer, int y) {
    static const struct {
        const char* text;
        int px;
    } notes[] = {
        { PWR_NOTE_ZH0, UI_PX_BODY },
        { PWR_NOTE_ZH1, UI_PX_BODY },
        { PWR_NOTE_EN, UI_PX_CAPTION },
    };
    for (unsigned i = 0; i < sizeof(notes) / sizeof(notes[0]); i++) {
        ui_text(
            framebuffer, UI_MARGIN, y, notes[i].px, notes[i].text,
            EPD_DRAW_ALIGN_LEFT, false
        );
        y += notes[i].px + 10;
    }
    return y;
}

static void draw_page(uint8_t* framebuffer, const read_pico_status_t* status) {
    char line[56];
    char aux[48];
    const bool pgood = ((uint8_t)status->ioe_input & PWR_PGOOD_BIT) != 0;
    const sy7636a_status_t* sy = &status->sy;
    const pmu_snapshot_t* pmu = read_pico_pmu_get();
    const pwr_geom_t g = pwr_geom();

    ui_clear_page(framebuffer);
    ui_draw_header(framebuffer, PWR_TITLE, "EPD PMIC: SY7636A");

    int y = ui_draw_section(framebuffer, g.bat_y, "电池 Battery");
    const char* none = "未读到 No data";
    const char* volt = none;
    const char* soc = none;
    const char* chg = none;
    const char* led = none;
    if (pmu->status_ok) {
        snprintf(
            line, sizeof(line), "%u.%03u V",
            pmu->battery_mv / 1000, pmu->battery_mv % 1000
        );
        snprintf(aux, sizeof(aux), "%u%%", pmu->soc_permille / 10);
        volt = line;
        soc = aux;
        chg = charge_text(pmu);
        led = led_text(pmu);
    }
    y = ui_draw_row2(framebuffer, y, "电压 Voltage", volt, "电量 SOC", soc);
    y = ui_draw_row2(framebuffer, y, "充电 Charge", chg, "指示灯 LED", led);

    y = ui_draw_section(framebuffer, g.rail_y, "屏幕电源 EPD Power");
    snprintf(line, sizeof(line), "%d ℃", sy->temperature_c);
    y = ui_draw_row2(
        framebuffer, y, "温度 Temp", line,
        "就绪 PGOOD", pgood ? "是 Yes" : "否 No"
    );
    // 寄存器原值：bit0=PG，故障码在 bit[4:1]。01 是就绪且无故障，不是 1 号故障。/ Raw register: bit0=PG, fault code in bit[4:1]. 01 is ready with no fault, not fault number 1.
    snprintf(line, sizeof(line), "代码 %02X　%s", sy->fault_reg, sy_fault_name(sy->fault));
    y = ui_draw_row2(framebuffer, y, "故障 Fault", line, NULL, NULL);

    y = ui_draw_section(framebuffer, g.cfg_y, "芯片配置");
    if (s_vcom_valid) {
        snprintf(aux, sizeof(aux), "-%d mV", s_vcom_mv);
    } else {
        snprintf(aux, sizeof(aux), "未标定 Unset");
    }
    y = ui_draw_row2(
        framebuffer, y, "源极 VLDO", sy7636a_vldo_name(sy->vldo), "面板 VCOM", aux
    );
    dischg_text(sy->discharge, line, sizeof(line));
    y = ui_draw_row2(framebuffer, y, "掉电放电 Discharge", line, NULL, NULL);
    snprintf(
        line, sizeof(line), "%d / %d / %d / %d ms",
        sy->dly_ms[0], sy->dly_ms[1], sy->dly_ms[2], sy->dly_ms[3]
    );
    ui_draw_row2(
        framebuffer, y, "上电延时 Delay", line,
        "门控 Gate", sy->vcom_manual ? "VCOM_EN" : "自动 Auto"
    );

    y = ui_draw_section(framebuffer, g.note_y, "说明");
    pwr_draw_notes(framebuffer, y);

    ui_draw_button(framebuffer, g.read, "读取 Read", false);
    ui_draw_menu_handle(framebuffer, false);
}

static bool status_changed(const read_pico_status_t* a, const read_pico_status_t* b) {
    return a->ioe_input != b->ioe_input
        || a->ioe_output != b->ioe_output
        || a->rails_on != b->rails_on
        || a->sy_live != b->sy_live
        || memcmp(&a->sy, &b->sy, sizeof(a->sy)) != 0;
}

static void render(app_ctx_t* ctx, uint8_t* fb) {
    (void)ctx;
    // SY7636A 的 I2C 和温度要等轨道起来才有效，调用方已经上电。/ SY7636A I2C and temperature are valid only after the rails are up; the caller already powered them.
    read_pico_status_t status = { 0 };
    read_pico_get_status(&status);
    draw_page(fb, &status);
}

static void on_enter(app_ctx_t* ctx) {
    epd_poweron();
    read_pico_pmu_refresh();
    refresh_vcom();
    esp_err_t err = read_pico_get_status(&s_drawn);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Board status using fallback: %s", esp_err_to_name(err));
    }
    s_last_poll_ms = ctx->now_ms;
}

static EpdRect area_hint(app_ctx_t* ctx) {
    (void)ctx;
    return ui_content_refresh_area();
}

static app_redraw_t on_touch(app_ctx_t* ctx, const cst836u_touch_t* touch) {
    const pwr_geom_t g = pwr_geom();
    if (pwr_hit(&g, touch->x, touch->y) != PWR_HIT_READ) return APP_REDRAW_NONE;

    if (!read_pico_rails_on()) epd_poweron();
    read_pico_pmu_poll();
    refresh_vcom();
    read_pico_get_status(&s_drawn);
    s_last_poll_ms = ctx->now_ms;
    rails_keepalive();
    return APP_REDRAW_PAGE;
}

static app_redraw_t on_tick(app_ctx_t* ctx) {
    if (!read_pico_rails_on()) return APP_REDRAW_NONE;
    if (ctx->now_ms - s_last_poll_ms < PWR_POLL_MS) return APP_REDRAW_NONE;
    s_last_poll_ms = ctx->now_ms;

    read_pico_status_t status = { 0 };
    read_pico_get_status(&status);
    read_pico_pmu_poll();
    rails_keepalive();
    if (!status_changed(&status, &s_drawn)) return APP_REDRAW_NONE;
    s_drawn = status;
    draw_page(ctx->fb, &status);
    return APP_REDRAW_AREA;
}

const app_desc_t app_power = {
    .title = PWR_TITLE,
    .detail = "电池与屏幕电源 Battery & EPD Power",
    .render = render,
    .on_enter = on_enter,
    .on_touch = on_touch,
    .on_tick = on_tick,
    .area_hint = area_hint,
};
