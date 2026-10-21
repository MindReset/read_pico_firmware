/*
 * SPDX-License-Identifier: Apache-2.0
 * 用户要求快刷封面稍微加深：白色混入从1/8降为1/16，仍输出黑白端点，不改变亚克力或刷新次数。
 * User requests slightly darker fast covers: reduce the white lift from 1/8 to 1/16, retaining binary output without changing acrylic or refresh counts.
 */
#include "ui_image_dither.h"

uint8_t ui_image_dither_gray(uint8_t gray, int x, int y) {
    static const uint8_t bayer4[4][4] = {
        { 0,  8,  2, 10},
        {12,  4, 14,  6},
        { 3, 11,  1,  9},
        {15,  7, 13,  5},
    };
    const unsigned base = gray / 17u;
    if (base >= 15u) return 0xff;
    const unsigned remainder = gray - base * 17u;
    const unsigned threshold = bayer4[(unsigned)y & 3u][(unsigned)x & 3u];
    const unsigned bump = remainder * 16u > threshold * 17u ? 1u : 0u;
    return (uint8_t)((base + bump) * 17u);
}

uint8_t ui_image_dither_bw(uint8_t gray, int x, int y) {
    static const uint8_t bayer4[4][4] = {
        {0,8,2,10}, {12,4,14,6}, {3,11,1,9}, {15,7,13,5},
    };
    if (gray == 0) return 0;
    if (gray == 255) return 255;
    return gray > bayer4[(unsigned)y & 3u][(unsigned)x & 3u] * 16u + 8u ? 255 : 0;
}

uint8_t ui_image_dither_cover_bw(uint8_t gray, int x, int y) {
    // 白色混入减半，纯黑区从14/16增至15/16黑点；坐标相位稳定，图标与文字不经过这里。
    // Halve the white lift, raising solid-black density from 14/16 to 15/16 dots; keep phase stable and bypass icons and text.
    const uint8_t lighter=(uint8_t)(gray + (255u-gray+8u)/16u);
    return ui_image_dither_bw(lighter,x,y);
}

uint8_t ui_image_dither_acrylic_bw(uint8_t gray, int x, int y) {
    // 用户要求规则细棋盘格：8×8有序阈值，中间灰每像素黑白交替，其余灰度有序增减密度。
    // User requests a fine regular checkerboard: 8x8 ordered thresholds alternate single pixels at midgray and vary density in order at other grays.
    // 固定绝对坐标相位，无逐帧随机变化，无运行时缓存；黑白端点原样保留。
    // Anchor the phase to absolute coordinates, with no per-frame randomness or runtime cache; preserve black/white endpoints.
    static const uint8_t thresholds[8][8] = {
        { 0,32, 8,40, 2,34,10,42},
        {48,16,56,24,50,18,58,26},
        {12,44, 4,36,14,46, 6,38},
        {60,28,52,20,62,30,54,22},
        { 3,35,11,43, 1,33, 9,41},
        {51,19,59,27,49,17,57,25},
        {15,47, 7,39,13,45, 5,37},
        {63,31,55,23,61,29,53,21},
    };
    if (gray == 0 || gray == 255) return gray;
    const unsigned threshold = thresholds[(unsigned)y & 7u][(unsigned)x & 7u];
    return gray * 128u > (threshold * 2u + 1u) * 255u ? 255 : 0;
}
