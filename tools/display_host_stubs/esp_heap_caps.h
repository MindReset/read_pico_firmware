/* SPDX-License-Identifier: Apache-2.0
 * 中文：可选显示缓存的主机内存边界。/ English: Host allocation boundary for optional display snapshots.
 */
#pragma once
#include <stddef.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
void* heap_caps_aligned_alloc(size_t alignment, size_t bytes, unsigned caps);
void heap_caps_free(void* allocation);
