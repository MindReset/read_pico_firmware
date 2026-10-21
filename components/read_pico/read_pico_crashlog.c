/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：崩溃日志的实现。RTC 内存跨软复位保留，所以这些行在崩溃重启后还在；
 * 冷启动（断电）时 magic 不匹配，视为空。
 *
 * English: Crash log implementation. RTC memory survives a soft reset, so the lines are still
 * there after a crash restart; on a cold boot the magic does not match and the log reads empty.
 */

#include "read_pico_crashlog.h"

#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// 一行含时间戳（"12.345 "）与正文。40 字节装得下"connect/link up/subscribe"这类短标记。
// A line holds a timestamp ("12.345 ") and the text. Forty bytes fits short markers such as
// connect, link up and subscribe.
#define RP_CRASH_LOG_LEN 40
#define RP_CRASH_LOG_MAGIC 0x524C4F47u  // "RLOG"

RTC_NOINIT_ATTR static char s_lines[RP_CRASH_LOG_LINES][RP_CRASH_LOG_LEN];
RTC_NOINIT_ATTR static uint32_t s_written;
RTC_NOINIT_ATTR static uint32_t s_magic;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
// 首屏后停止开机标记，普通回首页不挤占诊断轨迹；无需额外 RTC 存储。
// Stop boot marks after the first frame so normal Home visits do not evict diagnostics; no extra RTC storage.
static bool s_boot_marks_done;

void rp_crash_log(const char *fmt, ...) {
    char line[RP_CRASH_LOG_LEN];
    const uint32_t ms = (uint32_t)(esp_timer_get_time() / 1000);
    int used = snprintf(line, sizeof(line), "%lu.%03lu ", (unsigned long)(ms / 1000),
                        (unsigned long)(ms % 1000));
    if (used < 0) used = 0;
    if ((size_t)used < sizeof(line)) {
        va_list args;
        va_start(args, fmt);
        vsnprintf(line + used, sizeof(line) - (size_t)used, fmt, args);
        va_end(args);
    }
    // 时间戳在 panic 之外的上下文里取；这里不做分配，也不碰文件系统。
    // The timestamp comes from outside the panic context; nothing here allocates or touches the FS.
    portENTER_CRITICAL(&s_lock);
    if (s_magic != RP_CRASH_LOG_MAGIC) {
        memset(s_lines, 0, sizeof(s_lines));
        s_written = 0;
        s_magic = RP_CRASH_LOG_MAGIC;
    }
    memcpy(s_lines[s_written % RP_CRASH_LOG_LINES], line, sizeof(s_lines[0]));
    ++s_written;
    portEXIT_CRITICAL(&s_lock);
}

uint8_t rp_crash_log_count(void) {
    if (s_magic != RP_CRASH_LOG_MAGIC || s_written == 0) return 0;
    return s_written < RP_CRASH_LOG_LINES ? (uint8_t)s_written : RP_CRASH_LOG_LINES;
}

const char *rp_crash_log_line(uint8_t index) {
    if (index >= rp_crash_log_count()) return NULL;
    return s_lines[(s_written - 1 - index) % RP_CRASH_LOG_LINES];
}

uint32_t rp_crash_log_serial(void) { return s_magic == RP_CRASH_LOG_MAGIC ? s_written : 0; }

void rp_crash_log_clear(void) {
    portENTER_CRITICAL(&s_lock);
    s_magic = 0;
    s_written = 0;
    portEXIT_CRITICAL(&s_lock);
}

void rp_boot_mark(const char *name) {
    if (s_boot_marks_done) return;
    rp_crash_log("boot %s %ums", name ? name : "?", (unsigned)(esp_timer_get_time() / 1000));
    if (name && strcmp(name, "frame") == 0) s_boot_marks_done = true;
}
