/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>

// 两个相邻像素形态间插值，每档1/16像素，保留字宽及字形比例。
// Interpolate adjacent pixel morphs in 1/16-pixel steps, preserving advances and glyph proportions.
// 每边留ceil(abs(steps)/16)像素；源目标不能重叠。/ Pad by ceil(abs(steps)/16); source and destination must not overlap.
static inline void ttf_stroke_filter_fine(uint8_t *dst, const uint8_t *src, int width, int height,
                              int px, int steps) {
    (void)px;
    if(steps < -32)steps=-32;
    if(steps > 32)steps=32;
    int amount=steps<0?-steps:steps,radius=(amount+15)/16,inner=amount/16,fraction=amount%16;
    int stride = width + 2*radius;
    for (int y = -radius; y < height+radius; ++y) {
        for (int x = -radius; x < width+radius; ++x) {
            int original = x >= 0 && x < width && y >= 0 && y < height
                ? src[y * width + x] : 0;
            int extreme = original, near = original;
            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    int nx = x + dx, ny = y + dy;
                    int value = nx >= 0 && nx < width && ny >= 0 && ny < height
                        ? src[ny * width + nx] : 0;
                    if (steps > 0 ? value > extreme : value < extreme) extreme = value;
                    if(dx>=-inner&&dx<=inner&&dy>=-inner&&dy<=inner &&
                        (steps > 0 ? value > near : value < near))near=value;
                }
            }
            dst[(y + radius) * stride + x + radius] = (uint8_t)((near*(16-fraction)+extreme*fraction+8)/16);
        }
    }
}
static inline void ttf_stroke_filter(uint8_t *dst,const uint8_t *src,int width,int height,int px,int level){
    if(level < -2)level=-2;
    if(level > 2)level=2;
    ttf_stroke_filter_fine(dst,src,width,height,px,level*16);
}
