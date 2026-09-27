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
void epd_fill_rect(EpdRect rect, uint8_t color, uint8_t *fb);
void epd_fill_circle_helper(int x, int y, int radius, int corners, int delta, uint8_t color, uint8_t *fb);
void epd_draw_rotated_image(EpdRect rect, const uint8_t *image, uint8_t *fb);
int epd_rotated_display_width(void);
int epd_rotated_display_height(void);
int epd_width(void);
int epd_height(void);
enum EpdRotation epd_get_rotation(void);
