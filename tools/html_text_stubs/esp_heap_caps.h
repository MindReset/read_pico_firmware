/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：HTML 宿主测试分配器。/ English: Allocator for HTML host tests.
 * 冻结：仅供测试。/ Frozen: Tests only.
 */
#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
extern int html_test_fail_after;
static inline void* heap_caps_realloc(void* p, size_t n, int caps) {
    (void)caps;
    if (html_test_fail_after == 0) return NULL;
    if (html_test_fail_after > 0) html_test_fail_after--;
    return realloc(p, n);
}
