/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 设备功能自检。探活真测，其余发令看 ACK；断电项只在后台记结果。
 *
 * Device self-test. Live probes are real measurements; the rest send a command
 * and watch ACK. Power-cut items only record results in the backend.
 */

#include "pmu_selftest.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "epdiy.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "read_pico_board.h"
#include "read_pico_pmu.h"
#include "read_pico_sd.h"

#define TAG "pmu_st"
#define NVS_NS "pmu_st"
#define NVS_KEY "blob"
#define BLOB_MAGIC 0xAA
#define SY_PGOOD_BIT (1U << 5)

typedef struct {
    uint8_t magic;
    uint8_t results[PMU_ST_COUNT];
} pmu_st_blob_t;

static pmu_st_view_t s_view;
static sc7a20h_handle_t s_acc;
static pmu_st_blob_t s_blob;
static char s_detail[96];

static const char* const s_names[PMU_ST_COUNT] = {
    "身份", "握手", "连通", "状态",
    "快读电池", "采电池", "对时", "设闹钟",
    "关闹钟", "充电唤醒", "按键事件", "充电灯",
    "低电通知", "清事件", "指示灯命令",
    "屏电源轨", "加速度计",
    "红灯", "白灯", "短按", "插拔充电",
    "浅睡按键", "浅睡拿起",
    "深睡开机", "闹钟唤醒", "关机开机",
};

const char* pmu_selftest_name(int id) {
    if (id < 0 || id >= PMU_ST_COUNT) return "?";
    return s_names[id];
}

const char* pmu_selftest_result_cn(uint8_t result) {
    switch (result) {
        case PMU_ST_RES_PASS: return "通过 Pass";
        case PMU_ST_RES_FAIL: return "失败 Fail";
        case PMU_ST_RES_SKIP: return "跳过 Skip";
        default: return "-";
    }
}

static void recount(void) {
    s_view.pass_n = s_view.fail_n = s_view.skip_n = s_view.pending_n = 0;
    for (int i = 0; i < PMU_ST_COUNT; i++) {
        s_view.results[i] = s_blob.results[i];
        switch (s_blob.results[i]) {
            case PMU_ST_RES_PASS: s_view.pass_n++; break;
            case PMU_ST_RES_FAIL: s_view.fail_n++; break;
            case PMU_ST_RES_SKIP: s_view.skip_n++; break;
            default: s_view.pending_n++; break;
        }
    }
}

static void save_blob(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    s_blob.magic = BLOB_MAGIC;
    nvs_set_blob(h, NVS_KEY, &s_blob, sizeof(s_blob));
    nvs_commit(h);
    nvs_close(h);
}

static void load_blob(void) {
    memset(&s_blob, 0, sizeof(s_blob));
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t len = sizeof(s_blob);
    pmu_st_blob_t tmp = { 0 };
    if (nvs_get_blob(h, NVS_KEY, &tmp, &len) == ESP_OK
        && tmp.magic == BLOB_MAGIC && len == sizeof(tmp)) {
        s_blob = tmp;
    }
    nvs_close(h);
}

static void note(const char* msg) {
    strlcpy(s_view.last_msg, msg ? msg : "", sizeof(s_view.last_msg));
}

static void hint(const char* msg) {
    snprintf(s_view.hint, sizeof(s_view.hint), "%s", msg ? msg : "");
}

static void mark(int id, uint8_t res, const char* msg) {
    if (id < 0 || id >= PMU_ST_COUNT) return;
    s_blob.results[id] = res;
    s_view.results[id] = res;
    if (msg) note(msg);
    ESP_LOGI(
        TAG, "%s %s %s", s_names[id], pmu_selftest_result_cn(res),
        msg ? msg : ""
    );
}

static bool i2c_dead(void) {
    return read_pico_pmu_get()->last_err != ESP_OK;
}

static bool cmd_ok(uint16_t code, const uint8_t* payload, uint8_t plen) {
    if (read_pico_pmu_cmd(code, payload, plen) != ESP_OK) return false;
    uint16_t st = read_pico_pmu_get()->last_op_status;
    return st == PMU_STATUS_OK || st == PMU_STATUS_ACCEPTED;
}

static uint32_t unix_now(void) {
    time_t now = time(NULL);
    if (now >= 946684800) return (uint32_t)now;
    return 1704067200u;
}

static void wr32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static bool run_ident(void) {
    if (read_pico_pmu_refresh() != ESP_OK) return false;
    const pmu_snapshot_t* s = read_pico_pmu_get();
    if (!s->identity_ok || s->device_id != PMU_DEVICE_ID) return false;
    if (s->proto_major != 1) return false;
    if (s->fw_major != PMU_FW_MAJOR) return false;
    if (s->fw_minor != PMU_FW_MINOR) return false;
    return s->fw_patch >= PMU_FW_PATCH;
}

static bool run_handshake(void) {
    if (read_pico_pmu_poll() != ESP_OK) return false;
    const pmu_snapshot_t* s = read_pico_pmu_get();
    return s->power_state == PMU_PWR_RUNNING
        && (s->flags & PMU_STATUS_HOST_EN_HIGH) != 0;
}

static bool run_ping(void) {
    return cmd_ok(PMU_CMD_PING, NULL, 0);
}

static bool run_status(void) {
    for (int i = 0; i < 3; i++) {
        if (i) vTaskDelay(pdMS_TO_TICKS(20));
        if (read_pico_pmu_poll() != ESP_OK) continue;
        if (read_pico_pmu_get()->status_ok) return true;
    }
    return false;
}

static bool run_quick_bat(void) {
    if (read_pico_pmu_poll() != ESP_OK) return false;
    const pmu_snapshot_t* s = read_pico_pmu_get();
    bool valid = (s->qb_flags & 0x01) != 0 && s->qb_mv >= 2700 && s->qb_mv <= 4400;
    bool charging = (s->qb_flags & 0x04) != 0
        || s->charge_state == PMU_CHARGE_CHARGING;
    return valid || charging;
}

static bool run_bat_sample(void) {
    return cmd_ok(PMU_CMD_BATTERY_SAMPLE, NULL, 0);
}

static bool run_time_sync(void) {
    uint32_t t = unix_now();
    uint8_t buf[4];
    wr32(buf, t);
    if (!cmd_ok(PMU_CMD_TIME_SYNC, buf, 4)) return false;
    if (!cmd_ok(PMU_CMD_TIME_GET, NULL, 0)) return false;
    const pmu_snapshot_t* s = read_pico_pmu_get();
    if (!s->time_ok || !s->time_synced) return false;
    int32_t d = (int32_t)s->unix_sec - (int32_t)t;
    if (d < 0) d = -d;
    return d < 5;
}

static bool run_alarm_set(void) {
    uint8_t buf[5] = { 1, 10, 0, 0, 0 };
    if (!cmd_ok(PMU_CMD_ALARM_SET, buf, 5)) return false;
    if (!cmd_ok(PMU_CMD_ALARM_GET, NULL, 0)) return false;
    const pmu_snapshot_t* s = read_pico_pmu_get();
    return s->alarm_ok && s->alarm_mode == 1
        && s->alarm_remain >= 5 && s->alarm_remain <= 15;
}

static bool run_alarm_off(void) {
    uint8_t buf[5] = { 0 };
    if (!cmd_ok(PMU_CMD_ALARM_SET, buf, 5)) return false;
    if (!cmd_ok(PMU_CMD_ALARM_GET, NULL, 0)) return false;
    return read_pico_pmu_get()->alarm_mode == 0;
}

static bool cfg_rw_u8(uint16_t cmd, uint8_t orig, uint16_t orig_th, bool soc) {
    uint8_t next = orig ? 0 : 1;
    uint8_t buf[3] = { next, 0, 0 };
    uint8_t plen = 1;
    if (soc && next) {
        buf[1] = 50;
        buf[2] = 0;
        plen = 3;
    }
    if (!cmd_ok(cmd, buf, plen)) return false;
    if (!cmd_ok(PMU_CMD_CONFIG_GET, NULL, 0)) return false;
    const pmu_snapshot_t* s = read_pico_pmu_get();
    uint8_t got = 0;
    if (cmd == PMU_CMD_CONFIG_SET_WAKE_ON_CHARGE) got = s->config.wake_on_charge;
    else if (cmd == PMU_CMD_CONFIG_SET_KEY_EVENTS) got = s->config.key_raw_events;
    else if (cmd == PMU_CMD_CONFIG_SET_CHARGE_LED) got = s->config.charge_led;
    else got = s->config.low_soc_enable;
    bool ok = got == next;
    buf[0] = orig;
    plen = 1;
    if (soc && orig) {
        buf[1] = (uint8_t)orig_th;
        buf[2] = (uint8_t)(orig_th >> 8);
        plen = 3;
    }
    (void)cmd_ok(cmd, buf, plen);
    (void)cmd_ok(PMU_CMD_CONFIG_GET, NULL, 0);
    return ok;
}

static bool run_cfg(uint16_t cmd) {
    if (!cmd_ok(PMU_CMD_CONFIG_GET, NULL, 0)) return false;
    const pmu_config_t* c = &read_pico_pmu_get()->config;
    if (cmd == PMU_CMD_CONFIG_SET_WAKE_ON_CHARGE) {
        return cfg_rw_u8(cmd, c->wake_on_charge, 0, false);
    }
    if (cmd == PMU_CMD_CONFIG_SET_KEY_EVENTS) {
        return cfg_rw_u8(cmd, c->key_raw_events, 0, false);
    }
    if (cmd == PMU_CMD_CONFIG_SET_CHARGE_LED) {
        return cfg_rw_u8(cmd, c->charge_led, 0, false);
    }
    return cfg_rw_u8(cmd, c->low_soc_enable, c->low_soc_threshold, true);
}

static bool run_evt_clear(void) {
    if (!cmd_ok(PMU_CMD_EVENTS_CLEAR_ALL, NULL, 0)) return false;
    read_pico_pmu_drain_events();
    if (read_pico_pmu_poll() != ESP_OK) return false;
    return read_pico_pmu_get()->pending_events == 0;
}

static bool run_led_cmd(void) {
    uint8_t on[4] = { PMU_LED_RED, 255, 0, 0 };
    if (!cmd_ok(PMU_CMD_LED_SET, on, 4)) return false;
    return cmd_ok(PMU_CMD_LED_OVERRIDE_CLEAR, NULL, 0);
}

static bool run_sy_rail(void) {
    read_pico_status_t st = { 0 };
    if (read_pico_get_status(&st) != ESP_OK) return false;
    return st.rails_on && (st.ioe_input & SY_PGOOD_BIT) != 0;
}

static bool run_accel(void) {
    if (s_acc == NULL) return false;
    bool was = sc7a20h_powered(s_acc);
    if (!was && sc7a20h_power_up(s_acc) != ESP_OK) return false;
    uint8_t who = 0, ver = 0;
    esp_err_t err = sc7a20h_version(s_acc, &who, &ver);
    if (!was) sc7a20h_power_down(s_acc);
    return err == ESP_OK && who == SC7A20H_WHO_AM_I_VAL;
}

static bool run_case(int id) {
    s_detail[0] = '\0';
    switch (id) {
        case PMU_ST_IDENT: return run_ident();
        case PMU_ST_HANDSHAKE: return run_handshake();
        case PMU_ST_PING: return run_ping();
        case PMU_ST_STATUS: return run_status();
        case PMU_ST_QUICK_BAT: return run_quick_bat();
        case PMU_ST_BAT_SAMPLE: return run_bat_sample();
        case PMU_ST_TIME_SYNC: return run_time_sync();
        case PMU_ST_ALARM_SET: return run_alarm_set();
        case PMU_ST_ALARM_OFF: return run_alarm_off();
        case PMU_ST_CFG_WAKE: return run_cfg(PMU_CMD_CONFIG_SET_WAKE_ON_CHARGE);
        case PMU_ST_CFG_KEY: return run_cfg(PMU_CMD_CONFIG_SET_KEY_EVENTS);
        case PMU_ST_CFG_LED: return run_cfg(PMU_CMD_CONFIG_SET_CHARGE_LED);
        case PMU_ST_CFG_SOC: return run_cfg(PMU_CMD_CONFIG_SET_LOW_SOC_NOTIFY);
        case PMU_ST_EVT_CLEAR: return run_evt_clear();
        case PMU_ST_LED_CMD: return run_led_cmd();
        case PMU_ST_SY_RAIL: return run_sy_rail();
        case PMU_ST_ACCEL: return run_accel();
        default: return false;
    }
}

const pmu_st_view_t* pmu_selftest_view(void) {
    return &s_view;
}

void pmu_selftest_bind(sc7a20h_handle_t acc) {
    s_acc = acc;
    load_blob();
    recount();
    if (s_view.pending_n == 0
        && (s_view.pass_n + s_view.fail_n + s_view.skip_n) > 0) {
        hint(s_view.fail_n ? "上次探活失败" : "探活通过");
        note("结果来自上次");
    }
}

void pmu_selftest_reset(void) {
    memset(&s_blob, 0, sizeof(s_blob));
    s_blob.magic = BLOB_MAGIC;
    memset(&s_view, 0, sizeof(s_view));
    recount();
    save_blob();
    hint("已清空，点探测开始");
    note("已清空");
}

bool pmu_selftest_boot_resume(void) {
    load_blob();
    recount();
    return false;
}

void pmu_selftest_run_probe(void) {
    memset(&s_blob, 0, sizeof(s_blob));
    s_blob.magic = BLOB_MAGIC;
    recount();
    if (!read_pico_pmu_ready()) {
        for (int i = 0; i < PMU_ST_CMD_END; i++) {
            mark(i, PMU_ST_RES_FAIL, "PMU 未就绪");
        }
        for (int i = PMU_ST_CMD_END; i < PMU_ST_COUNT; i++) {
            mark(i, PMU_ST_RES_SKIP, NULL);
        }
        hint("PMU 未就绪");
        recount();
        save_blob();
        return;
    }

    bool gate_ok = true;
    for (int i = 0; i < PMU_ST_CMD_END; i++) {
        if (!gate_ok) {
            mark(i, PMU_ST_RES_FAIL, "探活未过");
            continue;
        }
        if (i2c_dead()) {
            mark(i, PMU_ST_RES_FAIL, "I2C 中断");
            gate_ok = false;
            continue;
        }
        if (i == PMU_ST_ACCEL && s_acc == NULL) {
            mark(i, PMU_ST_RES_SKIP, "无加速度计");
            continue;
        }
        bool ok = run_case(i);
        mark(i, ok ? PMU_ST_RES_PASS : PMU_ST_RES_FAIL,
             s_detail[0] ? s_detail : (ok ? "通过" : "断言失败"));
        if (i < PMU_ST_GATE_END && !ok) gate_ok = false;
    }
    for (int i = PMU_ST_CMD_END; i < PMU_ST_COUNT; i++) {
        mark(i, PMU_ST_RES_SKIP, NULL);
    }
    recount();
    save_blob();
    if (s_view.fail_n) {
        hint("探活失败");
        note("探活失败");
    } else {
        hint("探活通过");
        note("探活通过");
    }
}

void pmu_selftest_prepare_powerdown(void) {
    epd_poweroff();
    esp_err_t err = read_pico_sd_sync();
    if (err != ESP_OK && err != ESP_ERR_NOT_FINISHED) {
        ESP_LOGW(TAG, "sd sync %s", esp_err_to_name(err));
    }
}
