/* SPDX-License-Identifier: Apache-2.0
 * 中文：补充汉字按像素中心作覆盖率插值；沿用字框和字宽，不增加字库或缓存。
 * English: Interpolate supplemental Han coverage at pixel centers, retaining metrics without enlarging the dictionary or cache.
 */
#pragma once
#include "ui_hanzi.h"
static inline unsigned ui_hanzi_bit(const uint8_t *record, int x, int y) {
    if (x < 0 || y < 0 || x >= record[2] || y >= record[3]) return 0;
    unsigned bit = (unsigned)y * UI_HANZI_BASE_PX + (unsigned)x;
    return (record[5 + bit / 8] >> (bit & 7)) & 1u;
}
static inline uint8_t ui_hanzi_coverage(const uint8_t *record, int width, int height, int x, int y) {
    int sx = ((2 * x + 1) * record[2] * 256) / (2 * width) - 128;
    int sy = ((2 * y + 1) * record[3] * 256) / (2 * height) - 128;
    int ix = sx >= 0 ? sx / 256 : -((-sx + 255) / 256);
    int iy = sy >= 0 ? sy / 256 : -((-sy + 255) / 256);
    unsigned fx = (unsigned)(sx - ix * 256), fy = (unsigned)(sy - iy * 256);
    unsigned coverage = ui_hanzi_bit(record, ix, iy) * (256 - fx) * (256 - fy)
        + ui_hanzi_bit(record, ix + 1, iy) * fx * (256 - fy)
        + ui_hanzi_bit(record, ix, iy + 1) * (256 - fx) * fy
        + ui_hanzi_bit(record, ix + 1, iy + 1) * fx * fy;
    return (uint8_t)((coverage * 255 + 32768) / 65536);
}
