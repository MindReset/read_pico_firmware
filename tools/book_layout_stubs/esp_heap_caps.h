/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：宿主布局测试的内存适配。/ English: Memory adapter for host layout tests.
 * 冻结：仅供测试。/ Frozen: Tests only.
 */
#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
static inline void* heap_caps_malloc(size_t n, int caps) { (void)caps; return malloc(n); }
static inline void* heap_caps_realloc(void* p, size_t n, int caps) { (void)caps; return realloc(p, n); }
