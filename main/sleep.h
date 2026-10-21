/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 锁屏、浅睡等待、软睡/关机下电。
 *
 * Lock, light-sleep wait, and soft-sleep / power-off rail drop.
 */

#pragma once

#include <stdint.h>

#include "epd_highlevel.h"
#include "esp_err.h"
#include "sc7a20h.h"
#include "cst836u.h"
#include "settings.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_WAKE_NONE = 0,
    APP_WAKE_KEY,
    APP_WAKE_PICKUP,
    APP_WAKE_TIMEOUT,
} app_wake_source_t;

/// 登记常开供电的触摸与加速度计，并准备 CPU 浅睡保持区。/ Bind always-on touch/accelerometer and prepare CPU light-sleep retention.
void app_sleep_bind(cst836u_handle_t tp, sc7a20h_handle_t acc);

void enter_lock_and_sleep(
    EpdiyHighlevelState* hl, int64_t* ignore_until_ms, sc7a20h_handle_t acc, cst836u_handle_t touch,
    bool reader_background
);

/// 主动拼贴锁屏在保存进度/切字体前清白并显示圆锁；不适用于自动休眠、直接关机或升级/上传中。
/// Show white and a lock badge before manual-lock saves/font changes; not for idle sleep, direct shutdown or active OTA/uploads.
void app_lock_feedback(EpdiyHighlevelState *hl);

/// 在消费恢复/开书前进行冷启动与深睡密码验证。/ Gate cold/deep starts before consuming resume or opening books.
void app_lock_boot_gate(EpdiyHighlevelState *hl, cst836u_handle_t touch, sc7a20h_handle_t acc);

/// 等电源键松开，避免进睡瞬间被同一下按住立刻唤醒。
/// Wait for the power key to release so the same press does not wake immediately.
void app_lock_wait_key_idle(int timeout_ms);
/// ESP 浅睡，按键或拿起唤醒。acc 为空则只等按键。
/// ESP light sleep; wake on key or pickup. Key only when acc is NULL.
app_wake_source_t app_light_sleep_wait(sc7a20h_handle_t acc);
/// ESP 浅睡至按键、拿起或超时；超时以毫秒计，0 表示不设期限。
/// ESP light sleep until key, pickup, or timeout; 0 means no deadline.
app_wake_source_t app_light_sleep_wait_timed(sc7a20h_handle_t acc, uint32_t timeout_ms);
app_wake_source_t app_last_wake_source(void);
/// 软睡/关机前停用外设并落盘；成功不返回，交接失败恢复外设并返回错误。保留调用方的阅读/密码上下文。
/// Park peripherals and sync before host sleep/off; success does not return, failure restores peripherals and returns an error with reader/PIN context intact.
esp_err_t app_enter_host_sleep(app_sleep_mode_t mode);
/// 将当前系统时间写入常供电 PMU RTC；主控断电后由 PMU 继续走时。
/// Sync system time to the always-on PMU RTC so it keeps ticking while the host is off.
esp_err_t app_sync_time_to_pmu(void);
/// 让 PMU 正常重启主控；先保存 RTC，调用后不会返回。
/// Ask the PMU for a normal host restart after preserving RTC; does not return.
void app_restart_host(void);

#ifdef __cplusplus
}
#endif
