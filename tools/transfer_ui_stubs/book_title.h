/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：book_title 的宿主替身。set 记录路径与标题——虚拟书回归据此断言标题
 * 覆盖；其余按惰性形态。/ English: host shim for book_title. set records the
 * path and the title, which the virtual-book regression asserts against; the
 * rest keep the inert shape.
 * 冻结：仅供测试。/ Frozen: tests only.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "transfer_ui_test_env.h"

// 标题覆盖的记录缓冲：虚拟书回归据此断言「覆盖成推送标题」。
// Recording buffers for the title override: the virtual-book regression asserts
// "override with the pushed title" against them.
static char test_book_title_path[160], test_book_title[128];

static inline bool book_title_get(const char *path, char *out, size_t cap) {
    (void)path;
    if (cap) out[0] = 0;
    return false;
}

static inline int book_title_set(const char *path, const char *title) {
    if (!path || !title) return -1;
    snprintf(test_book_title_path, sizeof(test_book_title_path), "%s", path);
    snprintf(test_book_title, sizeof(test_book_title), "%s", title);
    return 0;
}
static inline int book_title_clear(const char *path) {(void)path;return 0;}
