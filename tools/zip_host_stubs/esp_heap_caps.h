/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 主机堆替身。/ Host allocation shim.
 * 冻结：不用于固件。/ Frozen: Not used by firmware.
 */
#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
static inline void* heap_caps_malloc(size_t n, int caps) { (void)caps; return malloc(n); }
static inline void* heap_caps_calloc(size_t n, size_t size, int caps) { (void)caps; return calloc(n, size); }
