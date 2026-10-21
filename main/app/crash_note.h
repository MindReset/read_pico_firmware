/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：崩溃现场的读取接口。记录由 panic 包装器写进 RTC，这里只负责读出来给屏显和串口。
 *
 * English: Reading side of the panic scene. The panic wrapper writes it into RTC; this only reads
 * it back for the screen and the log.
 */

#pragma once

#include <stdbool.h>

/// 是否有上一次崩溃的记录。/ Whether a crash from the previous run was recorded.
bool crash_note_valid(void);

/// 一行描述，形如 "panic pc=0x4201a2b4 exc=4 core=0 ..."；没有记录时返回 NULL。
/// 地址可以直接交给 addr2line 在固件 elf 上解析出函数与行号。
/// One-line description such as "panic pc=0x4201a2b4 exc=4 core=0 ..."; NULL without a record.
/// The address can go straight to addr2line against the firmware elf for a function and a line.
const char *crash_note_text(void);

/// 把记录打一遍（不清除）。启动时调用一次。/ Log the record once, without clearing it. Call at boot.
void crash_note_log(void);
