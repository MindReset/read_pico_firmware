/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：用主机像素缓冲验证按压背景和刷新区域。
 * English: Verify pressed backgrounds and refresh regions using a host pixel buffer.
 * 冻结：仅用于主机测试。/ Frozen: Host tests only.
 */
#include "ui_kit.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#define W 100
#define H 100
void epd_draw_pixel(int x, int y, uint8_t color, uint8_t *fb) {
    assert(x >= 0 && x < W && y >= 0 && y < H); fb[y * W + x] = color;
}
void epd_fill_rect(EpdRect r, uint8_t color, uint8_t *fb) {
    for (int y = r.y; y < r.y + r.height; ++y)
        for (int x = r.x; x < r.x + r.width; ++x) epd_draw_pixel(x, y, color, fb);
}
void epd_fill_circle_helper(int x, int y, int radius, int corners, int delta, uint8_t color, uint8_t *fb) {
    (void)x; (void)y; (void)radius; (void)corners; (void)delta; (void)color; (void)fb;
}
static void rect_eq(EpdRect r, int x, int y, int w, int h) {
    assert(r.x == x && r.y == y && r.width == w && r.height == h);
}
int main(void) {
    rect_eq(ui_rect_union((EpdRect){0}, (EpdRect){10,20,30,40}), 10,20,30,40);
    rect_eq(ui_rect_union((EpdRect){10,20,30,40}, (EpdRect){5,30,50,10}), 5,20,50,40);
    rect_eq(ui_rect_union((EpdRect){-10,-20,30,40}, (EpdRect){0}), 0,0,20,20);
    rect_eq(ui_rect_union((EpdRect){680,1210,INT_MAX,INT_MAX}, (EpdRect){0}), 680,1210,4,6);
    rect_eq(ui_rect_union((EpdRect){INT_MIN,INT_MIN,10,10}, (EpdRect){0}), 0,0,0,0);
    uint8_t fb[W * H]; memset(fb, UI_GRAY_WHITE, sizeof(fb));
    ui_draw_pressed_round_rect(fb, (EpdRect){10,10,60,40}, 0);
    assert(fb[30 * W + 40] == UI_GRAY_LIGHT);
    assert(fb[30 * W + 14] == UI_GRAY_BLACK && fb[30 * W + 16] == UI_GRAY_BLACK);
    assert(fb[30 * W + 9] == UI_GRAY_WHITE);
    // 按压底图之后绘制黑字像素，文字不反色。/ Draw black text pixels after the pressed background, without inversion.
    epd_draw_pixel(40,30,UI_GRAY_BLACK,fb); assert(fb[30 * W + 40] == UI_GRAY_BLACK);
    puts("ui kit host tests passed");
}
