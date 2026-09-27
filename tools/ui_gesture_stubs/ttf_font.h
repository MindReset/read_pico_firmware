/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：主机字体接口替身。/ English: Host font interface shim.
 * 冻结：仅用于测试。/ Frozen: Tests only.
 */
#pragma once
#include <stdbool.h>
#include "epdiy.h"
bool ttf_font_ready(void);
int ttf_ascender_px(int px);
void ttf_measure_line_px(int px, const char *text, int *above, int *below);
void ttf_draw_text_px(uint8_t *fb, int x, int y, int px, const char *text, enum EpdFontFlags align, uint8_t fg, uint8_t bg);
void ttf_draw_text_px_bw(uint8_t *fb, int x, int y, int px, const char *text, enum EpdFontFlags align, uint8_t fg, uint8_t bg);
