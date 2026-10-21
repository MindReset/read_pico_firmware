/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：主机显示接口替身。/ English: Host display interface shim.
 * 冻结：仅用于测试。/ Frozen: Tests only.
 */
#pragma once
#include <stdint.h>
typedef struct { int x, y, width, height; } EpdRect;
enum EpdFontFlags { EPD_DRAW_ALIGN_LEFT, EPD_DRAW_ALIGN_CENTER, EPD_DRAW_ALIGN_RIGHT };
enum EpdRotation { EPD_ROT_LANDSCAPE, EPD_ROT_PORTRAIT, EPD_ROT_INVERTED_LANDSCAPE, EPD_ROT_INVERTED_PORTRAIT };
void epd_draw_pixel(int x, int y, uint8_t color, uint8_t *fb);
uint8_t epd_get_pixel(int x, int y, int width, int height, const uint8_t *fb);
void epd_draw_line(int x0, int y0, int x1, int y1, uint8_t color, uint8_t *fb);
void epd_draw_circle(int x, int y, int radius, uint8_t color, uint8_t *fb);
void epd_fill_circle(int x, int y, int radius, uint8_t color, uint8_t *fb);
void epd_fill_rect(EpdRect rect, uint8_t color, uint8_t *fb);
void epd_fill_circle_helper(int x, int y, int radius, int corners, int delta, uint8_t color, uint8_t *fb);
void epd_draw_rotated_image(EpdRect rect, const uint8_t *image, uint8_t *fb);
// 旋转尺寸带头定义：宿主测试默认竖屏 684x1216，链接方无需再提供替身。
// Define the rotation sizes here: host tests default to portrait 684x1216, so linkers
// no longer need a separate shim.
/* 自带动态替身（按旋转/小窗换算尺寸）的测试在 include 前定义此宏关掉本组替身，
   防 static inline 与测试桩重定义（r93 头链把 epdiy.h 拉进更多测试后撞车）。
   Tests that ship their own dynamic shims (rotation/size dependent) define this
   macro before including, avoiding redefinition once r93 header chains pull
   epdiy.h into more test units. */
#ifdef EPDIY_STUB_NO_ROTATED_SIZE
// 关掉替身定义但仍给出声明：同一份测试里其它翻译单元（如 ui_image_dither.c）也要能调用。
// Drop the shim bodies but keep the declarations: other translation units in the same test
// (ui_image_dither.c for one) still call them.
int epd_rotated_display_width(void);
int epd_rotated_display_height(void);
#else
static inline int epd_rotated_display_width(void) { return 684; }
static inline int epd_rotated_display_height(void) { return 1216; }
#endif
int epd_width(void);
int epd_height(void);
enum EpdRotation epd_get_rotation(void);
