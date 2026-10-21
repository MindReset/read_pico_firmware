/* SPDX-License-Identifier: Apache-2.0
 * 中文：四位密码的纯绘制与手势状态；背景只在进入时生成。
 * English: Pure four-digit pad painting and gestures; build the backdrop only on entry.
 */
#pragma once
#include "epdiy.h"
#include "ui_gesture.h"
typedef struct {
    uint8_t *background;
    char digits[5], title[40], notice[80];
    unsigned count;
    int pressed, dirty_key;
    bool blocked;
} ui_pinpad_t;
typedef enum { UI_PIN_NONE, UI_PIN_CHANGED, UI_PIN_CANCEL, UI_PIN_COMPLETE } ui_pin_result_t;
/// 缓存实际画面的模糊亚克力；内存不足仍可使用中性背景。/ Cache frosted actual art; low memory retains a usable neutral backdrop.
void ui_pinpad_begin(ui_pinpad_t *pad, const uint8_t *frame, const char *title);
/// 清零输入和释放缓存。/ Wipe input and release the cache.
void ui_pinpad_end(ui_pinpad_t *pad);
/// 清输入并更换提示，不重建背景。/ Clear input and change copy without rebuilding the backdrop.
void ui_pinpad_reset(ui_pinpad_t *pad, const char *title, const char *notice);
/// 纯绘制；area只恢复并绘制指定区域。/ Pure painting; restore and paint only the requested area.
void ui_pinpad_paint(uint8_t *frame, const ui_pinpad_t *pad, EpdRect area);
/// 输入反馈只重画圆内环和密码圆点；不处理背景或字形。/ Input feedback repaints only inner rings and dots, never artwork or glyphs.
void ui_pinpad_paint_input(uint8_t *frame, const ui_pinpad_t *pad);
/// 单次有效释放提交一个数字；滑出、多指、错误或长按取消。/ A valid release commits one digit; outside, multitouch, errors and holds cancel.
ui_pin_result_t ui_pinpad_handle(ui_pinpad_t *pad, const ui_gesture_event_t *event, EpdRect *dirty);
/// 原生背景/图形区域。/ Native backdrop and control bounds.
EpdRect ui_pinpad_full(void);
EpdRect ui_pinpad_entry_area(void);
