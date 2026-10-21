/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：主机堆平台接口替身。/ English: Host heap platform shim.
 * 冻结：仅用于测试。/ Frozen: Tests only.
 */
#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
static inline void *heap_caps_malloc(size_t n, int caps) { (void)caps; return malloc(n); }
static inline void *heap_caps_calloc(size_t n, size_t s, int caps) { (void)caps; return calloc(n,s); }
static inline void *heap_caps_realloc(void *p, size_t n, int caps) { (void)caps; return realloc(p,n); }
static inline void heap_caps_free(void *p) { free(p); }
/// 主机上没有 PSRAM 预算这回事：报一个宽裕的固定值，好让“剩余空间够不够”的判断照常走。
/// There is no PSRAM budget on the host: report a comfortable constant so the "is there room
/// left" checks still take their normal path.
static inline size_t heap_caps_get_free_size(int caps) { (void)caps; return (size_t)16 * 1024 * 1024; }
static inline size_t heap_caps_get_largest_free_block(int caps) { (void)caps; return (size_t)16 * 1024 * 1024; }
