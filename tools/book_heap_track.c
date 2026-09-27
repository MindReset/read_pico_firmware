/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 记录解析器申请的内存峰值并检查释放；不计宿主运行库。/ Track parser allocation peaks and release, excluding host runtime allocations.
 * 冻结：仅主机测试。/ Frozen: Host tests only.
 */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct { void *ptr; size_t size; } allocation_t;
static allocation_t slots[131072];
static size_t live, peak;
static size_t slot(void *p) {
    size_t i = ((uintptr_t)p >> 4) % 131072;
    for (size_t n = 0; n < 131072; ++n, i = (i + 1) % 131072) {
        if (slots[i].ptr == p || !slots[i].ptr) return i;
    }
    abort();
}
static void remove_slot(size_t i) {
    live -= slots[i].size;
    slots[i] = (allocation_t){0};
    for (i = (i + 1) % 131072; slots[i].ptr; i = (i + 1) % 131072) {
        allocation_t move = slots[i]; slots[i] = (allocation_t){0};
        slots[slot(move.ptr)] = move;
    }
}
static void record(void *p, size_t n) {
    if (!p) return;
    size_t i = slot(p);
    live -= slots[i].size;
    slots[i] = (allocation_t){p, n}; live += n;
    if (live > peak) peak = live;
}
void *book_test_malloc(size_t n) { void *p = malloc(n); record(p, n); return p; }
void *book_test_calloc(size_t n, size_t s) { void *p = calloc(n, s); record(p, n*s); return p; }
void *book_test_realloc(void *p, size_t n) {
    size_t i = p ? slot(p) : 0;
    void *next = realloc(p, n);
    if (next || !n) {
        if (p) remove_slot(i);
        record(next, n);
    }
    return next;
}
void __real_free(void *p);
void __wrap_free(void *p) {
    if (p) { size_t i = slot(p); if (slots[i].ptr) remove_slot(i); }
    __real_free(p);
}
void book_heap_reset(void) {
    assert(!live);
    for (size_t i = 0; i < 131072; ++i) slots[i] = (allocation_t){0};
    peak = 0;
}
void book_heap_report(void) {
    printf("parser heap peak=%zu bytes, remaining=%zu\n", peak, live);
    assert(!live);
    assert(peak < 6U * 1024U * 1024U);
}
