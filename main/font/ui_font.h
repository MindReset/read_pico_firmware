/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：独立的内建无衬线界面字形，不随阅读正文字体切换。
 * English: Separate built-in sans UI glyphs, unaffected by reading-font changes.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "epdiy.h"

bool ui_font_has_text(const char *text);
int ui_font_ascender_px(int px);
int ui_font_text_width_px(int px, const char *text);
/// 含补充位图的标题整行使用原生24px；轮廓字形直接按目标字号生成。
/// Titles containing bitmap supplements use native 24px throughout; outlines rasterize at the target size.
int ui_font_title_px(int preferred_px, const char *text);
/// 标题统一使用黑色笔画；补充位图不插值，轮廓按相同覆盖率门限绘制。
/// Titles use solid black strokes, unscaled supplements and matching outline coverage thresholds.
void ui_font_draw_title_px(uint8_t *fb, int x, int baseline, int px,
                          const char *text, enum EpdFontFlags align);
/// 以实际字形测量基线上下范围，不分配位图。/ Measure actual glyph extents above/below baseline without bitmap allocation.
void ui_font_measure_line_px(int px, const char *text, int *above, int *below);
void ui_font_draw_text_px(uint8_t *fb, int x, int baseline, int px,
                          const char *text, enum EpdFontFlags align,
                          uint8_t fg, uint8_t bg, bool black_white);
