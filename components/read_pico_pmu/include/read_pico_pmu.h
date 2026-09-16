/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 小纸 Pico CW32L010 PMU 主机侧：I2C 协议、电池、软睡、闹钟、关机。
 * 总线由调用方注入，不依赖板级 BSP。
 *
 * Read Pico CW32L010 PMU host: I2C protocol, battery, soft sleep, alarm,
 * power-off. The caller injects the bus; this file does not depend on the
 * board BSP.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "read_pico_pmu_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t low_mv;
    uint16_t critical_mv;
    uint16_t key_long_ms;
    uint16_t key_force_event_ms;
    uint16_t key_power_on_ms;
    uint16_t key_force_off_ms;
    uint16_t full_mv;
    uint32_t generation;
    uint16_t forced_off_mv;
    uint16_t hysteresis_mv;
    uint16_t key_debounce_ms;
    uint16_t host_boot_timeout_ms;
    uint16_t shutdown_timeout_ms;
    uint16_t led_override_timeout_ms;
    uint8_t wake_on_charge;
    uint8_t key_raw_events;
    uint8_t charge_led;
    uint8_t low_soc_enable;
    uint16_t low_soc_threshold;
} pmu_config_t;

typedef struct {
    bool present;
    bool identity_ok;
    bool status_ok;
    bool diag_ok;
    bool event_ok;
    bool config_ok;
    bool time_ok;
    bool alarm_ok;

    uint8_t proto_major;
    uint8_t proto_minor;
    uint8_t fw_major;
    uint8_t fw_minor;
    uint8_t fw_patch;
    uint8_t hw_rev;
    uint16_t device_id;
    uint8_t uid[PMU_CHIP_UID_LEN];
    bool uid_ok;
    uint32_t boot_id;
    uint32_t caps;
    uint16_t build_number;
    uint32_t build_hash;

    uint16_t generation;
    uint8_t power_state;
    uint8_t wake_reason;
    uint32_t flags;
    uint16_t battery_mv;
    uint16_t battery_raw;
    uint16_t soc_permille;
    int16_t mcu_temp_centi;
    uint8_t charge_state;
    uint8_t key_state;
    uint8_t led_state;
    uint8_t host_state;
    uint32_t fault_flags;
    uint8_t pending_events;
    uint8_t reset_reason;
    uint8_t last_cmd_status;
    uint32_t uptime_ms;
    uint32_t config_generation;
    uint16_t last_cmd_seq;
    uint16_t last_event_id;
    uint32_t last_action_reason;

    uint16_t diag_i2c_rx;
    uint16_t diag_i2c_recov;
    uint16_t diag_crc;
    uint16_t diag_bad_len;
    uint16_t diag_unknown;
    uint16_t diag_overflow;
    uint16_t diag_sleep;
    uint16_t diag_host_rst;
    uint16_t diag_adc;

    uint16_t qb_mv;
    uint16_t qb_soc;
    uint8_t qb_charge;
    uint8_t qb_flags;

    pmu_event_t event;
    pmu_config_t config;
    uint32_t unix_sec;
    uint8_t time_synced;
    uint8_t alarm_mode;
    uint32_t alarm_remain;
    uint32_t alarm_target;
    /// RTC_RAW：CR0 CR1 CR2 PSC DATE TIME SSCNT SYSCTRL_CR1 SYSCTRL_LSI
    /// / RTC_RAW: CR0 CR1 CR2 PSC DATE TIME SSCNT SYSCTRL_CR1 SYSCTRL_LSI
    uint32_t rtc_raw[9];
    bool rtc_raw_ok;

    uint16_t last_op_code;
    uint16_t last_op_status;
    esp_err_t last_err;

} pmu_snapshot_t;

#ifndef READ_PICO_PMU_I2C_HZ_DEFAULT
#define READ_PICO_PMU_I2C_HZ_DEFAULT 400000
#endif

typedef struct {
    uint16_t i2c_addr;
    uint32_t scl_speed_hz;
} read_pico_pmu_config_t;

#define READ_PICO_PMU_CONFIG_DEFAULT() \
    { \
        .i2c_addr = PMU_I2C_ADDR, \
        .scl_speed_hz = READ_PICO_PMU_I2C_HZ_DEFAULT, \
    }

esp_err_t read_pico_pmu_init(i2c_master_bus_handle_t bus_handle, const read_pico_pmu_config_t* config);
esp_err_t read_pico_pmu_deinit(void);
bool read_pico_pmu_ready(void);
const pmu_snapshot_t* read_pico_pmu_get(void);

/// 读 IDENTITY / STATUS / DIAG / 事件 / 配置 / 时间 / 闹钟。
/// / Read IDENTITY / STATUS / DIAG / event / config / time / alarm.
esp_err_t read_pico_pmu_refresh(void);
/// 只刷新 STATUS / QUICK / 事件计数，给页面轮询用。
/// / Refresh STATUS / QUICK / event count only, for page polls.
esp_err_t read_pico_pmu_poll(void);

esp_err_t read_pico_pmu_cmd(uint16_t code, const uint8_t* payload, uint8_t plen);
/// 读出厂绑定的面板 VCOM。valid 则 *out_mv 为 500..2500，
/// 未标定 / 旧固件 / 读失败一律 ESP_ERR_NOT_FOUND。
/// / Read factory-bound panel VCOM. When valid, *out_mv is 500..2500.
/// Unset / old firmware / read failure all return ESP_ERR_NOT_FOUND.
esp_err_t read_pico_pmu_vcom_get(int* out_mv);
/// 出厂标定页写入。mv 须 500..2500 且整十。
/// / Factory setup-page write. mv must be 500..2500 and a multiple of 10.
esp_err_t read_pico_pmu_vcom_set(int mv);
/// 读 CW32 出厂 UID（10 字节）。成功过一次会缓存在 snapshot 里。
/// / Read the CW32 factory UID (10 bytes). Cached in the snapshot after first success.
esp_err_t read_pico_pmu_uid_get(uint8_t out[PMU_CHIP_UID_LEN]);
esp_err_t read_pico_pmu_event_ack(uint16_t event_id);
esp_err_t read_pico_pmu_action(uint8_t action, uint16_t delay_ms, uint16_t reason);
/// 抽干事件队列，不解释按键（开机丢掉上电/烧录残留）。
/// / Drain the event queue without interpreting keys (drop boot/flash leftovers).
void read_pico_pmu_drain_events(void);
/// 抽干事件队列；若有电源键短按返回 true。
/// / Drain the queue; true if a power-key short press was seen.
bool read_pico_pmu_take_key_short(void);
/// 抽干队列；按下或短按都算唤醒（浅睡常先收到 KEY_DOWN）。
/// / Drain the queue; KEY_DOWN or SHORT both count as wake (light sleep often sees KEY_DOWN first).
bool read_pico_pmu_take_key_wakeup(void);
/// 保证 RUNNING 后发 HOST_SOFT_SLEEP，等到 STATUS 为 SOFT_SLEEP（随后 EN=0）。
/// / After RUNNING, send HOST_SOFT_SLEEP and wait for STATUS SOFT_SLEEP (then EN=0).
esp_err_t read_pico_pmu_report_sleep(void);
/// 发 HOST_READY，等到 STATUS 为 RUNNING。
/// / Send HOST_READY and wait for STATUS RUNNING.
esp_err_t read_pico_pmu_report_ready(void);
/// 协作关机：REQUEST_OFF → SHUTDOWN_READY，EN 保持低。
/// / Cooperative power-off: REQUEST_OFF → SHUTDOWN_READY; EN stays low.
esp_err_t read_pico_pmu_power_off(void);

const char* pmu_power_name(uint8_t state);
const char* pmu_charge_name(uint8_t state);
const char* pmu_event_name(uint8_t type);
const char* pmu_status_name(uint16_t status);
const char* pmu_led_name(uint8_t color);
const char* pmu_action_name(uint8_t action);

#ifdef __cplusplus
}
#endif
