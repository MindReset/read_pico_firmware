/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * CW32L010 协议页。电池电压电量充电看「电源与电池」；本页只看协议状态、
 * 事件、配置和不会动面板高压的命令。
 *
 * CW32L010 protocol page. Voltage, SOC, and charge live on Power; this page
 * is protocol state, events, config, and commands that do not move panel HV.
 *
 * 冻结：四页 信息 / 事件 / 配置 / 动作；
 * 关机 / 复位 / 下载走主机路径并通知 PMU，不让 PMU 拉 EN/BOOT；
 * 不提供恢复出厂、硬复位、软睡；VCOM 只读；
 * 故障只显示低 16 位十六进制，不写原因；
 * 配置开关 2×2：来电开机 / 按键 / 充灯 / 低电通知；
 * KEY3 走主循环菜单把手。
 * Frozen: four views Info / Event / Config / Action; Off / Reset / Download
 * take the host path and notify the PMU, do not let the PMU pull EN/BOOT;
 * no factory reset, hard reset, or soft sleep; VCOM is read-only; faults show
 * the low 16 bits in hex with no reason text; config switches are 2×2:
 * AC-on / key / charge LED / low-SOC notify. KEY3 is the loop menu handle.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "app.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pmu_selftest.h"
#include "read_pico_pmu.h"
#include "read_pico_pmu_protocol.h"
#include "soc/rtc_cntl_reg.h"
#include "ui_kit.h"
#include "ui_menu.h"

#define TAG "app_pmu"

#define PMU_TITLE "电源管理协议 PMU"
#define PMU_POLL_MS 500
#define PMU_CHIP_H 56
#define PMU_CHIP_GAP 8
#define PMU_CHIP_COLS 4
#define PMU_TAB_PX 22
#define PMU_BTN_STRIDE (UI_BTN_H + UI_SECTION_GAP)
#define PMU_BAR_Y (UI_CONTENT_BOTTOM - UI_BTN_H)
#define PMU_CFG_Y (PMU_BAR_Y - UI_BTN_H - UI_GAP)

#define UI_PMU_VIEW_INFO 0
#define UI_PMU_VIEW_EVT 1
#define UI_PMU_VIEW_CFG 2
#define UI_PMU_VIEW_ACT 3
#define UI_PMU_VIEW_COUNT 4

#define UI_PMU_HIT_NONE (-1)
#define UI_PMU_HIT_TAB0 0
#define UI_PMU_HIT_READ 10
#define UI_PMU_HIT_OFF 11
#define UI_PMU_HIT_RST 12
#define UI_PMU_HIT_DL 13
#define UI_PMU_HIT_ACK 20
#define UI_PMU_HIT_EVT_CLR 21
#define UI_PMU_HIT_DIAG_CLR 22
#define UI_PMU_HIT_CFG_WAKE 30
#define UI_PMU_HIT_CFG_KEY 31
#define UI_PMU_HIT_CFG_LED 32
#define UI_PMU_HIT_CFG_SOC 33
#define UI_PMU_HIT_LED_OFF 40
#define UI_PMU_HIT_LED_RED 41
#define UI_PMU_HIT_LED_WHT 42
#define UI_PMU_HIT_LED_BOTH 43
#define UI_PMU_HIT_LED_CLR 44
#define UI_PMU_HIT_PING 45
#define UI_PMU_HIT_SAMPLE 46
#define UI_PMU_HIT_READY 47
#define UI_PMU_HIT_TIME_GET 48
#define UI_PMU_HIT_TIME_SYNC 49
#define UI_PMU_HIT_ALARM_OFF 50
#define UI_PMU_HIT_ALARM_10 51
#define UI_PMU_HIT_ALARM_60 52
#define UI_PMU_HIT_ALARM_GET 53

typedef struct {
    int view;
    char last_msg[56];
} ui_pmu_state_t;

typedef struct {
    int body_y;
    int info_status_y;
    int info_ident_y;
    int info_flags_y;
    int evt_latest_y;
    int evt_diag_y;
    int cfg_key_y;
    int cfg_to_y;
    int bar_y;
    int cfg_y;
    int act_led_y;
    int act_cmd_y;
    int act_tim_y;
    int act_al_y;
} pmu_geom_t;

typedef struct {
    int cols;
    int rows;
    int y;
    const int* ids;
} pmu_hit_band_t;

static const char* const k_tab_labels[UI_PMU_VIEW_COUNT] = {
    "信息 Info", "事件 Event", "配置 Config", "动作 Action",
};

static const char* const k_info_btn[] = {
    "读取 Read", "关机 Off", "复位 Reset", "下载 DL",
};
static const int k_info_ids[] = {
    UI_PMU_HIT_READ, UI_PMU_HIT_OFF, UI_PMU_HIT_RST, UI_PMU_HIT_DL,
};

static const char* const k_evt_btn[] = { "确认 Ack", "清事件 Clear", "清诊断 Diag" };
static const int k_evt_ids[] = {
    UI_PMU_HIT_ACK, UI_PMU_HIT_EVT_CLR, UI_PMU_HIT_DIAG_CLR,
};

static const char* const k_cfg_btn[] = {
    "来电开机 AC On", "按键 Key", "充灯 ChgLED", "低电通知 LowSOC",
};
static const int k_cfg_ids[] = {
    UI_PMU_HIT_CFG_WAKE, UI_PMU_HIT_CFG_KEY, UI_PMU_HIT_CFG_LED, UI_PMU_HIT_CFG_SOC,
};

static const char* const k_led_btn[] = {
    "关 Off", "红 Red", "白 White", "红白 Both", "清除 Clear",
};
static const int k_led_ids[] = {
    UI_PMU_HIT_LED_OFF, UI_PMU_HIT_LED_RED, UI_PMU_HIT_LED_WHT,
    UI_PMU_HIT_LED_BOTH, UI_PMU_HIT_LED_CLR,
};

static const char* const k_cmd_btn[] = { "连通 Ping", "采电池 Sample", "就绪 Ready" };
static const int k_cmd_ids[] = {
    UI_PMU_HIT_PING, UI_PMU_HIT_SAMPLE, UI_PMU_HIT_READY,
};

static const char* const k_tim_btn[] = { "读时间 Time", "对时 Sync", "读闹钟 Alarm" };
static const int k_tim_ids[] = {
    UI_PMU_HIT_TIME_GET, UI_PMU_HIT_TIME_SYNC, UI_PMU_HIT_ALARM_GET,
};

static const char* const k_al_btn[] = { "关闹钟 Off", "10 秒 10s", "60 秒 60s" };
static const int k_al_ids[] = {
    UI_PMU_HIT_ALARM_OFF, UI_PMU_HIT_ALARM_10, UI_PMU_HIT_ALARM_60,
};

static ui_pmu_state_t s_pmu_ui;
static int64_t s_last_poll_ms;

// 一段标题 + N 行键值后再留一节间距，给下一节或按钮带用。/ After a section title and N key-value rows, leave one section gap for the next section or button band.
static int pmu_after_rows(int y, int rows) {
    return y + UI_SEC_HEAD + rows * UI_ROW_H_SM + UI_SECTION_GAP;
}

static pmu_geom_t pmu_geom(void) {
    const int body = UI_CONTENT_TOP;
    const int info_status = pmu_after_rows(body, 1);
    const int info_ident = pmu_after_rows(info_status, 2);
    const int evt_latest = pmu_after_rows(body, 1);
    const int cfg_key = pmu_after_rows(body, 3);
    const int act_led = pmu_after_rows(body, 2);
    return (pmu_geom_t){
        .body_y = body,
        .info_status_y = info_status,
        .info_ident_y = info_ident,
        .info_flags_y = pmu_after_rows(info_ident, 2),
        .evt_latest_y = evt_latest,
        .evt_diag_y = pmu_after_rows(evt_latest, 2),
        .cfg_key_y = cfg_key,
        .cfg_to_y = pmu_after_rows(cfg_key, 3),
        .bar_y = PMU_BAR_Y,
        .cfg_y = PMU_CFG_Y,
        .act_led_y = act_led,
        .act_cmd_y = act_led + PMU_BTN_STRIDE,
        .act_tim_y = act_led + 2 * PMU_BTN_STRIDE,
        .act_al_y = act_led + 3 * PMU_BTN_STRIDE,
    };
}

static EpdRect pmu_grid(int col, int cols, int row, int y0) {
    return ui_grid_rect(col, cols, row, y0, UI_BTN_H);
}

// 中英并列比单行中文长，按钮用略小一档，避免窄格裁字。/ Bilingual labels are longer than Chinese-only; use a smaller button size so narrow cells do not clip.
static void pmu_draw_btn(uint8_t* framebuffer, EpdRect rect, const char* label, bool on) {
    ui_draw_choice_round_rect(framebuffer, rect, UI_BTN_RADIUS, on);
    ui_text_vc(
        framebuffer, rect.x + rect.width / 2, rect.y + rect.height / 2,
        UI_PX_LABEL_SM, label, EPD_DRAW_ALIGN_CENTER, false
    );
}

static void pmu_draw_grid(
    uint8_t* framebuffer, int cols, int rows, int y0,
    const char* const* labels, const bool* on
) {
    const int n = cols * rows;
    for (int i = 0; i < n; i++) {
        pmu_draw_btn(
            framebuffer, pmu_grid(i % cols, cols, i / cols, y0),
            labels[i], on != NULL && on[i]
        );
    }
}

static int pmu_hit_bands(
    uint16_t x, uint16_t y, const pmu_hit_band_t* bands, int n
) {
    for (int i = 0; i < n; i++) {
        int hit = ui_grid_hit(
            x, y, bands[i].cols, bands[i].rows, bands[i].y, UI_BTN_H, bands[i].ids
        );
        if (hit >= 0) return hit;
    }
    return UI_PMU_HIT_NONE;
}

static const char* power_text(uint8_t state) {
    switch (state) {
        case PMU_PWR_OFF: return "关机 Off";
        case PMU_PWR_POWERING_ON: return "上电中 Powering";
        case PMU_PWR_BOOT_WAIT: return "等启动 Boot";
        case PMU_PWR_RUNNING: return "运行中 Running";
        case PMU_PWR_SHUTDOWN_PENDING: return "关机中 Stopping";
        case PMU_PWR_RESETTING: return "复位中 Resetting";
        case PMU_PWR_DOWNLOAD_MODE: return "下载 Download";
        case PMU_PWR_FAULT: return "故障 Fault";
        case PMU_PWR_SOFT_SLEEP: return "软睡眠 Sleep";
        default: return "未知 Unknown";
    }
}

static const char* status_text(uint16_t status) {
    switch (status) {
        case PMU_STATUS_OK: return "成功 OK";
        case PMU_STATUS_ACCEPTED: return "已受理 Ack";
        case PMU_STATUS_BUSY: return "忙 Busy";
        case PMU_STATUS_BAD_CRC: return "校验错 CRC";
        case PMU_STATUS_BAD_MAGIC: return "魔数错 Magic";
        case PMU_STATUS_BAD_LENGTH: return "长度错 Len";
        case PMU_STATUS_UNSUPPORTED_VERSION: return "版本不支持 Ver";
        case PMU_STATUS_UNKNOWN_COMMAND: return "未知命令 Cmd";
        case PMU_STATUS_INVALID_ARGUMENT: return "参数错 Arg";
        case PMU_STATUS_SEQUENCE_CONFLICT: return "序号冲突 Seq";
        case PMU_STATUS_STALE_SESSION: return "会话过期 Sess";
        case PMU_STATUS_NOT_SUPPORTED: return "不支持 N/S";
        case PMU_STATUS_INVALID_STATE: return "状态错 State";
        case PMU_STATUS_NOT_ARMED: return "未就绪 N/A";
        case PMU_STATUS_TOKEN_EXPIRED: return "令牌过期 Tok";
        case PMU_STATUS_PERMISSION_DENIED: return "无权限 Deny";
        default: return "错误 Error";
    }
}

static const char* event_text(uint8_t type) {
    switch (type) {
        case PMU_EVT_KEY_DOWN: return "按下 Down";
        case PMU_EVT_KEY_UP: return "抬起 Up";
        case PMU_EVT_KEY_SHORT: return "短按 Short";
        case PMU_EVT_KEY_LONG: return "长按 Long";
        case PMU_EVT_KEY_FORCE_OFF: return "强制关机 Force";
        case PMU_EVT_CHARGE_STATE_CHANGED: return "充电变化 Charge";
        case PMU_EVT_BATTERY_SAMPLE_READY: return "电池采样 Sample";
        case PMU_EVT_BATTERY_LOW: return "电池低压 Low";
        case PMU_EVT_BATTERY_CRITICAL: return "电池危压 Crit";
        case PMU_EVT_BATTERY_SOC_LOW: return "电量低 SOC";
        case PMU_EVT_ALARM_FIRED: return "闹钟 Alarm";
        case PMU_EVT_HOST_STARTED: return "主机已起 Started";
        case PMU_EVT_HOST_READY: return "主机就绪 Ready";
        case PMU_EVT_SHUTDOWN_REQUESTED: return "请求关机 Off req";
        case PMU_EVT_HOST_RESET_PERFORMED: return "主机复位 Reset";
        case PMU_EVT_COMMAND_COMPLETED: return "命令完成 Done";
        case PMU_EVT_CONFIG_RECOVERED: return "配置恢复 Recovered";
        case PMU_EVT_CW_RESET: return "电源复位 PMU rst";
        case PMU_EVT_OVERFLOW: return "溢出 Overrun";
        default: return "事件 Event";
    }
}

static void fmt_uptime(char* out, size_t n, uint32_t ms) {
    unsigned long sec = (unsigned long)(ms / 1000);
    unsigned long h = sec / 3600;
    unsigned long m = (sec % 3600) / 60;
    unsigned long s = sec % 60;
    if (h > 0) {
        snprintf(out, n, "%lu h %lu m %lu s", h, m, s);
    } else if (m > 0) {
        snprintf(out, n, "%lu m %lu s", m, s);
    } else {
        snprintf(out, n, "%lu s", s);
    }
}

static const char* alarm_text(uint8_t mode) {
    switch (mode) {
        case 1: return "单次 Once";
        case 2: return "循环 Repeat";
        default: return "关 Off";
    }
}

static void pmu_wr_u32le(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void draw_pmu_tabs(uint8_t* framebuffer, int view) {
    for (int i = 0; i < UI_PMU_VIEW_COUNT; i++) {
        EpdRect rect = ui_bar_rect(i, UI_PMU_VIEW_COUNT);
        ui_draw_choice_round_rect(framebuffer, rect, UI_BTN_RADIUS, view == i);
        ui_text_vc(
            framebuffer, rect.x + rect.width / 2, rect.y + rect.height / 2,
            PMU_TAB_PX, k_tab_labels[i], EPD_DRAW_ALIGN_CENTER, false
        );
    }
}

static EpdRect pmu_chip_rect(int i, int y) {
    const int w = (ui_content_width() - (PMU_CHIP_COLS - 1) * PMU_CHIP_GAP) / PMU_CHIP_COLS;
    return (EpdRect){
        .x = UI_MARGIN + (i % PMU_CHIP_COLS) * (w + PMU_CHIP_GAP),
        .y = y + (i / PMU_CHIP_COLS) * (PMU_CHIP_H + PMU_CHIP_GAP),
        .width = w,
        .height = PMU_CHIP_H,
    };
}

static void draw_pmu_flags(uint8_t* framebuffer, int y, uint32_t flags) {
    static const struct {
        const char* label;
        uint32_t bit;
    } items[8] = {
        { "有效 Valid", PMU_STATUS_BATTERY_VALID },
        { "低压 Low", PMU_STATUS_BATTERY_LOW },
        { "危压 Crit", PMU_STATUS_BATTERY_CRITICAL },
        { "充电 Chg", PMU_STATUS_CHARGING_ACTIVE },
        { "供电 USB", PMU_STATUS_CHARGE_PIN_HIGH },
        { "按键 Key", PMU_STATUS_KEY_PRESSED },
        { "使能 EN", PMU_STATUS_HOST_EN_HIGH },
        { "启动 Boot", PMU_STATUS_HOST_BOOT_LOW },
    };
    for (int i = 0; i < 8; i++) {
        ui_draw_chip(
            framebuffer, pmu_chip_rect(i, y), items[i].label,
            (flags & items[i].bit) != 0
        );
    }
}

static void draw_pmu_info(uint8_t* framebuffer, const pmu_snapshot_t* s, const pmu_geom_t* g) {
    char line[48];
    char aux[48];
    if (!s->present) {
        int y = ui_draw_section(framebuffer, g->body_y, "电源芯片 Chip");
        ui_text(
            framebuffer, UI_MARGIN, y, UI_PX_BODY, "无应答 No reply",
            EPD_DRAW_ALIGN_LEFT, false
        );
        ui_text(
            framebuffer, UI_MARGIN, y + UI_ROW_H, UI_PX_BODY,
            "深睡时请短按电源键 Short-press power",
            EPD_DRAW_ALIGN_LEFT, false
        );
        pmu_draw_grid(framebuffer, 4, 1, g->bar_y, k_info_btn, NULL);
        return;
    }

    // 电压电量充电在电源页。这里只留原始 ADC、MCU 温度和协议态。/ Voltage, SOC, and charge are on the power page. Here: raw ADC, MCU temperature, and protocol state.
    int y = ui_draw_section(framebuffer, g->body_y, "采样 Sample");
    int t = s->mcu_temp_centi;
    snprintf(line, sizeof(line), "%u", s->battery_raw);
    snprintf(aux, sizeof(aux), "%d.%02d ℃", t / 100, (t < 0 ? -t : t) % 100);
    ui_draw_row2(framebuffer, y, "原始 Raw", line, "MCU 温度 Temp", aux);

    y = ui_draw_section(framebuffer, g->info_status_y, "运行状态 Status");
    snprintf(line, sizeof(line), "%u", s->pending_events);
    y = ui_draw_row2(
        framebuffer, y, "电源 Power", power_text(s->power_state),
        "待处理 Pending", line
    );
    snprintf(line, sizeof(line), "%04X", (uint16_t)s->fault_flags);
    fmt_uptime(aux, sizeof(aux), s->uptime_ms);
    ui_draw_row2(framebuffer, y, "故障 Fault", line, "运行 Up", aux);

    y = ui_draw_section(framebuffer, g->info_ident_y, "标识 Identity");
    if (s->uid_ok) {
        size_t o = 0;
        for (int i = 0; i < PMU_CHIP_UID_LEN && o + 2 < sizeof(line); i++) {
            o += (size_t)snprintf(line + o, sizeof(line) - o, "%02X", s->uid[i]);
        }
    } else {
        snprintf(line, sizeof(line), "未读到 No data");
    }
    y = ui_draw_row2(framebuffer, y, "PMU UID", line, NULL, NULL);
    snprintf(
        line, sizeof(line), "%s #%u",
        status_text(s->last_cmd_status), s->last_cmd_seq
    );
    ui_draw_row2(
        framebuffer, y, "上次命令 Last", line, "校验 Check",
        s->identity_ok && s->status_ok ? "正常 OK" : "未读到 No data"
    );

    y = ui_draw_section(framebuffer, g->info_flags_y, "状态位 Flags");
    draw_pmu_flags(framebuffer, y, s->flags);

    pmu_draw_grid(framebuffer, 4, 1, g->bar_y, k_info_btn, NULL);
}

static void draw_pmu_evt(uint8_t* framebuffer, const pmu_snapshot_t* s, const pmu_geom_t* g) {
    char line[48];
    char aux[48];

    int y = ui_draw_section(framebuffer, g->body_y, "队列 Queue");
    snprintf(line, sizeof(line), "%u", s->pending_events);
    ui_draw_row2(
        framebuffer, y, "待处理 Pending", line,
        "预览 Peek", s->event_ok ? "有 Yes" : "无 No"
    );

    y = ui_draw_section(framebuffer, g->evt_latest_y, "最近事件 Latest");
    if (s->event_ok) {
        snprintf(line, sizeof(line), "#%u　%s", s->event.event_id, event_text(s->event.type));
        snprintf(aux, sizeof(aux), "%u", s->event.severity);
        y = ui_draw_row2(framebuffer, y, "事件 Event", line, "级别 Sev", aux);
        snprintf(line, sizeof(line), "%lu ms", (unsigned long)s->event.timestamp_ms);
        snprintf(aux, sizeof(aux), "%lu / %u", (unsigned long)s->event.arg0, s->event.arg1);
        ui_draw_row2(framebuffer, y, "时刻 Time", line, "参数 Args", aux);
    } else {
        ui_text(
            framebuffer, UI_MARGIN, y + 12, UI_PX_BODY, "事件队列为空 Queue empty",
            EPD_DRAW_ALIGN_LEFT, false
        );
    }

    y = ui_draw_section(framebuffer, g->evt_diag_y, "诊断计数 Diagnostics");
    snprintf(line, sizeof(line), "%u", s->diag_i2c_rx);
    snprintf(aux, sizeof(aux), "%u", s->diag_i2c_recov);
    y = ui_draw_row2(framebuffer, y, "接收 Rx", line, "总线恢复 Recov", aux);
    snprintf(line, sizeof(line), "%u", s->diag_crc);
    snprintf(aux, sizeof(aux), "%u", s->diag_bad_len);
    y = ui_draw_row2(framebuffer, y, "校验错 CRC", line, "长度错 Len", aux);
    snprintf(line, sizeof(line), "%u", s->diag_unknown);
    snprintf(aux, sizeof(aux), "%u", s->diag_overflow);
    y = ui_draw_row2(framebuffer, y, "未知命令 Cmd", line, "溢出 Over", aux);
    snprintf(line, sizeof(line), "%u / %u", s->diag_sleep, s->diag_adc);
    snprintf(aux, sizeof(aux), "%u", s->diag_host_rst);
    ui_draw_row2(framebuffer, y, "睡眠 / 采样 Sleep/ADC", line, "主机复位 Rst", aux);

    pmu_draw_grid(framebuffer, 3, 1, g->bar_y, k_evt_btn, NULL);
}

static void draw_pmu_cfg(uint8_t* framebuffer, const pmu_snapshot_t* s, const pmu_geom_t* g) {
    char line[48];
    char aux[48];
    const pmu_config_t* c = &s->config;
    if (!s->config_ok) {
        int y = ui_draw_section(framebuffer, g->body_y, "配置 Config");
        ui_text(
            framebuffer, UI_MARGIN, y + 10, UI_PX_BODY, "未读到配置 No data",
            EPD_DRAW_ALIGN_LEFT, false
        );
    } else {
        int y = ui_draw_section(framebuffer, g->body_y, "电压阈值 mV");
        snprintf(line, sizeof(line), "%u", c->low_mv);
        snprintf(aux, sizeof(aux), "%u", c->critical_mv);
        y = ui_draw_row2(framebuffer, y, "低压 Low", line, "危压 Crit", aux);
        snprintf(line, sizeof(line), "%u", c->full_mv);
        snprintf(aux, sizeof(aux), "%u", c->forced_off_mv);
        y = ui_draw_row2(framebuffer, y, "满电 Full", line, "强制关机 Force", aux);
        snprintf(line, sizeof(line), "%u", c->hysteresis_mv);
        snprintf(aux, sizeof(aux), "%u %%", c->low_soc_threshold);
        ui_draw_row2(framebuffer, y, "回差 Hyst", line, "低电量 SOC", aux);

        y = ui_draw_section(framebuffer, g->cfg_key_y, "按键 ms");
        snprintf(line, sizeof(line), "%u", c->key_debounce_ms);
        snprintf(aux, sizeof(aux), "%u", c->key_long_ms);
        y = ui_draw_row2(framebuffer, y, "消抖 Debounce", line, "长按 Long", aux);
        snprintf(line, sizeof(line), "%u", c->key_force_event_ms);
        snprintf(aux, sizeof(aux), "%u", c->key_power_on_ms);
        y = ui_draw_row2(framebuffer, y, "强制事件 Force", line, "开机 On", aux);
        snprintf(line, sizeof(line), "%u", c->key_force_off_ms);
        ui_draw_row2(framebuffer, y, "强制关机 Off", line, NULL, NULL);

        y = ui_draw_section(framebuffer, g->cfg_to_y, "超时 Timeout");
        snprintf(line, sizeof(line), "%u", c->host_boot_timeout_ms);
        snprintf(aux, sizeof(aux), "%u", c->shutdown_timeout_ms);
        y = ui_draw_row2(framebuffer, y, "启动 Boot", line, "关机 Off", aux);
        snprintf(line, sizeof(line), "%u", c->led_override_timeout_ms);
        snprintf(aux, sizeof(aux), "%lu", (unsigned long)c->generation);
        ui_draw_row2(framebuffer, y, "灯超时 LED", line, "代数 Gen", aux);
    }
    bool on[] = {
        s->config_ok && c->wake_on_charge,
        s->config_ok && c->key_raw_events,
        s->config_ok && c->charge_led,
        s->config_ok && c->low_soc_enable,
    };
    pmu_draw_grid(framebuffer, 2, 2, g->cfg_y, k_cfg_btn, on);
}

static void draw_pmu_act(uint8_t* framebuffer, const pmu_snapshot_t* s, const pmu_geom_t* g) {
    char line[48];
    char aux[48];
    int y = ui_draw_section(framebuffer, g->body_y, "时间与闹钟 Time");
    snprintf(line, sizeof(line), "%lu", (unsigned long)s->unix_sec);
    y = ui_draw_row2(
        framebuffer, y, "对时 Sync",
        s->time_ok && s->time_synced ? "已对时 Synced" : "未对时 Unset",
        "Unix", line
    );
    snprintf(aux, sizeof(aux), "%lu s", (unsigned long)s->alarm_remain);
    ui_draw_row2(framebuffer, y, "闹钟 Alarm", alarm_text(s->alarm_mode), "剩余 Left", aux);

    bool led_on[5] = { 0 };
    uint8_t led = s->led_state & 0x03;
    if (led < 4) led_on[led] = true;
    pmu_draw_grid(framebuffer, 5, 1, g->act_led_y, k_led_btn, led_on);
    pmu_draw_grid(framebuffer, 3, 1, g->act_cmd_y, k_cmd_btn, NULL);
    pmu_draw_grid(framebuffer, 3, 1, g->act_tim_y, k_tim_btn, NULL);
    pmu_draw_grid(framebuffer, 3, 1, g->act_al_y, k_al_btn, NULL);
}

static void draw_pmu_page(uint8_t* framebuffer, const ui_pmu_state_t* state) {
    const pmu_snapshot_t* s = read_pico_pmu_get();
    const pmu_geom_t g = pmu_geom();
    char line[128];
    ui_clear_page(framebuffer);
    if (s->present) {
        snprintf(
            line, sizeof(line), "固件 FW %u.%u.%u　协议 Proto %u.%u　%s",
            s->fw_major, s->fw_minor, s->fw_patch,
            s->proto_major, s->proto_minor,
            state->last_msg[0] ? state->last_msg : status_text(s->last_op_status)
        );
    } else {
        snprintf(line, sizeof(line), "CW32 无应答 No reply");
    }
    ui_draw_header(framebuffer, PMU_TITLE, line);

    switch (state->view) {
        case UI_PMU_VIEW_EVT: draw_pmu_evt(framebuffer, s, &g); break;
        case UI_PMU_VIEW_CFG: draw_pmu_cfg(framebuffer, s, &g); break;
        case UI_PMU_VIEW_ACT: draw_pmu_act(framebuffer, s, &g); break;
        default: draw_pmu_info(framebuffer, s, &g); break;
    }
    draw_pmu_tabs(framebuffer, state->view);
    ui_draw_menu_handle(framebuffer, false);
}

static int pmu_hit_test(uint16_t x, uint16_t y, int view, const pmu_geom_t* g) {
    int tab = ui_bar_hit(x, y, UI_PMU_VIEW_COUNT);
    if (tab >= 0) return UI_PMU_HIT_TAB0 + tab;

    switch (view) {
        case UI_PMU_VIEW_INFO: {
            pmu_hit_band_t live[] = { { 4, 1, g->bar_y, k_info_ids } };
            return pmu_hit_bands(x, y, live, 1);
        }
        case UI_PMU_VIEW_EVT: {
            pmu_hit_band_t live[] = { { 3, 1, g->bar_y, k_evt_ids } };
            return pmu_hit_bands(x, y, live, 1);
        }
        case UI_PMU_VIEW_CFG: {
            pmu_hit_band_t live[] = { { 2, 2, g->cfg_y, k_cfg_ids } };
            return pmu_hit_bands(x, y, live, 1);
        }
        case UI_PMU_VIEW_ACT: {
            pmu_hit_band_t live[] = {
                { 5, 1, g->act_led_y, k_led_ids },
                { 3, 1, g->act_cmd_y, k_cmd_ids },
                { 3, 1, g->act_tim_y, k_tim_ids },
                { 3, 1, g->act_al_y, k_al_ids },
            };
            return pmu_hit_bands(x, y, live, 4);
        }
        default:
            break;
    }
    return UI_PMU_HIT_NONE;
}

static void pmu_note(const char* what) {
    const pmu_snapshot_t* s = read_pico_pmu_get();
    snprintf(
        s_pmu_ui.last_msg, sizeof(s_pmu_ui.last_msg), "%s %s",
        what,
        s->last_err == ESP_OK ? status_text(s->last_op_status)
                              : esp_err_to_name(s->last_err)
    );
}

static uint32_t pmu_unix_now(void) {
    time_t now = time(NULL);
    if (now >= 946684800) return (uint32_t)now;
    return 1787961600U;
}

static void pmu_select_view(int view) {
    s_pmu_ui.view = view;
    if (view == UI_PMU_VIEW_INFO) read_pico_pmu_poll();
    else read_pico_pmu_refresh();
}

static void pmu_cfg_set(uint16_t cmd, const uint8_t* p, size_t n, const char* note) {
    read_pico_pmu_cmd(cmd, p, n);
    read_pico_pmu_cmd(PMU_CMD_CONFIG_GET, NULL, 0);
    pmu_note(note);
}

static void pmu_alarm_set(uint8_t mode, uint32_t secs, const char* note) {
    uint8_t buf[5] = { mode };
    pmu_wr_u32le(&buf[1], secs);
    read_pico_pmu_cmd(PMU_CMD_ALARM_SET, buf, 5);
    read_pico_pmu_cmd(PMU_CMD_ALARM_GET, NULL, 0);
    pmu_note(note);
}

// 和锁屏关机同一条路：先卸屏轨、刷 SD，再 REQUEST_OFF → SHUTDOWN_READY。/ Same path as lock-then-off: drop panel rails, sync SD, then REQUEST_OFF → SHUTDOWN_READY.
static void pmu_host_off(void) {
    pmu_selftest_prepare_powerdown();
    if (read_pico_pmu_power_off() != ESP_OK) {
        pmu_note("关机失败 Off fail");
        return;
    }
    while (true) vTaskDelay(pdMS_TO_TICKS(1000));
}

// 先通知 PMU 主机要重启，再由 ESP 自己复位，不让 PMU 拉 EN。/ Tell the PMU the host will reboot, then ESP resets itself; do not let the PMU pull EN.
static void pmu_host_reset(void) {
    uint8_t req[2] = { 0, 0 };
    (void)read_pico_pmu_cmd(PMU_CMD_HOST_REQUEST_RESET, req, sizeof(req));
    pmu_selftest_prepare_powerdown();
    esp_restart();
}

// 通知 PMU 后进 ROM 下载；FORCE_DOWNLOAD_BOOT 只对 CPU 复位有效，所以不让 PMU 掉电。/ Notify the PMU, then enter ROM download; FORCE_DOWNLOAD_BOOT only works on a CPU reset, so do not let the PMU cut power.
static void pmu_host_download(void) {
    uint8_t req[2] = { 0, 0 };
    (void)read_pico_pmu_cmd(PMU_CMD_HOST_REQUEST_RESET, req, sizeof(req));
    pmu_selftest_prepare_powerdown();
    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    esp_restart();
}

static void pmu_handle_hit(int hit) {
    uint8_t buf[8] = { 0 };
    if (hit >= UI_PMU_HIT_TAB0 && hit < UI_PMU_HIT_TAB0 + UI_PMU_VIEW_COUNT) {
        pmu_select_view(hit - UI_PMU_HIT_TAB0);
        return;
    }
    switch (hit) {
        case UI_PMU_HIT_READ:
            read_pico_pmu_refresh();
            (void)read_pico_pmu_uid_get(NULL);
            pmu_note("读取 Read");
            break;
        case UI_PMU_HIT_OFF:
            pmu_host_off();
            break;
        case UI_PMU_HIT_RST:
            pmu_host_reset();
            break;
        case UI_PMU_HIT_DL:
            pmu_host_download();
            break;
        case UI_PMU_HIT_ACK: {
            const pmu_snapshot_t* s = read_pico_pmu_get();
            if (s->event_ok) read_pico_pmu_event_ack(s->event.event_id);
            pmu_note("确认 Ack");
            break;
        }
        case UI_PMU_HIT_EVT_CLR:
            read_pico_pmu_cmd(PMU_CMD_EVENTS_CLEAR_ALL, NULL, 0);
            read_pico_pmu_poll();
            pmu_note("清事件 Clear");
            break;
        case UI_PMU_HIT_DIAG_CLR:
            read_pico_pmu_cmd(PMU_CMD_CLEAR_DIAGNOSTICS, NULL, 0);
            read_pico_pmu_refresh();
            pmu_note("清诊断 Diag");
            break;
        case UI_PMU_HIT_CFG_WAKE:
            buf[0] = read_pico_pmu_get()->config.wake_on_charge ? 0 : 1;
            pmu_cfg_set(PMU_CMD_CONFIG_SET_WAKE_ON_CHARGE, buf, 1, "来电开机 AC On");
            break;
        case UI_PMU_HIT_CFG_KEY:
            buf[0] = read_pico_pmu_get()->config.key_raw_events ? 0 : 1;
            pmu_cfg_set(PMU_CMD_CONFIG_SET_KEY_EVENTS, buf, 1, "按键 Key");
            break;
        case UI_PMU_HIT_CFG_LED:
            buf[0] = read_pico_pmu_get()->config.charge_led ? 0 : 1;
            pmu_cfg_set(PMU_CMD_CONFIG_SET_CHARGE_LED, buf, 1, "充灯 ChgLED");
            break;
        case UI_PMU_HIT_CFG_SOC:
            if (read_pico_pmu_get()->config.low_soc_enable) {
                buf[0] = 0;
                pmu_cfg_set(PMU_CMD_CONFIG_SET_LOW_SOC_NOTIFY, buf, 1, "低电通知 LowSOC");
            } else {
                buf[0] = 1;
                buf[1] = 50;
                buf[2] = 0;
                pmu_cfg_set(PMU_CMD_CONFIG_SET_LOW_SOC_NOTIFY, buf, 3, "低电通知 LowSOC");
            }
            break;
        case UI_PMU_HIT_LED_OFF:
        case UI_PMU_HIT_LED_RED:
        case UI_PMU_HIT_LED_WHT:
        case UI_PMU_HIT_LED_BOTH:
            buf[0] = (uint8_t)(hit - UI_PMU_HIT_LED_OFF);
            buf[1] = 255;
            read_pico_pmu_cmd(PMU_CMD_LED_SET, buf, 4);
            pmu_note("指示灯 LED");
            break;
        case UI_PMU_HIT_LED_CLR:
            read_pico_pmu_cmd(PMU_CMD_LED_OVERRIDE_CLEAR, NULL, 0);
            pmu_note("清除 Clear");
            break;
        case UI_PMU_HIT_PING:
            read_pico_pmu_cmd(PMU_CMD_PING, NULL, 0);
            pmu_note("连通 Ping");
            break;
        case UI_PMU_HIT_SAMPLE:
            read_pico_pmu_cmd(PMU_CMD_BATTERY_SAMPLE, NULL, 0);
            pmu_note("采电池 Sample");
            break;
        case UI_PMU_HIT_READY:
            read_pico_pmu_cmd(PMU_CMD_HOST_READY, buf, 1);
            pmu_note("就绪 Ready");
            break;
        case UI_PMU_HIT_TIME_GET:
            read_pico_pmu_cmd(PMU_CMD_TIME_GET, NULL, 0);
            pmu_note("读时间 Time");
            break;
        case UI_PMU_HIT_TIME_SYNC: {
            pmu_wr_u32le(buf, pmu_unix_now());
            read_pico_pmu_cmd(PMU_CMD_TIME_SYNC, buf, 4);
            read_pico_pmu_cmd(PMU_CMD_TIME_GET, NULL, 0);
            pmu_note("对时 Sync");
            break;
        }
        case UI_PMU_HIT_ALARM_GET:
            read_pico_pmu_cmd(PMU_CMD_ALARM_GET, NULL, 0);
            pmu_note("读闹钟 Alarm");
            break;
        case UI_PMU_HIT_ALARM_OFF:
            pmu_alarm_set(0, 0, "关闹钟 Off");
            break;
        case UI_PMU_HIT_ALARM_10:
            pmu_alarm_set(1, 10, "闹钟 10s");
            break;
        case UI_PMU_HIT_ALARM_60:
            pmu_alarm_set(1, 60, "闹钟 60s");
            break;
        default:
            break;
    }
}

static void on_enter(app_ctx_t* ctx) {
    read_pico_pmu_refresh();
    (void)read_pico_pmu_uid_get(NULL);
    snprintf(s_pmu_ui.last_msg, sizeof(s_pmu_ui.last_msg), "已打开 Open");
    s_last_poll_ms = ctx->now_ms;
}

static void render(app_ctx_t* ctx, uint8_t* fb) {
    (void)ctx;
    draw_pmu_page(fb, &s_pmu_ui);
}

static EpdRect area_hint(app_ctx_t* ctx) {
    (void)ctx;
    return ui_content_refresh_area();
}

static app_redraw_t on_touch(app_ctx_t* ctx, const cst836u_touch_t* touch) {
    const pmu_geom_t g = pmu_geom();
    int hit = pmu_hit_test(touch->x, touch->y, s_pmu_ui.view, &g);
    if (hit < 0) return APP_REDRAW_NONE;
    pmu_handle_hit(hit);
    s_last_poll_ms = ctx->now_ms;
    ESP_LOGI(TAG, "PMU hit %d view %d", hit, s_pmu_ui.view);
    return APP_REDRAW_PAGE;
}

static app_redraw_t on_tick(app_ctx_t* ctx) {
    if (ctx->now_ms - s_last_poll_ms < PMU_POLL_MS) return APP_REDRAW_NONE;
    s_last_poll_ms = ctx->now_ms;
    const pmu_snapshot_t* s = read_pico_pmu_get();
    uint8_t prev_ev = s->pending_events;
    uint8_t prev_chg = s->charge_state;
    uint16_t prev_mv = s->battery_mv;
    uint8_t prev_pwr = s->power_state;
    if (read_pico_pmu_poll() != ESP_OK) return APP_REDRAW_NONE;
    s = read_pico_pmu_get();
    if (s->pending_events == prev_ev && s->charge_state == prev_chg
        && s->battery_mv == prev_mv && s->power_state == prev_pwr) {
        return APP_REDRAW_NONE;
    }
    draw_pmu_page(ctx->fb, &s_pmu_ui);
    return APP_REDRAW_AREA;
}

const app_desc_t app_pmu = {
    .title = PMU_TITLE,
    .detail = "协议状态与命令 Protocol Status & Commands",
    .render = render,
    .on_enter = on_enter,
    .on_touch = on_touch,
    .on_tick = on_tick,
    .area_hint = area_hint,
};
