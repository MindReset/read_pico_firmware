/* SPDX-License-Identifier: Apache-2.0
 * 宿主端替身：PSRAM 分配直接落到普通堆，但保留一个「可注入失败」的开关，
 * 用来验证分配失败时代码会退化而不是崩。
 * Host stub: PSRAM allocation falls through to the plain heap, with an
 * injectable-failure switch so the degradation path can be tested.
 */
#pragma once
#include <stdlib.h>

#define MALLOC_CAP_SPIRAM (1 << 0)
#define MALLOC_CAP_8BIT (1 << 1)
#define MALLOC_CAP_INTERNAL (1 << 2)

extern int vocab_test_alloc_fail; /* >0 时下一次分配返回 NULL / when >0 the next alloc fails */

static inline void* heap_caps_malloc(size_t n, int caps) {
    (void)caps;
    if (vocab_test_alloc_fail > 0) { --vocab_test_alloc_fail; return NULL; }
    return malloc(n);
}
static inline void* heap_caps_calloc(size_t n, size_t size, int caps) {
    (void)caps;
    if (vocab_test_alloc_fail > 0) { --vocab_test_alloc_fail; return NULL; }
    return calloc(n, size);
}
static inline void heap_caps_free(void* p) { free(p); }
