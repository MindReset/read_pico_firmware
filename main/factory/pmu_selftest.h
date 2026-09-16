/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 设备功能自检步骤表。0..GATE_END 是探活，GATE_END+1..CMD_END 是发令看 ACK，
 * 其后是要断电或等人的物理项（只在后台记结果）。
 *
 * Device self-test step IDs. 0..GATE_END are live probes, GATE_END+1..CMD_END
 * are command ACKs, the rest are physical items that cut power or wait
 * for a person (results kept in the backend only).
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "sc7a20h.h"

#define PMU_ST_COUNT 26
/// 探活组最后一项下标（含）。其后是发令看 ACK。
/// Last live-probe index, inclusive. Command-ACK items follow.
#define PMU_ST_GATE_END 4
/// 命令组最后一项下标（含）。其后是断电/等人的物理项。
/// Last command-ACK index, inclusive. Physical items follow.
#define PMU_ST_CMD_END 17

/// 探活 / Live probes
#define PMU_ST_IDENT 0
#define PMU_ST_HANDSHAKE 1
#define PMU_ST_PING 2
#define PMU_ST_STATUS 3
#define PMU_ST_QUICK_BAT 4
/// 发令看 ACK / Command ACKs
#define PMU_ST_BAT_SAMPLE 5
#define PMU_ST_TIME_SYNC 6
#define PMU_ST_ALARM_SET 7
#define PMU_ST_ALARM_OFF 8
#define PMU_ST_CFG_WAKE 9
#define PMU_ST_CFG_KEY 10
#define PMU_ST_CFG_LED 11
#define PMU_ST_CFG_SOC 12
#define PMU_ST_EVT_CLEAR 13
#define PMU_ST_LED_CMD 14
#define PMU_ST_SY_RAIL 15
#define PMU_ST_ACCEL 16
/// 物理动作（断电或等人）/ Physical items (power-cut or wait)
#define PMU_ST_LED_RED 17
#define PMU_ST_LED_WHT 18
#define PMU_ST_KEY_SHORT 19
#define PMU_ST_CHARGE 20
#define PMU_ST_LIGHT_KEY 21
#define PMU_ST_LIGHT_PICKUP 22
#define PMU_ST_DEEP_SLEEP 23
#define PMU_ST_ALARM_WAKE 24
#define PMU_ST_POWER_OFF 25

#define PMU_ST_RES_NONE 0   ///< 未跑 / not run
#define PMU_ST_RES_PASS 1   ///< 通过 / pass
#define PMU_ST_RES_FAIL 2   ///< 失败 / fail
#define PMU_ST_RES_SKIP 3   ///< 跳过 / skip

#define PMU_ST_HIT_NONE (-1)
#define PMU_ST_HIT_PROBE 0
#define PMU_ST_HIT_RESET 1

typedef struct {
    uint8_t results[PMU_ST_COUNT];
    uint8_t pass_n;
    uint8_t fail_n;
    uint8_t skip_n;
    uint8_t pending_n;
    char hint[96];
    char last_msg[96];
} pmu_st_view_t;

void pmu_selftest_bind(sc7a20h_handle_t acc);
bool pmu_selftest_boot_resume(void);
const pmu_st_view_t* pmu_selftest_view(void);

void pmu_selftest_reset(void);
void pmu_selftest_run_probe(void);
void pmu_selftest_prepare_powerdown(void);

const char* pmu_selftest_name(int id);
const char* pmu_selftest_result_cn(uint8_t result);
