/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：墨水屏单行输入框，常亮光标、长文字滚动与左右定位。
 * English: Single-line e-paper input fields with a steady caret, overflow scrolling and positioning.
 */
#pragma once
#include <stdint.h>
#include "epdiy.h"
#include "ui_text_edit.h"

/// 纯绘制，不闪烁或启动计时器；suffix 为不可编辑扩展名，缓冲区最多 128 字节。
/// Paint only, without blinking or timers; suffix is a read-only extension, with at most 128 text bytes.
void ui_text_input_draw(uint8_t *fb, const ui_text_edit_t *edit, EpdRect field,
                        int px, bool masked, const char *suffix);
/// 轻点文字定位，左右箭头移动光标；返回是否命中输入框。
/// Tap text to position the caret or arrows to move it; return whether the field was hit.
bool ui_text_input_tap(ui_text_edit_t *edit, EpdRect field, int px, bool masked,
                       const char *suffix, int x, int y);
/// 拼音组字行同样显示常亮光标。/ Show a steady caret on the Pinyin composition line too.
void ui_text_input_composition(uint8_t *fb, const char *text, EpdRect line, int px);
