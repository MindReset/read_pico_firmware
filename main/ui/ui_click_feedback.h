/* SPDX-License-Identifier: Apache-2.0
 * 中文：共享控件单次按下/复位反馈，不处理导航或后台动画。
 * English: One press/restore feedback for shared controls, without navigation or background animation.
 */
#pragma once
#include "ui_kit.h"
typedef enum { UI_CLICK_NAV, UI_CLICK_BACK, UI_CLICK_BACK_SMALL } ui_click_kind_t;
void ui_click_feedback_reset(void);
/// 仅主循环当前帧可注册；后台阅读预绘制不能改控件表。/ Only the loop's current frame may register; reader prefetch cannot alter controls.
void ui_click_feedback_begin(uint8_t *fb);
/// 最多12控件，一个按压缓存上限4096B PSRAM；OOM仅略过装饰。/ Up to 12 controls, one press cache bounded to 4096 PSRAM bytes; OOM skips decoration only.
void ui_click_feedback_register(uint8_t *fb, EpdRect rect, ui_click_kind_t kind, ui_icon_t icon);
/// 三种主页模式保留底栏灰底，返回实际变化边界，不按缓存方框推屏。/ All three main modes retain footer grays and return actual change bounds instead of the cache rectangle.
bool ui_click_feedback_press(uint8_t *fb, int x, int y, EpdRect *area);
/// 先记录变化边界，再精确恢复完整小控件缓存；不分配额外内存。/ Record change bounds before restoring the complete small cache exactly, without another allocation.
bool ui_click_feedback_release(uint8_t *fb, EpdRect *area);
bool ui_click_feedback_cancel_at(int x, int y);
bool ui_click_feedback_active(void);
