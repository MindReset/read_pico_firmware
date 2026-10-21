/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：系统与阅读使用独立保存的字体；页面边界切换活动字形。
 * English: System and reader fonts persist separately; page boundaries switch the active face.
 */
#pragma once
#include <stdbool.h>
#include "ttf_font.h"

typedef struct {
    char path[TTF_FONT_PATH_MAX];
    bool reading;
} app_lock_font_t;
/// 票根与书架拼贴暂用系统字体；先收齐字体预渲染，解锁后恢复原字体上下文。
/// Temporarily use the system face for tickets and library collages; join page preparation first and restore the context after waking.
void app_font_begin_lock(app_lock_font_t *saved);
void app_font_end_lock(const app_lock_font_t *saved);

bool app_font_activate_system(void);
bool app_font_activate_reading(void);
bool app_font_retry_active(void);
