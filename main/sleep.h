/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 锁屏、浅睡等待、软睡/关机下电。
 *
 * Lock, light-sleep wait, and soft-sleep / power-off rail drop.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cst836u.h"
#include "epd_highlevel.h"
#include "esp_err.h"
#include "sc7a20h.h"
#include "settings.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_WAKE_NONE = 0,
    APP_WAKE_KEY,
    APP_WAKE_PICKUP,
} app_wake_source_t;

/// 登记常开 3V3 上的触摸与加速度计，睡前由本模块送进最低功耗态。
/// Register the always-on touch and accelerometer so sleep paths can park them.
void app_sleep_bind(cst836u_handle_t tp, sc7a20h_handle_t acc);
/// 当前锁屏设置是否走软睡/关机。
/// Whether the stored lock choice powers the host down (soft sleep or off).
bool app_lock_powers_down(void);

void enter_lock_and_sleep(
    EpdiyHighlevelState* hl, int64_t* ignore_until_ms, sc7a20h_handle_t acc
);

/// 等电源键松开，避免进睡瞬间被同一下按住立刻唤醒。
/// Wait for the power key to release so the same press does not wake immediately.
void app_lock_wait_key_idle(int timeout_ms);
/// ESP 浅睡，按键或拿起唤醒。acc 为空则只等按键。
/// ESP light sleep; wake on key or pickup. Key only when acc is NULL.
app_wake_source_t app_light_sleep_wait(sc7a20h_handle_t acc);
app_wake_source_t app_last_wake_source(void);
/// 软睡或关机：停外设、落盘后让 PMU 拉掉 EN。成功不返回；返回即失败，外设已恢复。
/// Soft sleep or power-off: park peripherals, sync, let the PMU drop EN. Does not return on success; a return means failure with peripherals restored.
esp_err_t app_enter_host_sleep(app_sleep_mode_t mode);

#ifdef __cplusplus
}
#endif
