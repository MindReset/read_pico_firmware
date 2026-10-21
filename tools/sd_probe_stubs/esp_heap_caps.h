#pragma once
#include <stdlib.h>
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_DMA 2
static inline void *heap_caps_malloc(size_t n, int caps) {(void)caps;return malloc(n);}
static inline void heap_caps_free(void *p) {free(p);}
