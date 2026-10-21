/* SPDX-License-Identifier: Apache-2.0
 * 中文：按索引读取压缩的内建黑体汉字，单块缓存最多 2464 字节，避免整份字体驻留。
 * English: Indexed compressed built-in Han glyphs; a 2464-byte block cache avoids retaining a whole font.
 */
#include "ui_hanzi.h"
#include "esp_heap_caps.h"
#include "miniz.h"
#include <stddef.h>
#include <string.h>
extern const uint8_t ui_hanzi_start[] asm("_binary_ui_hanzi_bin_start");
extern const uint8_t ui_hanzi_end[] asm("_binary_ui_hanzi_bin_end");
#define BLOCK 32u
static uint8_t *s_block;
static tinfl_decompressor *s_inflate;
static size_t s_cached = SIZE_MAX;
static uint32_t word(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static int index_for(uint32_t cp) {
    size_t bytes = ui_hanzi_end - ui_hanzi_start;
    if (cp > 0xffff || bytes < 16 || memcmp(ui_hanzi_start, "PIF1", 4)) return -1;
    size_t count = word(ui_hanzi_start + 4), blocks = (count + BLOCK - 1) / BLOCK;
    if (count > 6763 || word(ui_hanzi_start + 8) != UI_HANZI_BASE_PX || word(ui_hanzi_start + 12) != BLOCK ||
        16 + count * 2 + (blocks + 1) * 4 > bytes) return -1;
    size_t low = 0, high = count;
    const uint8_t *chars = ui_hanzi_start + 16;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        uint32_t candidate = chars[mid * 2] | (uint32_t)chars[mid * 2 + 1] << 8;
        if (candidate < cp) low = mid + 1; else high = mid;
    }
    return low < count && (chars[low * 2] | (uint32_t)chars[low * 2 + 1] << 8) == cp ? (int)low : -1;
}
bool ui_hanzi_has(uint32_t cp) { return index_for(cp) >= 0; }
bool ui_hanzi_get(uint32_t cp, uint8_t record[UI_HANZI_RECORD]) {
    int index = index_for(cp); if (index < 0 || !record) return false;
    size_t block = (unsigned)index / BLOCK;
    if (!s_block) s_block = heap_caps_malloc(BLOCK * UI_HANZI_RECORD, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_inflate) s_inflate = heap_caps_malloc(sizeof(*s_inflate), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_block || !s_inflate) return false;
    if (s_cached != block) {
        size_t count = word(ui_hanzi_start + 4), blocks = (count + BLOCK - 1) / BLOCK;
        const uint8_t *offsets = ui_hanzi_start + 16 + count * 2;
        const uint8_t *data = offsets + (blocks + 1) * 4;
        size_t start = word(offsets + block * 4), end = word(offsets + (block + 1) * 4);
        size_t entries = count - block * BLOCK; if (entries > BLOCK) entries = BLOCK;
        s_cached = SIZE_MAX;
        if (end < start || end > (size_t)(ui_hanzi_end - data)) return false;
        // 解压器也放 PSRAM；便利解压函数的大型栈变量不适合 UI 任务。
        // Keep the inflater in PSRAM too; the convenience decoder's large stack frame is unsuitable for UI tasks.
        tinfl_init(s_inflate);
        size_t input = end - start, output = BLOCK * UI_HANZI_RECORD;
        tinfl_status status = tinfl_decompress(s_inflate, data + start, &input, s_block, s_block, &output,
            TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
        if (status != TINFL_STATUS_DONE || output != entries * UI_HANZI_RECORD || input != end - start) return false;
        s_cached = block;
    }
    memcpy(record, s_block + ((unsigned)index % BLOCK) * UI_HANZI_RECORD, UI_HANZI_RECORD);
    return record[2] > 0 && record[2] <= 24 && record[3] > 0 && record[3] <= 24 && record[4] > 0;
}
