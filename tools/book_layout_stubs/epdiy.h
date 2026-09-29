/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：宿主布局测试的显示类型。/ English: Display types for host layout tests.
 * 冻结：仅供测试。/ Frozen: Tests only.
 */
#pragma once
#include <stdint.h>
typedef struct { int x, y, width, height; } EpdRect;
enum EpdFontFlags { EPD_DRAW_ALIGN_LEFT = 0, EPD_DRAW_ALIGN_CENTER = 1 };
void epd_draw_pixel(int x, int y, uint8_t color, uint8_t* framebuffer);
