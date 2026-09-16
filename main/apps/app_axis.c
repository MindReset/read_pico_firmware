/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * SC7A20H。读数页 Live / Tap / Orient，诊断页 Setup / Event / FIFO / Test。
 * 两页共用绘制和芯片状态，靠 app_desc_t.user 区分子视图。
 *
 * SC7A20H. Live / Tap / Orient on the reading page; Setup / Event / FIFO /
 * Test on the lab page. Both share draw and chip state; app_desc_t.user
 * selects the view subset.
 *
 * 冻结：默认不跟手，点启用监听才 DU；敲击提到 200Hz；FIFO 默认存满即停；
 * 自测/噪声前旁路 FIFO；KEY3 走主循环菜单把手。
 * Frozen: no live tracking by default; Start Listen starts DU; tap uses 200Hz;
 * FIFO defaults to stop-when-full; bypass FIFO before self-test/noise.
 * KEY3 is the loop menu handle.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "display.h"
#include "e0470_epaper_waveform.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "read_pico_board.h"
#include "read_pico_init.h"
#include "sc7a20h_lab.h"
#include "ui_kit.h"
#include "ui_menu.h"

#define TAG "app_axis"

#define AXIS_TITLE "加速度计 Accel"
#define AXIS_LAB_TITLE "加速度计诊断 IMU Lab"
#define AXIS_BTN_LISTEN_LIVE "启用监听 Start Listen"
#define AXIS_BTN_LISTEN_TAP "启用监听 Start Tap"
#define AXIS_BTN_LISTEN_ORI "启用监听 Start Orient"
#define AXIS_BTN_LISTEN_FF "启用失重 Start Fall"
#define AXIS_BTN_LISTEN_ACT "启用晃动 Start Shake"
#define AXIS_BTN_ZERO "设为零点 Set Zero"

// 跟随用的短波形一次连 diff 带扫描只要 50ms，间隔压到刚好盖住一次刷新的耗时。/ FOLLOW DU takes about 50ms; interval is just enough to cover one refresh.
#define AXIS_UPDATE_INTERVAL_MS 120
// 非实时视图只等事件，慢一点也够。/ Non-live views only wait for events; a slower poll is enough.
#define AXIS_EVENT_INTERVAL_MS 400
// 桌面噪声经常超过 8 mg；跟手时三轴都在这个范围内才定稿。/ Desk noise often exceeds 8 mg; while tracking, settle only when all three axes stay in this band.
#define AXIS_STILL_MG 40

#define UI_AXIS_VIEW_LIVE 0
#define UI_AXIS_VIEW_CFG 1
#define UI_AXIS_VIEW_TAP 2
#define UI_AXIS_VIEW_ORI 3
#define UI_AXIS_VIEW_EVT 4
#define UI_AXIS_VIEW_FIFO 5
#define UI_AXIS_VIEW_DIAG 6
#define UI_AXIS_VIEW_COUNT 7

#define UI_AXIS_HIT_NONE (-1)
#define UI_AXIS_HIT_TAB0 0
#define UI_AXIS_HIT_LIVE_HOLD 10
#define UI_AXIS_HIT_LIVE_ZERO 11
#define UI_AXIS_HIT_CFG_ODR_DEC 20
#define UI_AXIS_HIT_CFG_ODR_INC 21
#define UI_AXIS_HIT_CFG_FS_DEC 22
#define UI_AXIS_HIT_CFG_FS_INC 23
#define UI_AXIS_HIT_CFG_MODE 24
#define UI_AXIS_HIT_CFG_OSR 25
#define UI_AXIS_HIT_CFG_DLPF 26
#define UI_AXIS_HIT_CFG_HPF 27
#define UI_AXIS_HIT_TAP_ARM 30
#define UI_AXIS_HIT_TAP_AXIS 31
#define UI_AXIS_HIT_TAP_THS_DEC 32
#define UI_AXIS_HIT_TAP_THS_INC 33
#define UI_AXIS_HIT_TAP_CLR 34
#define UI_AXIS_HIT_ORI_ARM 40
#define UI_AXIS_HIT_ORI_4D 41
#define UI_AXIS_HIT_ORI_THS_DEC 42
#define UI_AXIS_HIT_ORI_THS_INC 43
#define UI_AXIS_HIT_ORI_DUR_DEC 44
#define UI_AXIS_HIT_ORI_DUR_INC 45
#define UI_AXIS_HIT_EVT_FF 50
#define UI_AXIS_HIT_EVT_ACT 51
#define UI_AXIS_HIT_EVT_ROUTE 52
#define UI_AXIS_HIT_EVT_CLR 53
#define UI_AXIS_HIT_FIFO_MODE 60
#define UI_AXIS_HIT_FIFO_WTM_DEC 61
#define UI_AXIS_HIT_FIFO_WTM_INC 62
#define UI_AXIS_HIT_FIFO_BITS 63
#define UI_AXIS_HIT_FIFO_READ 64
#define UI_AXIS_HIT_FIFO_CLR 65
#define UI_AXIS_HIT_DIAG_ST 70
#define UI_AXIS_HIT_DIAG_STATS 71
#define UI_AXIS_HIT_DIAG_DUMP 72
#define UI_AXIS_HIT_DIAG_RESET 73

typedef struct {
    int view;
    sc7a20h_sample_t sample;
    bool hold;
    bool zeroed;
    int16_t zero_x;
    int16_t zero_y;
    int16_t zero_z;
    sc7a20h_sensor_config_t cfg;
    uint16_t measured_odr_x10;
    bool click_armed;
    uint8_t click_axis;
    sc7a20h_click_ths_t click_ths;
    uint8_t click_src;
    uint8_t click_last;
    int click_count;
    bool ori_armed;
    bool ori_4d;
    uint8_t ori_ths;
    uint8_t ori_dur;
    uint8_t ori_src;
    sc7a20h_orient_t orient;
    bool ff_armed;
    bool act_armed;
    uint8_t aoi2_src;
    int ff_count;
    int act_count;
    int64_t last_ff_ms;
    int64_t last_act_ms;
    uint8_t int_route;
    uint32_t int1_count;
    int int1_level;
    sc7a20h_fifo_mode_t fifo_mode;
    uint8_t fifo_wtm;
    bool fifo_8bit;
    sc7a20h_fifo_status_t fifo_st;
    sc7a20h_sample_t fifo_buf[SC7A20H_FIFO_MAX];
    int fifo_n;
    sc7a20h_selftest_t st;
    bool st_valid;
    sc7a20h_stats_t stats;
    bool stats_valid;
    uint8_t dump[SC7A20H_DUMP_LEN];
    bool dump_valid;
    uint8_t whoami;
    uint8_t version;
    char last_msg[72];
    bool busy;
} ui_axis_state_t;

// 两页共用这一份芯片状态：诊断页改完采样参数，切回读数页立刻是新参数。/ Both pages share this chip state: a lab change is already in effect when returning to the reading page.
static ui_axis_state_t s_axis_ui = {
    .cfg = SC7A20H_SENSOR_CONFIG_DEFAULT,
    .hold = true,
    .zeroed = true,
    .zero_x = READ_PICO_ACCEL_ZERO_X_MG,
    .zero_y = READ_PICO_ACCEL_ZERO_Y_MG,
    .zero_z = READ_PICO_ACCEL_ZERO_Z_MG,
    .ori_ths = 32,
    .ori_dur = 2,
    .fifo_mode = SC7A20H_FIFO_MODE,
    .fifo_wtm = 16,
    .click_axis = 2,
    .click_ths = SC7A20H_CLICK_SOFT,
};

static sc7a20h_handle_t s_acc;
static sc7a20h_sample_t s_drawn_sample;
static bool s_settled;
static int64_t s_last_update_ms;

typedef struct {
    const char* title;
    const int* views;
    int count;
} axis_tier_t;

static const int k_demo_views[] = {
    UI_AXIS_VIEW_LIVE, UI_AXIS_VIEW_TAP, UI_AXIS_VIEW_ORI,
};
static const int k_lab_views[] = {
    UI_AXIS_VIEW_CFG, UI_AXIS_VIEW_EVT, UI_AXIS_VIEW_FIFO, UI_AXIS_VIEW_DIAG,
};

static axis_tier_t s_demo_tier = {
    .title = AXIS_TITLE,
    .views = k_demo_views,
    .count = (int)(sizeof(k_demo_views) / sizeof(k_demo_views[0])),
};
static axis_tier_t s_lab_tier = {
    .title = AXIS_LAB_TITLE,
    .views = k_lab_views,
    .count = (int)(sizeof(k_lab_views) / sizeof(k_lab_views[0])),
};

#define UI_SENSOR_RANGE_MG 2000
#define UI_SENSOR_BAR_H 40
#define UI_SENSOR_STRIDE 148
#define UI_SENSOR_TOP (UI_CONTENT_TOP + UI_SEC_HEAD + 2 * UI_ROW_H_SM + 8)
#define UI_FIFO_WAVE_H 180
#define UI_ORI_CHIP_H 64
#define UI_ORI_CHIP_GAP 8
#define UI_ORI_COLS 3
#define UI_AXIS_BTN_Y (UI_CONTENT_BOTTOM - UI_BTN_H)
#define UI_AXIS_BTN2_Y (UI_AXIS_BTN_Y - UI_BTN_H - UI_GAP)

typedef struct {
    int btn_y;
    int btn2_y;
    int sensor_y[3];
    EpdRect refresh;
} axis_geom_t;

static axis_geom_t axis_geom(void) {
    axis_geom_t g = {
        .btn_y = UI_AXIS_BTN_Y,
        .btn2_y = UI_AXIS_BTN2_Y,
        .refresh = {
            .x = UI_MARGIN - 5,
            .y = UI_CONTENT_TOP,
            .width = ui_content_width() + 10,
            .height = UI_AXIS_BTN_Y - UI_CONTENT_TOP - 8,
        },
    };
    for (int i = 0; i < 3; i++) g.sensor_y[i] = UI_SENSOR_TOP + i * UI_SENSOR_STRIDE;
    return g;
}

static int axis_band_y(const axis_geom_t* g, int row) {
    return row == 0 ? g->btn2_y : g->btn_y;
}

static EpdRect axis_grid(int col, int cols, int row, int y0) {
    return ui_grid_rect(col, cols, row, y0, UI_BTN_H);
}

static EpdRect axis_ori_chip(int i, int y0) {
    const int w = (ui_content_width() - (UI_ORI_COLS - 1) * UI_ORI_CHIP_GAP) / UI_ORI_COLS;
    return (EpdRect){
        .x = UI_MARGIN + (i % UI_ORI_COLS) * (w + UI_ORI_CHIP_GAP),
        .y = y0 + (i / UI_ORI_COLS) * (UI_ORI_CHIP_H + UI_ORI_CHIP_GAP),
        .width = w,
        .height = UI_ORI_CHIP_H,
    };
}

// 中英并列比单行中文长，按钮用略小一档，避免窄格裁字。/ Bilingual labels are longer than Chinese-only; use a smaller button size so narrow cells do not clip.
static void axis_draw_btn(uint8_t* framebuffer, EpdRect rect, const char* label, bool on) {
    ui_draw_choice_round_rect(framebuffer, rect, UI_BTN_RADIUS, on);
    ui_text_vc(
        framebuffer, rect.x + rect.width / 2, rect.y + rect.height / 2,
        UI_PX_LABEL_SM, label, EPD_DRAW_ALIGN_CENTER, false
    );
}

static const char* const k_view_labels[UI_AXIS_VIEW_COUNT] = {
    "读数 Live", "设置 Setup", "敲击 Tap", "朝向 Orient",
    "动作 Event", "波形 FIFO", "自检 Test",
};

// 标签栏按这一页挂上的子集画。/ Draw the tab bar from the subset attached to this page.
static void draw_axis_tabs(
    uint8_t* framebuffer, int view, const int* views, int count
) {
    for (int i = 0; i < count; i++) {
        EpdRect rect = ui_bar_rect(i, count);
        ui_draw_choice_round_rect(framebuffer, rect, UI_BTN_RADIUS, view == views[i]);
        ui_text_vc(
            framebuffer, rect.x + rect.width / 2, rect.y + rect.height / 2,
            22, k_view_labels[views[i]], EPD_DRAW_ALIGN_CENTER, false
        );
    }
}

static void draw_axis_block(
    uint8_t* framebuffer, int y, int value, const char* axis, int raw
) {
    char label[48];
    char text[24];
    snprintf(label, sizeof(label), "%s 轴 Axis　原值 Raw %d", axis, raw);
    snprintf(text, sizeof(text), "%d", value);

    ui_text(
        framebuffer, UI_MARGIN, y + 8, UI_PX_LABEL_SM, label,
        EPD_DRAW_ALIGN_LEFT, false
    );
    ui_text(
        framebuffer, ui_content_right() - 52, y - 4, 56, text,
        EPD_DRAW_ALIGN_RIGHT, false
    );
    ui_text(
        framebuffer, ui_content_right(), y + 28, UI_PX_CAPTION, "mg",
        EPD_DRAW_ALIGN_RIGHT, false
    );

    const int left = UI_MARGIN;
    const int width = ui_content_width();
    const int center = left + width / 2;
    const int bar_y = y + 72;
    int length = value * (width / 2) / UI_SENSOR_RANGE_MG;
    if (length > width / 2) length = width / 2;
    if (length < -width / 2) length = -width / 2;

    epd_draw_rect(
        (EpdRect){ .x = left, .y = bar_y, .width = width, .height = UI_SENSOR_BAR_H },
        UI_GRAY_BLACK, framebuffer
    );
    epd_draw_line(
        center, bar_y, center, bar_y + UI_SENSOR_BAR_H - 1, UI_GRAY_BLACK, framebuffer
    );
    const int fill_h = UI_SENSOR_BAR_H - 10;
    if (length >= 0) {
        epd_fill_rect(
            (EpdRect){ .x = center, .y = bar_y + 5, .width = length, .height = fill_h },
            UI_GRAY_BLACK, framebuffer
        );
    } else {
        epd_fill_rect(
            (EpdRect){
                .x = center + length, .y = bar_y + 5,
                .width = -length, .height = fill_h,
            },
            UI_GRAY_BLACK, framebuffer
        );
    }
}

static int axis_disp(int value, int zero, bool zeroed) {
    return zeroed ? value - zero : value;
}

static const char* ui_axis_mode_name(sc7a20h_mode_t mode) {
    switch (mode) {
        case SC7A20H_MODE_NORMAL: return "正常 Normal";
        case SC7A20H_MODE_LP: return "低功耗 Low Power";
        case SC7A20H_MODE_HR: return "高性能 High Res";
        case SC7A20H_MODE_ENHANCED: return "增强 Enhanced";
        default: return "--";
    }
}

static const char* ui_axis_orient_name(sc7a20h_orient_t orient) {
    switch (orient) {
        case SC7A20H_ORIENT_PX: return "X 正向立 +X Up";
        case SC7A20H_ORIENT_NX: return "X 负向立 -X Up";
        case SC7A20H_ORIENT_PY: return "Y 正向立 +Y Up";
        case SC7A20H_ORIENT_NY: return "Y 负向立 -Y Up";
        case SC7A20H_ORIENT_PZ: return "屏幕朝上 Face Up";
        case SC7A20H_ORIENT_NZ: return "屏幕朝下 Face Down";
        default: return "未判定 Unknown";
    }
}

static const char* ui_axis_click_name(uint8_t count) {
    switch (count) {
        case 0: return "无 None";
        case 1: return "单击 Single";
        case 2: return "双击 Double";
        case 3: return "三击 Triple";
        default: return "多击 Multi";
    }
}

static const char* ui_axis_fifo_mode_name(sc7a20h_fifo_mode_t mode) {
    switch (mode) {
        case SC7A20H_FIFO_BYPASS: return "不缓存 Bypass";
        case SC7A20H_FIFO_MODE: return "存满即停 FIFO";
        case SC7A20H_FIFO_STREAM: return "循环覆盖 Stream";
        case SC7A20H_FIFO_TRIGGER: return "事件后存 Trigger";
        default: return "--";
    }
}

static const char* ui_axis_int_route_name(uint8_t mask) {
    if (mask & SC7A20H_INT1_CLICK) return "敲击 Click";
    if (mask & SC7A20H_INT1_AOI1) return "朝向 Orient";
    if (mask & SC7A20H_INT1_AOI2) return "动作 Motion";
    if (mask & SC7A20H_INT1_DRDY) return "数据就绪 DRDY";
    if (mask & SC7A20H_INT1_WTM) return "水位 Watermark";
    return "未连接 None";
}

static void draw_axis_live(uint8_t* framebuffer, const ui_axis_state_t* st) {
    char a[24];
    char p[24];
    char r[24];
    char s[40];
    int x = axis_disp(st->sample.x_mg, st->zero_x, st->zeroed);
    int yv = axis_disp(st->sample.y_mg, st->zero_y, st->zeroed);
    int z = axis_disp(st->sample.z_mg, st->zero_z, st->zeroed);
    int mag = (int)lround(sqrt((double)x * x + (double)yv * yv + (double)z * z));
    double pitch = atan2(-(double)x, sqrt((double)yv * yv + (double)z * z)) * (180.0 / 3.141592653589793);
    double roll = atan2((double)yv, (double)z) * (180.0 / 3.141592653589793);
    snprintf(a, sizeof(a), "%d mg", mag);
    snprintf(p, sizeof(p), "%d°", (int)lround(pitch));
    snprintf(r, sizeof(r), "%d°", (int)lround(roll));
    // ZYXOR 只表示两次读之间芯片又出了新样本。读数页刷新远慢于 ODR，
    // 几乎每次都会置位，不当故障提示。
    // ZYXOR only means the chip produced a new sample between reads. The live
    // page is far slower than ODR, so it is almost always set; not a fault.
    snprintf(s, sizeof(s), "%s", sc7a20h_odr_name(st->cfg.odr));

    int y = ui_draw_section(
        framebuffer, UI_CONTENT_TOP, "启用监听后晃动设备 Enable, then tilt"
    );
    y = ui_draw_row2(framebuffer, y, "合加速度 Accel", a, "采样 ODR", s);
    y = ui_draw_row2(framebuffer, y, "俯仰 Pitch", p, "横滚 Roll", r);
    (void)y;
    const axis_geom_t g = axis_geom();
    draw_axis_block(framebuffer, g.sensor_y[0], x, "X", st->sample.x_raw);
    draw_axis_block(framebuffer, g.sensor_y[1], yv, "Y", st->sample.y_raw);
    draw_axis_block(framebuffer, g.sensor_y[2], z, "Z", st->sample.z_raw);
    axis_draw_btn(
        framebuffer, axis_grid(0, 2, 0, g.btn_y),
        AXIS_BTN_LISTEN_LIVE, !st->hold
    );
    axis_draw_btn(
        framebuffer, axis_grid(1, 2, 0, g.btn_y),
        AXIS_BTN_ZERO, st->zeroed
    );
}

static const char* axis_osr_name(uint8_t osr) {
    static const char* const names[] = {
        "关闭 Off", "2 次 2×", "4 次 4×", "8 次 8×", "16 次 16×", "32 次 32×",
    };
    return osr <= 5 ? names[osr] : "--";
}

static const char* axis_dlpf_name(uint8_t dlpf) {
    static const char* const names[] = {
        "关闭 Off", "弱 Weak", "中 Medium", "强 Strong",
    };
    return dlpf <= 3 ? names[dlpf] : "--";
}

static const char* axis_hpf_name(uint8_t hpf) {
    if (hpf == 0) return "关闭 Off（含重力 Gravity）";
    static const char* const names[] = {
        "很慢 Slowest", "慢 Slow", "中 Medium", "快 Fast",
    };
    return names[(hpf - 1) & 3];
}

static void draw_axis_cfg(uint8_t* framebuffer, const ui_axis_state_t* st) {
    const sc7a20h_sensor_config_t* c = &st->cfg;
    int y = ui_draw_section(
        framebuffer, UI_CONTENT_TOP, "更改立即生效 Changes apply now"
    );
    y = ui_draw_row(framebuffer, y, "输出速率 ODR", sc7a20h_odr_name(c->odr));
    y = ui_draw_row(framebuffer, y, "量程 Range", sc7a20h_fs_name(c->fs));
    y = ui_draw_row(framebuffer, y, "功耗模式 Mode", ui_axis_mode_name(c->mode));
    y = ui_draw_row(framebuffer, y, "过采样 OSR", axis_osr_name(c->osr));
    y = ui_draw_row(framebuffer, y, "低通 DLPF", axis_dlpf_name(c->dlpf));
    ui_draw_row(framebuffer, y, "高通 HPF", axis_hpf_name(c->hpf));

    static const char* const row0[] = {
        "更慢 Slow", "更快 Fast", "量程- FS-", "量程+ FS+",
    };
    static const char* const row1[] = {
        "功耗 Mode", "平均 OSR", "低通 DLPF", "高通 HPF",
    };
    for (int i = 0; i < 4; i++) {
        axis_draw_btn(framebuffer, axis_grid(i, 4, 0, UI_AXIS_BTN2_Y), row0[i], false);
        axis_draw_btn(framebuffer, axis_grid(i, 4, 0, UI_AXIS_BTN_Y), row1[i], false);
    }
}

static const char* click_axis_name(uint8_t axis) {
    switch (axis) {
        case 0: return "仅 Z 轴";
        case 1: return "仅 XY 轴";
        default: return "三轴 XYZ";
    }
}

static const char* click_ths_name(uint8_t ths) {
    if (ths <= 1) return "灵敏 Sensitive";
    if (ths <= 3) return "适中 Medium";
    if (ths <= 5) return "较钝 Dull";
    return "很钝 Deadened";
}

static void draw_axis_tap(uint8_t* framebuffer, const ui_axis_state_t* st) {
    char n[16];
    snprintf(n, sizeof(n), "%d", st->click_count);
    int y = ui_draw_section(
        framebuffer, UI_CONTENT_TOP, "启用监听后轻敲机身 Enable, then tap"
    );
    y = ui_draw_row(framebuffer, y, "最近一次 Last", ui_axis_click_name(st->click_last));
    y = ui_draw_row(framebuffer, y, "已记录 Count", n);
    y = ui_draw_row(framebuffer, y, "监听轴向 Axis", click_axis_name(st->click_axis));
    y = ui_draw_row(framebuffer, y, "力度门槛 Threshold", click_ths_name(st->click_ths));
    ui_draw_row(
        framebuffer, y, "状态 Status",
        st->click_armed ? "监听中 Listening" : "未启用 Idle"
    );

    axis_draw_btn(
        framebuffer, axis_grid(0, 2, 0, UI_AXIS_BTN2_Y),
        AXIS_BTN_LISTEN_TAP, st->click_armed
    );
    axis_draw_btn(
        framebuffer, axis_grid(1, 2, 0, UI_AXIS_BTN2_Y), "切换轴向 Axis", false
    );
    static const char* const row1[] = { "更敏 Sens-", "更钝 Sens+", "清除 Clear" };
    for (int i = 0; i < 3; i++) {
        axis_draw_btn(framebuffer, axis_grid(i, 3, 0, UI_AXIS_BTN_Y), row1[i], false);
    }
}

static void draw_axis_ori(uint8_t* framebuffer, const ui_axis_state_t* st) {
    static const struct {
        const char* label;
        sc7a20h_orient_t id;
    } faces[6] = {
        { "X+ 正向", SC7A20H_ORIENT_PX },
        { "X- 负向", SC7A20H_ORIENT_NX },
        { "Y+ 正向", SC7A20H_ORIENT_PY },
        { "Y- 负向", SC7A20H_ORIENT_NY },
        { "朝上 Up", SC7A20H_ORIENT_PZ },
        { "朝下 Down", SC7A20H_ORIENT_NZ },
    };
    char ths[24];
    char dur[40];
    snprintf(ths, sizeof(ths), "%u mg", sc7a20h_ths_mg_fs(st->cfg.fs, st->ori_ths));
    snprintf(dur, sizeof(dur), "连续 %u 次 %u samples", st->ori_dur, st->ori_dur);

    int y = ui_draw_section(
        framebuffer, UI_CONTENT_TOP, "启用监听后缓慢翻转 Enable, then flip"
    );
    for (int i = 0; i < 6; i++) {
        ui_draw_chip(
            framebuffer, axis_ori_chip(i, y), faces[i].label,
            st->orient == faces[i].id
        );
    }
    y += 2 * (UI_ORI_CHIP_H + UI_ORI_CHIP_GAP) + UI_ORI_CHIP_GAP;
    y = ui_draw_row(framebuffer, y, "当前朝向 Orient", ui_axis_orient_name(st->orient));
    y = ui_draw_row(framebuffer, y, "判定门槛 Threshold", ths);
    y = ui_draw_row(framebuffer, y, "稳定次数 Duration", dur);
    ui_draw_row(
        framebuffer, y, "朝向范围 Coverage",
        st->ori_4d ? "四向立放 4D" : "六面朝向 6D"
    );

    axis_draw_btn(
        framebuffer, axis_grid(0, 2, 0, UI_AXIS_BTN2_Y),
        AXIS_BTN_LISTEN_ORI, st->ori_armed
    );
    axis_draw_btn(
        framebuffer, axis_grid(1, 2, 0, UI_AXIS_BTN2_Y), "四向 4D", st->ori_4d
    );
    static const char* const row1[] = {
        "门槛- Ths-", "门槛+ Ths+", "更短 Dur-", "更长 Dur+",
    };
    for (int i = 0; i < 4; i++) {
        axis_draw_btn(framebuffer, axis_grid(i, 4, 0, UI_AXIS_BTN_Y), row1[i], false);
    }
}

static void draw_axis_evt(uint8_t* framebuffer, const ui_axis_state_t* st) {
    char n[24];
    int y = ui_draw_section(framebuffer, UI_CONTENT_TOP, "失重 Freefall");
    snprintf(n, sizeof(n), "%d", st->ff_count);
    y = ui_draw_row(framebuffer, y, "次数 Count", n);
    y = ui_draw_row(
        framebuffer, y, "状态 Status",
        st->ff_armed ? "监听中 Listening" : "未启用 Idle"
    );

    y = ui_draw_section(framebuffer, y + UI_SECTION_GAP, "晃动 Shake");
    snprintf(n, sizeof(n), "%d", st->act_count);
    y = ui_draw_row(framebuffer, y, "次数 Count", n);
    y = ui_draw_row(
        framebuffer, y, "状态 Status",
        st->act_armed ? "监听中 Listening" : "未启用 Idle"
    );

    y = ui_draw_section(framebuffer, y + UI_SECTION_GAP, "中断脚 INT1 GPIO1");
    y = ui_draw_row(
        framebuffer, y, "电平 Level", st->int1_level ? "高 High" : "低 Low"
    );
    snprintf(n, sizeof(n), "%lu", (unsigned long)st->int1_count);
    y = ui_draw_row(framebuffer, y, "跳变 Edges", n);
    ui_draw_row(framebuffer, y, "路由 Route", ui_axis_int_route_name(st->int_route));
    ui_text(
        framebuffer, UI_MARGIN, UI_AXIS_BTN2_Y - 36, UI_PX_CAPTION,
        "INT2 本板未接线 INT2 is not connected", EPD_DRAW_ALIGN_LEFT, false
    );

    axis_draw_btn(
        framebuffer, axis_grid(0, 2, 0, UI_AXIS_BTN2_Y),
        AXIS_BTN_LISTEN_FF, st->ff_armed
    );
    axis_draw_btn(
        framebuffer, axis_grid(1, 2, 0, UI_AXIS_BTN2_Y),
        AXIS_BTN_LISTEN_ACT, st->act_armed
    );
    axis_draw_btn(framebuffer, axis_grid(0, 2, 0, UI_AXIS_BTN_Y), "切换脚 Route", false);
    axis_draw_btn(framebuffer, axis_grid(1, 2, 0, UI_AXIS_BTN_Y), "清除 Clear", false);
}

// FSS 5 位：满 32 组时为 0，EMPTY=0。旁路时 EMPTY=1。/ FSS is 5 bits: 0 when 32 samples are full and EMPTY=0. EMPTY=1 in bypass.
static int axis_fifo_level(const sc7a20h_fifo_status_t* st) {
    if (st == NULL || st->empty) return 0;
    return st->fss == 0 ? SC7A20H_FIFO_MAX : (int)st->fss;
}

static void draw_fifo_wave(
    uint8_t* framebuffer, int y, const ui_axis_state_t* st
) {
    EpdRect box = {
        .x = UI_MARGIN, .y = y, .width = ui_content_width(), .height = UI_FIFO_WAVE_H,
    };
    ui_draw_round_rect(framebuffer, box, 8, UI_GRAY_BLACK);
    if (st->fifo_n < 2) {
        ui_text_vc(
            framebuffer, box.x + box.width / 2, box.y + box.height / 2,
            UI_PX_CAPTION, "点读取画出三轴 Tap Read to plot", EPD_DRAW_ALIGN_CENTER, false
        );
        return;
    }
    int mid = box.y + box.height / 2;
    int half = box.height / 2 - 8;
    int span = box.width - 16;
    epd_draw_line(box.x + 8, mid, box.x + box.width - 8, mid, UI_GRAY_LIGHT, framebuffer);
    for (int axis = 0; axis < 3; axis++) {
        int prev_x = 0;
        int prev_y = 0;
        for (int i = 0; i < st->fifo_n; i++) {
            const sc7a20h_sample_t* s = &st->fifo_buf[i];
            int v = axis == 0 ? s->x_mg : axis == 1 ? s->y_mg : s->z_mg;
            int px = box.x + 8 + i * span / (st->fifo_n - 1);
            int py = mid - v * half / UI_SENSOR_RANGE_MG;
            if (py < box.y + 4) py = box.y + 4;
            if (py > box.y + box.height - 4) py = box.y + box.height - 4;
            if (i > 0) {
                epd_draw_line(prev_x, prev_y, px, py, UI_GRAY_BLACK, framebuffer);
            }
            prev_x = px;
            prev_y = py;
        }
    }
}

static void draw_axis_fifo(uint8_t* framebuffer, const ui_axis_state_t* st) {
    char n[56];
    int y = ui_draw_section(
        framebuffer, UI_CONTENT_TOP, "芯片先缓存再读出 Buffer, then read"
    );
    y = ui_draw_row(framebuffer, y, "模式 Mode", ui_axis_fifo_mode_name(st->fifo_mode));
    snprintf(n, sizeof(n), "%u samples", st->fifo_wtm);
    y = ui_draw_row(framebuffer, y, "水位 Watermark", n);
    snprintf(n, sizeof(n), "%d/32", axis_fifo_level(&st->fifo_st));
    y = ui_draw_row2(
        framebuffer, y, "缓存 Stored", n, "精度 Bits",
        st->fifo_8bit ? "8-bit" : "12-bit"
    );
    {
        const bool full = st->fifo_st.overrun && st->fifo_mode != SC7A20H_FIFO_STREAM;
        const char* flags = st->fifo_st.empty ? "空 Empty"
            : full && st->fifo_st.wtm ? "有数据 · 到线 · 已满"
            : full ? "有数据 · 已满"
            : st->fifo_st.wtm ? "有数据 · 到线"
            : "有数据 Data";
        y = ui_draw_row(framebuffer, y, "状态 Status", flags);
    }
    draw_fifo_wave(framebuffer, y + 8, st);

    static const char* const labels[] = {
        "换方式 Mode", "少攒 Wtm-", "多攒 Wtm+",
        "精度 Bits", "读取 Read", "清空 Clear",
    };
    for (int i = 0; i < 6; i++) {
        axis_draw_btn(
            framebuffer, axis_grid(i % 3, 3, i / 3, UI_AXIS_BTN2_Y), labels[i], false
        );
    }
}

static void draw_axis_diag(uint8_t* framebuffer, const ui_axis_state_t* st) {
    char v[56];
    int y = ui_draw_section(
        framebuffer, UI_CONTENT_TOP, "内置自测，请放平 Built-in ST, keep flat"
    );
    if (st->busy) {
        y = ui_draw_row(framebuffer, y, "状态 Status", "采样中 Sampling");
    } else if (st->st_valid) {
        snprintf(
            v, sizeof(v), "%d mg　%s", st->st.dx_mg, st->st.pass_x ? "正常 Pass" : "偏差 Fail"
        );
        y = ui_draw_row(framebuffer, y, "X 偏移 Delta", v);
        snprintf(
            v, sizeof(v), "%d mg　%s", st->st.dy_mg, st->st.pass_y ? "正常 Pass" : "偏差 Fail"
        );
        y = ui_draw_row(framebuffer, y, "Y 偏移 Delta", v);
        snprintf(
            v, sizeof(v), "%d mg　%s", st->st.dz_mg, st->st.pass_z ? "正常 Pass" : "偏差 Fail"
        );
        y = ui_draw_row(framebuffer, y, "Z 偏移 Delta", v);
    } else {
        y = ui_draw_row(framebuffer, y, "状态 Status", "未测 Idle　典型 320/320/430");
    }

    y = ui_draw_section(framebuffer, y + UI_SECTION_GAP, "静放噪声 Noise at rest");
    if (st->stats_valid) {
        snprintf(
            v, sizeof(v), "均 M %d　峰 P %d　散 S %u",
            st->stats.mean_x, st->stats.pp_x, st->stats.std_x
        );
        y = ui_draw_row(framebuffer, y, "X", v);
        snprintf(
            v, sizeof(v), "均 M %d　峰 P %d　散 S %u",
            st->stats.mean_y, st->stats.pp_y, st->stats.std_y
        );
        y = ui_draw_row(framebuffer, y, "Y", v);
        snprintf(
            v, sizeof(v), "均 M %d　峰 P %d　散 S %u",
            st->stats.mean_z, st->stats.pp_z, st->stats.std_z
        );
        y = ui_draw_row(framebuffer, y, "Z", v);
        snprintf(
            v, sizeof(v), "%u.%u Hz",
            st->stats.odr_x10 / 10, st->stats.odr_x10 % 10
        );
        y = ui_draw_row(framebuffer, y, "实测速率 Measured", v);
    } else {
        y = ui_draw_row(framebuffer, y, "状态 Status", "未采集 Idle");
    }

    y = ui_draw_section(framebuffer, y + UI_SECTION_GAP, "传感器型号 / IMU IC：SC7A20H");
    {
        const bool ok =
            st->whoami == SC7A20H_WHO_AM_I_VAL && st->version == SC7A20H_VERSION_VAL;
        if (st->whoami == 0 && st->version == 0) {
            snprintf(v, sizeof(v), "未读 Unread");
        } else {
            snprintf(
                v, sizeof(v), "%02X / %02X　%s", st->whoami, st->version,
                ok ? "SC7A20H" : "不符 Mismatch"
            );
        }
        y = ui_draw_row(framebuffer, y, "WHO / VER", v);
    }
    if (st->dump_valid) {
        snprintf(
            v, sizeof(v),
            "%02X %02X %02X %02X %02X %02X %02X %02X",
            st->dump[0], st->dump[1], st->dump[2], st->dump[3],
            st->dump[4], st->dump[5], st->dump[6], st->dump[7]
        );
        y = ui_draw_row(framebuffer, y, "控制字 CTRL", v);
        snprintf(
            v, sizeof(v),
            "%02X %02X %02X %02X %02X %02X %02X %02X",
            st->dump[8], st->dump[9], st->dump[10], st->dump[11],
            st->dump[12], st->dump[13], st->dump[14], st->dump[15]
        );
        ui_draw_row(framebuffer, y, "状态字 STAT", v);
    }

    static const char* const labels[] = {
        "自测 Test", "噪声 Noise", "寄存器 Dump", "复位 Reset",
    };
    for (int i = 0; i < 4; i++) {
        axis_draw_btn(framebuffer, axis_grid(i, 4, 0, UI_AXIS_BTN_Y), labels[i], false);
    }
}

static void draw_axis_page(
    uint8_t* framebuffer, const ui_axis_state_t* state, const char* title,
    const int* views, int count
) {
    char sub[80];
    ui_clear_page(framebuffer);
    static const char* const hints[UI_AXIS_VIEW_COUNT] = {
        "启用监听后晃动 Enable listen, then tilt",
        "速率、量程与滤波 ODR, range, filters",
        "轻敲机身看单击双击 Tap for single or double",
        "翻转设备查看朝向 Flip to see orientation",
        "失重、晃动与中断脚 Fall, shake, INT1",
        "最多缓存 32 组 Buffer up to 32 samples",
        "放平后测芯片与噪声 Flat: self-test and noise",
    };
    if (state->busy) {
        snprintf(sub, sizeof(sub), "正在采样，请放平 Sampling, keep still");
    } else if (state->last_msg[0]) {
        snprintf(sub, sizeof(sub), "%s", state->last_msg);
    } else {
        int v = state->view;
        if (v < 0 || v >= UI_AXIS_VIEW_COUNT) v = 0;
        snprintf(sub, sizeof(sub), "%s", hints[v]);
    }
    ui_draw_header(framebuffer, title, sub);

    switch (state->view) {
        case UI_AXIS_VIEW_CFG: draw_axis_cfg(framebuffer, state); break;
        case UI_AXIS_VIEW_TAP: draw_axis_tap(framebuffer, state); break;
        case UI_AXIS_VIEW_ORI: draw_axis_ori(framebuffer, state); break;
        case UI_AXIS_VIEW_EVT: draw_axis_evt(framebuffer, state); break;
        case UI_AXIS_VIEW_FIFO: draw_axis_fifo(framebuffer, state); break;
        case UI_AXIS_VIEW_DIAG: draw_axis_diag(framebuffer, state); break;
        default: draw_axis_live(framebuffer, state); break;
    }
    draw_axis_tabs(framebuffer, state->view, views, count);
    ui_draw_menu_handle(framebuffer, false);
}

typedef struct {
    int cols;
    int rows;
    int band;
    const int* ids;
} axis_hit_band_t;

static int axis_hit_bands(
    uint16_t x, uint16_t y, const axis_geom_t* g,
    const axis_hit_band_t* bands, int n
) {
    for (int i = 0; i < n; i++) {
        int hit = ui_grid_hit(
            x, y, bands[i].cols, bands[i].rows,
            axis_band_y(g, bands[i].band), UI_BTN_H, bands[i].ids
        );
        if (hit >= 0) return hit;
    }
    return UI_AXIS_HIT_NONE;
}

static int axis_hit_test(
    uint16_t x, uint16_t y, int view, const int* views, int count
) {
    int tab = ui_bar_hit(x, y, count);
    if (tab >= 0) return UI_AXIS_HIT_TAB0 + views[tab];

    const axis_geom_t g = axis_geom();
    switch (view) {
        case UI_AXIS_VIEW_LIVE: {
            static const int ids[] = { UI_AXIS_HIT_LIVE_HOLD, UI_AXIS_HIT_LIVE_ZERO };
            static const axis_hit_band_t bands[] = { { 2, 1, 1, ids } };
            return axis_hit_bands(x, y, &g, bands, 1);
        }
        case UI_AXIS_VIEW_CFG: {
            static const int ids[] = {
                UI_AXIS_HIT_CFG_ODR_DEC, UI_AXIS_HIT_CFG_ODR_INC,
                UI_AXIS_HIT_CFG_FS_DEC, UI_AXIS_HIT_CFG_FS_INC,
                UI_AXIS_HIT_CFG_MODE, UI_AXIS_HIT_CFG_OSR,
                UI_AXIS_HIT_CFG_DLPF, UI_AXIS_HIT_CFG_HPF,
            };
            static const axis_hit_band_t bands[] = {
                { 4, 1, 0, ids }, { 4, 1, 1, ids + 4 },
            };
            return axis_hit_bands(x, y, &g, bands, 2);
        }
        case UI_AXIS_VIEW_TAP: {
            static const int ids[] = {
                UI_AXIS_HIT_TAP_ARM, UI_AXIS_HIT_TAP_AXIS,
                UI_AXIS_HIT_TAP_THS_DEC, UI_AXIS_HIT_TAP_THS_INC,
                UI_AXIS_HIT_TAP_CLR,
            };
            static const axis_hit_band_t bands[] = {
                { 2, 1, 0, ids }, { 3, 1, 1, ids + 2 },
            };
            return axis_hit_bands(x, y, &g, bands, 2);
        }
        case UI_AXIS_VIEW_ORI: {
            static const int ids[] = {
                UI_AXIS_HIT_ORI_ARM, UI_AXIS_HIT_ORI_4D, UI_AXIS_HIT_ORI_THS_DEC,
                UI_AXIS_HIT_ORI_THS_INC, UI_AXIS_HIT_ORI_DUR_DEC,
                UI_AXIS_HIT_ORI_DUR_INC,
            };
            static const axis_hit_band_t bands[] = {
                { 2, 1, 0, ids }, { 4, 1, 1, ids + 2 },
            };
            return axis_hit_bands(x, y, &g, bands, 2);
        }
        case UI_AXIS_VIEW_EVT: {
            static const int ids[] = {
                UI_AXIS_HIT_EVT_FF, UI_AXIS_HIT_EVT_ACT,
                UI_AXIS_HIT_EVT_ROUTE, UI_AXIS_HIT_EVT_CLR,
            };
            static const axis_hit_band_t bands[] = {
                { 2, 1, 0, ids }, { 2, 1, 1, ids + 2 },
            };
            return axis_hit_bands(x, y, &g, bands, 2);
        }
        case UI_AXIS_VIEW_FIFO: {
            static const int ids[] = {
                UI_AXIS_HIT_FIFO_MODE, UI_AXIS_HIT_FIFO_WTM_DEC,
                UI_AXIS_HIT_FIFO_WTM_INC, UI_AXIS_HIT_FIFO_BITS,
                UI_AXIS_HIT_FIFO_READ, UI_AXIS_HIT_FIFO_CLR,
            };
            static const axis_hit_band_t bands[] = { { 3, 2, 0, ids } };
            return axis_hit_bands(x, y, &g, bands, 1);
        }
        case UI_AXIS_VIEW_DIAG: {
            static const int ids[] = {
                UI_AXIS_HIT_DIAG_ST, UI_AXIS_HIT_DIAG_STATS,
                UI_AXIS_HIT_DIAG_DUMP, UI_AXIS_HIT_DIAG_RESET,
            };
            static const axis_hit_band_t bands[] = { { 4, 1, 1, ids } };
            return axis_hit_bands(x, y, &g, bands, 1);
        }
        default:
            break;
    }
    return UI_AXIS_HIT_NONE;
}

static EpdRect axis_refresh_area(void) {
    return axis_geom().refresh;
}

static void axis_note(const char* msg) {
    snprintf(s_axis_ui.last_msg, sizeof(s_axis_ui.last_msg), "%s", msg);
}

static void axis_aoi_off(sc7a20h_aoi_t id) {
    sc7a20h_aoi_cfg_t off = { 0 };
    sc7a20h_aoi_config(s_acc, id, &off);
}

static void axis_ori_apply(void) {
    if (!s_axis_ui.ori_armed) return;
    sc7a20h_orientation_arm(
        s_acc, s_axis_ui.ori_4d, sc7a20h_ths_mg(s_acc, s_axis_ui.ori_ths),
        s_axis_ui.ori_dur
    );
}

static bool axis_live_follow(void) {
    return s_axis_ui.view == UI_AXIS_VIEW_LIVE && !s_axis_ui.hold;
}

// 跟手刷新会占住主循环，手指在屏上时先别开扫，不然底栏点不中。/ Live refresh holds the loop; do not start a scan while a finger is down, or the bar misses taps.
static bool axis_finger_down(app_ctx_t* ctx) {
    if (ctx->touch != NULL && ctx->touch->touched) return true;
    if (ctx->tp == NULL) return false;
    cst836u_touch_t now = { 0 };
    return cst836u_read(ctx->tp, &now) == ESP_OK && now.touched;
}

// 读数页没跟手时关掉 ODR；切到别的子页或开始跟手再上电。/ Drop ODR when the live page is not tracking; power up again on another view or when tracking starts.
static void axis_sync_power(sc7a20h_handle_t acc) {
    if (acc == NULL) return;
    if (axis_live_follow() || s_axis_ui.view != UI_AXIS_VIEW_LIVE) {
        read_pico_sensor_wake(acc);
    } else {
        read_pico_sensor_sleep(acc);
    }
}

static uint8_t axis_click_mask(void) {
    if (s_axis_ui.click_axis == 0) return SC7A20H_AXIS_Z;
    if (s_axis_ui.click_axis == 1) return SC7A20H_AXIS_X | SC7A20H_AXIS_Y;
    return SC7A20H_AXIS_XYZ;
}

// 手册敲击窗口按 200/400Hz 设计；12.5Hz 几乎采不到一次轻敲。/ The tap window in the datasheet is for 200/400Hz; 12.5Hz almost never catches a light tap.
static void axis_apply_click(void) {
    sc7a20h_sensor_config_t cfg = s_axis_ui.cfg;
    if (cfg.odr < SC7A20H_ODR_200) {
        cfg.odr = SC7A20H_ODR_200;
        if (sc7a20h_apply_config(s_acc, &cfg) == ESP_OK) {
            s_axis_ui.cfg = cfg;
        }
    }
    sc7a20h_click_config(s_acc, axis_click_mask(), s_axis_ui.click_ths);
}

static void axis_click_stop(void) {
    sc7a20h_click_config(s_acc, 0, s_axis_ui.click_ths);
}

static uint8_t axis_next_route(uint8_t current) {
    static const uint8_t routes[] = {
        0,
        SC7A20H_INT1_CLICK,
        SC7A20H_INT1_AOI1,
        SC7A20H_INT1_AOI2,
        SC7A20H_INT1_DRDY,
        SC7A20H_INT1_WTM,
    };
    for (size_t i = 0; i < sizeof(routes); i++) {
        if (routes[i] == current) {
            return routes[(i + 1) % sizeof(routes)];
        }
    }
    return routes[1];
}

static void axis_apply_fifo(void) {
    sc7a20h_fifo_cfg_t cfg = {
        .mode = s_axis_ui.fifo_mode,
        .watermark = s_axis_ui.fifo_wtm,
        .bit8 = s_axis_ui.fifo_8bit,
    };
    sc7a20h_fifo_config(s_acc, &cfg);
    sc7a20h_fifo_status(s_acc, &s_axis_ui.fifo_st);
}

// 进波形页或点读取时：旁路改成存满即停。Stream 32 组就会置 OVER。/ Entering FIFO or tapping Read: leave bypass for stop-when-full. Stream sets OVER after 32 samples.
static void axis_fifo_arm(void) {
    if (s_axis_ui.fifo_mode == SC7A20H_FIFO_BYPASS) {
        s_axis_ui.fifo_mode = SC7A20H_FIFO_MODE;
    }
    axis_apply_fifo();
}

// 自测/噪声读 0x28；FIFO 开着时 ZYXOR 会一直亮，先旁路，记住原来的方式。/ Self-test/noise reads 0x28; with FIFO on, ZYXOR stays set. Bypass first and remember the previous mode.
static void axis_fifo_pause(void) {
    sc7a20h_fifo_mode_t keep = s_axis_ui.fifo_mode;
    s_axis_ui.fifo_mode = SC7A20H_FIFO_BYPASS;
    axis_apply_fifo();
    s_axis_ui.fifo_mode = keep;
}

static bool axis_fifo_wait(int need) {
    if (need < 2) need = 2;
    if (need > SC7A20H_FIFO_MAX) need = SC7A20H_FIFO_MAX;
    uint16_t hz = sc7a20h_odr_hz(s_axis_ui.cfg.odr);
    if (hz < 1) hz = 13;
    int timeout_ms = need * 1000 / (int)hz + 80;
    if (timeout_ms > 3500) timeout_ms = 3500;
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    sc7a20h_fifo_status_t st;
    while (esp_timer_get_time() < deadline) {
        if (sc7a20h_fifo_status(s_acc, &st) == ESP_OK) {
            s_axis_ui.fifo_st = st;
            if (axis_fifo_level(&st) >= need) return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return axis_fifo_level(&s_axis_ui.fifo_st) >= 2;
}

static void axis_select_view(int view) {
    s_axis_ui.view = view;
    s_axis_ui.last_msg[0] = '\0';
    if (view == UI_AXIS_VIEW_LIVE) {
        read_pico_sensor_wake(s_acc);
        read_pico_accel_read(s_acc, &s_axis_ui.sample);
    } else if (view == UI_AXIS_VIEW_FIFO) {
        axis_fifo_arm();
    } else if (view == UI_AXIS_VIEW_DIAG) {
        axis_fifo_pause();
        sc7a20h_version(s_acc, &s_axis_ui.whoami, &s_axis_ui.version);
    }
    axis_sync_power(s_acc);
}

static void axis_handle_hit(int hit) {
    if (hit >= UI_AXIS_HIT_TAB0 && hit < UI_AXIS_HIT_TAB0 + UI_AXIS_VIEW_COUNT) {
        axis_select_view(hit - UI_AXIS_HIT_TAB0);
        return;
    }

    sc7a20h_sensor_config_t cfg = s_axis_ui.cfg;
    switch (hit) {
        case UI_AXIS_HIT_LIVE_HOLD:
            s_axis_ui.hold = !s_axis_ui.hold;
            axis_sync_power(s_acc);
            axis_note(s_axis_ui.hold ? "已停止监听 Listen stopped" : "已启用监听 Listen enabled");
            break;
        case UI_AXIS_HIT_LIVE_ZERO:
            s_axis_ui.zeroed = !s_axis_ui.zeroed;
            if (s_axis_ui.zeroed) {
                s_axis_ui.zero_x = s_axis_ui.sample.x_mg;
                s_axis_ui.zero_y = s_axis_ui.sample.y_mg;
                s_axis_ui.zero_z = s_axis_ui.sample.z_mg;
            }
            axis_note(s_axis_ui.zeroed ? "已设为零点 Zero set" : "显示绝对加速度 Absolute");
            break;
        case UI_AXIS_HIT_CFG_ODR_DEC:
            if (cfg.odr > SC7A20H_ODR_12_5) cfg.odr = (sc7a20h_odr_t)(cfg.odr - 1);
            goto apply_cfg;
        case UI_AXIS_HIT_CFG_ODR_INC:
            if (cfg.odr < SC7A20H_ODR_800) cfg.odr = (sc7a20h_odr_t)(cfg.odr + 1);
            goto apply_cfg;
        case UI_AXIS_HIT_CFG_FS_DEC:
            if (cfg.fs > SC7A20H_FS_2G) cfg.fs = (sc7a20h_fs_t)(cfg.fs - 1);
            goto apply_cfg;
        case UI_AXIS_HIT_CFG_FS_INC:
            if (cfg.fs < SC7A20H_FS_16G) cfg.fs = (sc7a20h_fs_t)(cfg.fs + 1);
            goto apply_cfg;
        case UI_AXIS_HIT_CFG_MODE:
            cfg.mode = (sc7a20h_mode_t)((cfg.mode + 1) & 3);
            goto apply_cfg;
        case UI_AXIS_HIT_CFG_OSR:
            cfg.osr = (sc7a20h_osr_t)((cfg.osr + 1) % (SC7A20H_OSR_32 + 1));
            goto apply_cfg;
        case UI_AXIS_HIT_CFG_DLPF:
            cfg.dlpf = (sc7a20h_dlpf_t)((cfg.dlpf + 1) % (SC7A20H_DLPF_STRONG + 1));
            goto apply_cfg;
        case UI_AXIS_HIT_CFG_HPF:
            cfg.hpf = (sc7a20h_hpf_t)((cfg.hpf + 1) % (SC7A20H_HPF_FAST + 1));
            goto apply_cfg;
        apply_cfg:
            if (sc7a20h_apply_config(s_acc, &cfg) == ESP_OK) {
                s_axis_ui.cfg = cfg;
                s_axis_ui.measured_odr_x10 = 0;
                axis_note("参数已写入 Config written");
            } else {
                axis_note("写入失败 Write failed");
            }
            break;
        case UI_AXIS_HIT_TAP_ARM:
            s_axis_ui.click_armed = !s_axis_ui.click_armed;
            if (s_axis_ui.click_armed) {
                axis_apply_click();
                axis_note("可以敲击机身 Ready to tap");
            } else {
                axis_click_stop();
                axis_note("已停止监听 Listen stopped");
            }
            break;
        case UI_AXIS_HIT_TAP_AXIS:
            s_axis_ui.click_axis = (uint8_t)((s_axis_ui.click_axis + 1) % 3);
            if (s_axis_ui.click_armed) axis_apply_click();
            break;
        case UI_AXIS_HIT_TAP_THS_DEC:
            if (s_axis_ui.click_ths > 0) {
                s_axis_ui.click_ths = (sc7a20h_click_ths_t)(s_axis_ui.click_ths - 1);
            }
            if (s_axis_ui.click_armed) axis_apply_click();
            break;
        case UI_AXIS_HIT_TAP_THS_INC:
            if (s_axis_ui.click_ths < SC7A20H_CLICK_THS_MAX) {
                s_axis_ui.click_ths = (sc7a20h_click_ths_t)(s_axis_ui.click_ths + 1);
            }
            if (s_axis_ui.click_armed) axis_apply_click();
            break;
        case UI_AXIS_HIT_TAP_CLR:
            s_axis_ui.click_count = 0;
            s_axis_ui.click_last = 0;
            s_axis_ui.click_src = 0;
            axis_note("次数已清 Count cleared");
            break;
        case UI_AXIS_HIT_ORI_ARM:
            s_axis_ui.ori_armed = !s_axis_ui.ori_armed;
            if (s_axis_ui.ori_armed) {
                axis_ori_apply();
                axis_note("翻转设备看朝向 Flip the device");
            } else {
                axis_aoi_off(SC7A20H_AOI1);
                axis_note("已停止监听 Listen stopped");
            }
            break;
        case UI_AXIS_HIT_ORI_4D:
            s_axis_ui.ori_4d = !s_axis_ui.ori_4d;
            axis_ori_apply();
            break;
        case UI_AXIS_HIT_ORI_THS_DEC:
            if (s_axis_ui.ori_ths > 1) s_axis_ui.ori_ths--;
            goto ori_rearm;
        case UI_AXIS_HIT_ORI_THS_INC:
            if (s_axis_ui.ori_ths < 80) s_axis_ui.ori_ths++;
            goto ori_rearm;
        case UI_AXIS_HIT_ORI_DUR_DEC:
            if (s_axis_ui.ori_dur > 0) s_axis_ui.ori_dur--;
            goto ori_rearm;
        case UI_AXIS_HIT_ORI_DUR_INC:
            if (s_axis_ui.ori_dur < 20) s_axis_ui.ori_dur++;
            goto ori_rearm;
        ori_rearm:
            axis_ori_apply();
            break;
        case UI_AXIS_HIT_EVT_FF:
            if (s_axis_ui.ff_armed) {
                s_axis_ui.ff_armed = false;
                axis_aoi_off(SC7A20H_AOI2);
                axis_note("已停止监听 Listen stopped");
            } else {
                s_axis_ui.ff_armed = true;
                s_axis_ui.act_armed = false;
                sc7a20h_freefall_config(s_acc, 350, 3);
                axis_note("快速下移或轻抛 Drop or toss");
            }
            break;
        case UI_AXIS_HIT_EVT_ACT:
            if (s_axis_ui.act_armed) {
                s_axis_ui.act_armed = false;
                axis_aoi_off(SC7A20H_AOI2);
                axis_note("已停止监听 Listen stopped");
            } else {
                s_axis_ui.act_armed = true;
                s_axis_ui.ff_armed = false;
                sc7a20h_activity_config(s_acc, 160, 2);
                axis_note("请晃动设备 Shake the device");
            }
            break;
        case UI_AXIS_HIT_EVT_ROUTE:
            s_axis_ui.int_route = axis_next_route(s_axis_ui.int_route);
            sc7a20h_int_route(s_acc, s_axis_ui.int_route);
            axis_note(ui_axis_int_route_name(s_axis_ui.int_route));
            break;
        case UI_AXIS_HIT_EVT_CLR:
            s_axis_ui.ff_count = 0;
            s_axis_ui.act_count = 0;
            axis_note("次数已清 Count cleared");
            break;
        case UI_AXIS_HIT_FIFO_MODE:
            s_axis_ui.fifo_mode = (sc7a20h_fifo_mode_t)((s_axis_ui.fifo_mode + 1) & 3);
            axis_apply_fifo();
            axis_note(ui_axis_fifo_mode_name(s_axis_ui.fifo_mode));
            break;
        case UI_AXIS_HIT_FIFO_WTM_DEC:
            if (s_axis_ui.fifo_wtm > 1) s_axis_ui.fifo_wtm--;
            axis_apply_fifo();
            break;
        case UI_AXIS_HIT_FIFO_WTM_INC:
            if (s_axis_ui.fifo_wtm < 31) s_axis_ui.fifo_wtm++;
            axis_apply_fifo();
            break;
        case UI_AXIS_HIT_FIFO_BITS:
            s_axis_ui.fifo_8bit = !s_axis_ui.fifo_8bit;
            axis_apply_fifo();
            axis_note(s_axis_ui.fifo_8bit ? "粗精度 8-bit" : "细精度 12-bit");
            break;
        case UI_AXIS_HIT_FIFO_READ: {
            axis_fifo_arm();
            if (axis_fifo_level(&s_axis_ui.fifo_st) < 2) {
                int need = s_axis_ui.fifo_wtm > 2 ? (int)s_axis_ui.fifo_wtm : 16;
                axis_fifo_wait(need);
            }
            size_t n = 0;
            sc7a20h_fifo_read(s_acc, s_axis_ui.fifo_buf, SC7A20H_FIFO_MAX, &n);
            s_axis_ui.fifo_n = (int)n;
            for (int i = 0; i < s_axis_ui.fifo_n; i++) {
                read_pico_accel_to_device(&s_axis_ui.fifo_buf[i]);
            }
            if (s_axis_ui.fifo_mode == SC7A20H_FIFO_MODE
                || s_axis_ui.fifo_mode == SC7A20H_FIFO_TRIGGER) {
                axis_fifo_pause();
                axis_apply_fifo();
            } else {
                sc7a20h_fifo_status(s_acc, &s_axis_ui.fifo_st);
            }
            axis_note(
                s_axis_ui.fifo_n < 2
                    ? "缓存还是空的 Buffer still empty"
                    : "已画出缓存数据 Buffer plotted"
            );
            break;
        }
        case UI_AXIS_HIT_FIFO_CLR:
            sc7a20h_fifo_clear(s_acc);
            s_axis_ui.fifo_n = 0;
            sc7a20h_fifo_status(s_acc, &s_axis_ui.fifo_st);
            axis_note("缓存已清空 Buffer cleared");
            break;
        case UI_AXIS_HIT_DIAG_ST:
            axis_fifo_pause();
            if (sc7a20h_self_test(s_acc, &s_axis_ui.st) == ESP_OK) {
                s_axis_ui.st_valid = true;
                axis_note(
                    s_axis_ui.st.pass_x && s_axis_ui.st.pass_y && s_axis_ui.st.pass_z
                        ? "芯片自测正常 Self-test pass"
                        : "自测偏差超范围 Self-test fail"
                );
            } else {
                axis_note("自测超时，无新样本");
            }
            break;
        case UI_AXIS_HIT_DIAG_STATS:
            axis_fifo_pause();
            if (sc7a20h_stats_collect(s_acc, 256, &s_axis_ui.stats) == ESP_OK) {
                s_axis_ui.stats_valid = true;
                s_axis_ui.measured_odr_x10 = s_axis_ui.stats.odr_x10;
                axis_note("噪声已采完 Noise done");
            } else {
                axis_note("噪声未采到 Noise unread");
            }
            break;
        case UI_AXIS_HIT_DIAG_DUMP:
            if (sc7a20h_dump_regs(s_acc, s_axis_ui.dump, SC7A20H_DUMP_LEN) == ESP_OK) {
                s_axis_ui.dump_valid = true;
                axis_note("寄存器已读出 Registers read");
            }
            sc7a20h_version(s_acc, &s_axis_ui.whoami, &s_axis_ui.version);
            break;
        case UI_AXIS_HIT_DIAG_RESET:
            if (sc7a20h_soft_reset(s_acc) == ESP_OK) {
                s_axis_ui.cfg = *sc7a20h_get_config(s_acc);
                s_axis_ui.click_armed = false;
                s_axis_ui.ori_armed = false;
                s_axis_ui.ff_armed = false;
                s_axis_ui.act_armed = false;
                s_axis_ui.fifo_mode = SC7A20H_FIFO_BYPASS;
                sc7a20h_version(s_acc, &s_axis_ui.whoami, &s_axis_ui.version);
                axis_note("芯片已复位 Chip reset");
            } else {
                axis_note("复位失败 Reset failed");
            }
            break;
        default:
            break;
    }
}

static bool view_in_tier(const axis_tier_t* tier, int view) {
    for (int i = 0; i < tier->count; i++) {
        if (tier->views[i] == view) return true;
    }
    return false;
}

static void enter_tier(app_ctx_t* ctx, const axis_tier_t* tier) {
    s_acc = ctx->acc;
    if (!view_in_tier(tier, s_axis_ui.view)) {
        s_axis_ui.view = tier->views[0];
        s_axis_ui.last_msg[0] = '\0';
    }
    if (ctx->sensor_ready) {
        read_pico_sensor_wake(s_acc);
        sc7a20h_int1_begin(s_acc);
        read_pico_accel_read(s_acc, &s_axis_ui.sample);
        s_axis_ui.cfg = *sc7a20h_get_config(s_acc);
        axis_sync_power(s_acc);
        if (s_axis_ui.view == UI_AXIS_VIEW_FIFO) axis_fifo_arm();
        sc7a20h_version(s_acc, &s_axis_ui.whoami, &s_axis_ui.version);
    }
    s_drawn_sample = s_axis_ui.sample;
    s_settled = true;
    s_last_update_ms = ctx->now_ms;
}

static void exit_tier(app_ctx_t* ctx) {
    // 离页就让芯片睡下去；两页互切时主循环会立刻再 wake 一次。/ Sleep the chip on leave; switching between the two pages wakes it again immediately.
    if (ctx->sensor_ready) read_pico_sensor_sleep(ctx->acc);
}

static void render_tier(app_ctx_t* ctx, uint8_t* fb, const axis_tier_t* tier) {
    if (ctx->sensor_ready && s_axis_ui.view == UI_AXIS_VIEW_LIVE && !s_axis_ui.hold) {
        read_pico_accel_read(ctx->acc, &s_axis_ui.sample);
    }
    draw_axis_page(fb, &s_axis_ui, tier->title, tier->views, tier->count);
}

static app_redraw_t touch_tier(
    app_ctx_t* ctx, const cst836u_touch_t* touch, const axis_tier_t* tier
) {
    s_acc = ctx->acc;
    int hit = axis_hit_test(
        touch->x, touch->y, s_axis_ui.view, tier->views, tier->count
    );
    if (hit < 0) return APP_REDRAW_NONE;

    // 自检和噪声统计要采几百个点，先把「正在采样」推上屏再动手。/ Self-test and noise stats take hundreds of samples; present "sampling" first, then start.
    bool blocking = hit == UI_AXIS_HIT_DIAG_ST || hit == UI_AXIS_HIT_DIAG_STATS;
    if (blocking) {
        s_axis_ui.busy = true;
        draw_axis_page(ctx->fb, &s_axis_ui, tier->title, tier->views, tier->count);
        guard_draw_result(ctx->hl, update_display_mode(ctx->hl, APP_PAGE_REFRESH_MODE));
    }
    axis_handle_hit(hit);
    s_axis_ui.busy = false;
    s_last_update_ms = ctx->now_ms;
    s_drawn_sample = s_axis_ui.sample;
    s_settled = false;
    ESP_LOGI(TAG, "AXIS hit %d view %d", hit, s_axis_ui.view);
    return APP_REDRAW_PAGE;
}

// 实时读数用补全到全部灰阶迁移的厂家 DU，停住之后再用 GC16 定稿。/ Live digits use vendor DU that finishes every gray transition; settle with GC16 after motion stops.
static app_redraw_t tick_tier(app_ctx_t* ctx, const axis_tier_t* tier) {
    if (!ctx->sensor_ready) return APP_REDRAW_NONE;
    const bool live = s_axis_ui.view == UI_AXIS_VIEW_LIVE;
    if (live && axis_finger_down(ctx)) return APP_REDRAW_NONE;
    if (live && s_axis_ui.hold) return APP_REDRAW_NONE;
    const int64_t interval = live ? AXIS_UPDATE_INTERVAL_MS : AXIS_EVENT_INTERVAL_MS;
    if (ctx->now_ms - s_last_update_ms < interval) return APP_REDRAW_NONE;
    s_last_update_ms = ctx->now_ms;

    if (live) {
        if (s_axis_ui.hold) return APP_REDRAW_NONE;
        sc7a20h_sample_t sample;
        if (read_pico_accel_read(ctx->acc, &sample) != ESP_OK) return APP_REDRAW_NONE;
        bool moving = abs(sample.x_mg - s_drawn_sample.x_mg) > AXIS_STILL_MG
            || abs(sample.y_mg - s_drawn_sample.y_mg) > AXIS_STILL_MG
            || abs(sample.z_mg - s_drawn_sample.z_mg) > AXIS_STILL_MG;
        if (!moving && s_settled) return APP_REDRAW_NONE;
        // 跟手只走 DU。停住再 GC16 会占几百毫秒，底栏按键点不进去。/ Tracking is DU only. A settle GC16 takes hundreds of ms and the bar cannot be hit.
        if (!moving) {
            s_settled = true;
            return APP_REDRAW_NONE;
        }

        s_axis_ui.sample = sample;
        int64_t ui_started_us = esp_timer_get_time();
        draw_axis_page(ctx->fb, &s_axis_ui, tier->title, tier->views, tier->count);
        int32_t ui_ms = (int32_t)((esp_timer_get_time() - ui_started_us) / 1000);
        if (axis_finger_down(ctx)) return APP_REDRAW_NONE;

        int64_t upd_started_us = esp_timer_get_time();
        enum EpdDrawError result = update_display_area_with(
            ctx->hl, &E0470_FOLLOW_WAVEFORM, APP_DYNAMIC_REFRESH_MODE,
            axis_refresh_area()
        );
        int32_t upd_ms = (int32_t)((esp_timer_get_time() - upd_started_us) / 1000);
        guard_draw_result(ctx->hl, result);
        s_drawn_sample = sample;
        s_settled = false;
        ESP_LOGI(
            TAG, "Axis %d,%d,%d ui %dms upd %dms follow DU",
            sample.x_mg, sample.y_mg, sample.z_mg, ui_ms, upd_ms
        );
        return APP_REDRAW_DONE;
    }

    sc7a20h_events_t ev;
    if (sc7a20h_read_events(ctx->acc, &ev) != ESP_OK) return APP_REDRAW_NONE;
    bool changed = false;
    uint8_t clicks = ev.click_src & 0x0F;
    // SRC 读完即清。两次单击都是 1，不能再拿「和上次相同」丢掉。/ SRC clears on read. Two single taps are both 1; do not drop the second as "same as last".
    if (s_axis_ui.click_armed && clicks != 0) {
        s_axis_ui.click_src = ev.click_src;
        s_axis_ui.click_last = clicks;
        s_axis_ui.click_count++;
        changed = true;
    }
    if (s_axis_ui.ori_armed && ev.aoi1_src != s_axis_ui.ori_src) {
        sc7a20h_aoi_src_t src;
        sc7a20h_aoi_decode(read_pico_accel_map_aoi(ev.aoi1_src), &src);
        s_axis_ui.ori_src = ev.aoi1_src;
        s_axis_ui.orient = sc7a20h_orientation(&src);
        changed = true;
    }
    if (s_axis_ui.ff_armed && (ev.aoi2_src & 0x40)
        && ev.aoi2_src != s_axis_ui.aoi2_src) {
        s_axis_ui.ff_count++;
        s_axis_ui.last_ff_ms = ctx->now_ms;
        changed = true;
    }
    if (s_axis_ui.act_armed && (ev.aoi2_src & 0x40)
        && ev.aoi2_src != s_axis_ui.aoi2_src) {
        s_axis_ui.act_count++;
        s_axis_ui.last_act_ms = ctx->now_ms;
        changed = true;
    }
    if (ev.aoi2_src != s_axis_ui.aoi2_src) {
        s_axis_ui.aoi2_src = ev.aoi2_src;
        changed = true;
    }
    if (ev.int1_count != s_axis_ui.int1_count || ev.int1_level != s_axis_ui.int1_level) {
        s_axis_ui.int1_count = ev.int1_count;
        s_axis_ui.int1_level = ev.int1_level;
        changed = true;
    }
    sc7a20h_fifo_status_t fst = {
        .raw = ev.fifo_src,
        .wtm = (ev.fifo_src & 0x80) != 0,
        .overrun = (ev.fifo_src & 0x40) != 0,
        .empty = (ev.fifo_src & 0x20) != 0,
        .fss = ev.fifo_src & 0x1F,
    };
    if (fst.raw != s_axis_ui.fifo_st.raw) {
        s_axis_ui.fifo_st = fst;
        changed = true;
    }
    if (!changed) return APP_REDRAW_NONE;
    draw_axis_page(ctx->fb, &s_axis_ui, tier->title, tier->views, tier->count);
    guard_draw_result(ctx->hl, update_display_mode(ctx->hl, APP_DYNAMIC_REFRESH_MODE));
    return APP_REDRAW_DONE;
}

static void demo_enter(app_ctx_t* ctx) { enter_tier(ctx, &s_demo_tier); }
static void demo_render(app_ctx_t* ctx, uint8_t* fb) { render_tier(ctx, fb, &s_demo_tier); }
static app_redraw_t demo_touch(app_ctx_t* ctx, const cst836u_touch_t* t) {
    return touch_tier(ctx, t, &s_demo_tier);
}
static app_redraw_t demo_tick(app_ctx_t* ctx) { return tick_tier(ctx, &s_demo_tier); }

static void lab_enter(app_ctx_t* ctx) { enter_tier(ctx, &s_lab_tier); }
static void lab_render(app_ctx_t* ctx, uint8_t* fb) { render_tier(ctx, fb, &s_lab_tier); }
static app_redraw_t lab_touch(app_ctx_t* ctx, const cst836u_touch_t* t) {
    return touch_tier(ctx, t, &s_lab_tier);
}
static app_redraw_t lab_tick(app_ctx_t* ctx) { return tick_tier(ctx, &s_lab_tier); }

const app_desc_t app_axis = {
    .title = AXIS_TITLE,
    .detail = "读数与事件 Readings & Events",
    .render = demo_render,
    .on_enter = demo_enter,
    .on_exit = exit_tier,
    .on_touch = demo_touch,
    .on_tick = demo_tick,
    .user = &s_demo_tier,
};

const app_desc_t app_axis_lab = {
    .title = AXIS_LAB_TITLE,
    .detail = "采样参数与自测 Config & Self-test",
    .render = lab_render,
    .on_enter = lab_enter,
    .on_exit = exit_tier,
    .on_touch = lab_touch,
    .on_tick = lab_tick,
    .user = &s_lab_tier,
};
