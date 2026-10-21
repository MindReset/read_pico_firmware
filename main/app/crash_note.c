/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：崩溃现场。panic 时把异常类型、触发地址与描述写进 RTC 内存，下一次开机由首页显示
 * 并由串口打一遍。这台设备没有随手可用的串口，崩溃原因没有别的地方可以留。
 * 写记录只用整数赋值：panic 上下文里不能指望 printf 或分配。
 *
 * English: The panic scene. On a panic the exception type, faulting address and description go
 * into RTC memory, and the next boot shows them on the home screen and logs them once. This board
 * has no convenient console, and a crash has nowhere else to be remembered. Only integer
 * assignments happen here: the panic context cannot rely on printf or allocation.
 */

#include "crash_note.h"

#include <stdio.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_private/panic_internal.h"

static const char *TAG = "crash";

// RTC 内存跨软复位保留，所以崩溃后的第一次开机读到的就是上一次的现场。
// RTC memory survives a soft reset, so the first boot after a crash reads the previous scene.
typedef struct {
    uint32_t magic;
    uint32_t addr;         // 触发异常的指令地址 / instruction address that faulted
    uint32_t description;  // 描述字符串，在 flash 里，重启后仍有效 / description string, in flash
    uint32_t exception;
    uint32_t core;
} crash_note_t;

#define CRASH_NOTE_MAGIC 0x434E4F54u  // "CNOT"
RTC_NOINIT_ATTR static crash_note_t s_note;

// 链接器由 --wrap 提供：指向被包住的那个原始实现。
// Provided by the linker through --wrap: the original implementation being wrapped.
void __real_esp_panic_handler(panic_info_t *info);

// 包住 IDF 的 panic 处理器：先记现场，再交给原来的实现去打印与重启。
// Wraps the IDF panic handler: record the scene first, then let the original print and restart.
void __wrap_esp_panic_handler(panic_info_t *info) {
    if (info != NULL) {
        s_note.addr = (uint32_t)(uintptr_t)info->addr;
        s_note.description = (uint32_t)(uintptr_t)info->description;
        s_note.exception = (uint32_t)info->exception;
        s_note.core = (uint32_t)info->core;
        // magic 最后写：它落地才算这条记录完整。
        // The magic goes last: only once it lands is the record complete.
        s_note.magic = CRASH_NOTE_MAGIC;
    }
    __real_esp_panic_handler(info);
}

bool crash_note_valid(void) { return s_note.magic == CRASH_NOTE_MAGIC; }

const char *crash_note_text(void) {
    static char line[112];
    if (!crash_note_valid()) return NULL;
    snprintf(line, sizeof(line), "panic pc=0x%08lx exc=%lu core=%lu %s",
             (unsigned long)s_note.addr, (unsigned long)s_note.exception, (unsigned long)s_note.core,
             s_note.description != 0 ? (const char *)(uintptr_t)s_note.description : "");
    return line;
}

void crash_note_log(void) {
    const char *line = crash_note_text();
    if (line != NULL) ESP_LOGE(TAG, "%s", line);
}
