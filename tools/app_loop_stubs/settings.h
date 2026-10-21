/* 中文：调度测试替身。/ English: Scheduler test shim. */
#pragma once
#include "common.h"

bool app_settings_ble_turner(void);
void app_settings_set_ble_turner(bool enabled);

uint8_t app_settings_auto_lock_minutes(void);

uint8_t app_settings_system_contrast(void);

/* 中文：飞行模式在该调度测试里恒为关；真实读写在 main/settings.c。
   English: Flight mode is always off in this scheduler test; the real getter lives in main/settings.c. */
static inline bool app_settings_flight_mode(void) { return false; }
