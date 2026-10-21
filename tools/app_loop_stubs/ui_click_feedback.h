/* SPDX-License-Identifier: Apache-2.0
 * 中文：调度器的装饰绘制替身。/ English: Decoration shim for scheduler tests.
 */
#pragma once
#include "common.h"
void ui_click_feedback_reset(void);
void ui_click_feedback_begin(uint8_t *fb);
bool ui_click_feedback_active(void);
bool ui_click_feedback_release(uint8_t *fb,EpdRect *rect);
bool ui_click_feedback_press(uint8_t *fb,int x,int y,EpdRect *rect);
bool ui_click_feedback_cancel_at(int x,int y);
