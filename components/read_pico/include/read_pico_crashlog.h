/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：崩溃日志。最近若干条带时间戳的短文本留在 RTC 内存里，软复位不清它，
 * 于是崩溃后的第一次开机能把这些行原样列在屏幕上。这台设备没有随手可用的串口，
 * 崩溃前发生了什么只有这里说得清。面包屑只回答"最后一步是哪个事件"，这里回答"那之前都发生了什么"。
 *
 * English: Crash log. The last few timestamped short lines live in RTC memory, which a soft reset
 * leaves alone, so the first boot after a crash can list them on screen. This board has no
 * convenient console, and only this says what happened before a crash: the crumbs answer which
 * event came last, this answers what led up to it.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

/// 保留的行数。/ Number of lines kept.
#define RP_CRASH_LOG_LINES 24

/// 写一条日志（自动加时间戳前缀）。会挤掉最旧的一条。
/// Write one line, with a timestamp prefix added. The oldest line is pushed out.
void rp_crash_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/// 可读的行数（最多 RP_CRASH_LOG_LINES）。/ Lines available, at most RP_CRASH_LOG_LINES.
uint8_t rp_crash_log_count(void);

/// 第 index 行（0 是最新），越界返回 NULL。/ Line `index`, 0 being the newest; NULL when out of range.
const char *rp_crash_log_line(uint8_t index);

/// 总行数，单调递增：界面靠它判断有没有新的一行。/ Total lines written; the UI uses it to spot new ones.
uint32_t rp_crash_log_serial(void);

/// 清空（用户看过之后）。/ Clear, once the user has seen it.
void rp_crash_log_clear(void);

/// 记一条开机分段计时，形如 "boot splash 1234ms"（毫秒为开机以来的时间）。
/// 首帧标记之后忽略调用，普通回首页不会挤掉开机诊断。/ Ignore calls after the first frame; normal Home visits do not evict boot diagnostics.
/// 这台设备没有随身串口，开机慢在哪一段只能靠这几行说清楚。
/// Record one boot-phase mark, such as "boot splash 1234ms" (milliseconds since power-on). This
/// board has no console at hand, and these lines are how a slow boot says which phase it was in.
void rp_boot_mark(const char *name);
