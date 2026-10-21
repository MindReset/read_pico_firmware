#pragma once
#include <stddef.h>
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
extern int water_test_alloc_fail;
static inline void* heap_caps_aligned_alloc(size_t alignment, size_t size, unsigned caps) {
    (void)caps;
    if (water_test_alloc_fail) return NULL;
    return aligned_alloc(alignment, size);
}
static inline void heap_caps_free(void* ptr) { free(ptr); }
