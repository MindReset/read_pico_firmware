/* SPDX-License-Identifier: Apache-2.0
 * 中文：顶部快捷菜单的独立手势和纯绘图；动作由主循环执行。
 * English: Independent gesture recognition and pure drawing for the top quick layer; the loop owns actions.
 * 冻结：仅边缘起手；无连续动画、模糊、分配或缓存，下层重绘后只合成一次灰阶；用户要求三个模式均取消点阵。
 * Frozen: Edge-origin capture only; no continuous animation, blur, allocations or caches. Compose grayscale once over a freshly rendered underlay, per the user's revision for all modes.
 */
#pragma once
#include "app.h"

#define UI_QUICK_EDGE_PX 32
#define UI_QUICK_SWIPE_PX 72
#define UI_QUICK_HEIGHT 264
#define UI_QUICK_CIRCLE_Y 156
#define UI_QUICK_RADIUS 54
#define UI_QUICK_HIT_RADIUS 66

typedef enum {
    UI_QUICK_NONE, UI_QUICK_OPEN, UI_QUICK_CLOSE,
    UI_QUICK_WIFI, UI_QUICK_BLUETOOTH, UI_QUICK_FULL, UI_QUICK_LOCK,
} ui_quick_action_t;

typedef struct {
    bool open, tracking, swallow, moved;
    uint16_t x0, y0;
    int hit;
    int64_t started_ms;
} ui_quick_menu_t;

/// 清除层和手势；切页、睡眠、媒体失效时使用。/ Clear layer and contact state at navigation, sleep and media-loss boundaries.
void ui_quick_menu_reset(ui_quick_menu_t *menu);
/// 返回true表示整条触摸序列被接管；多指/读错不执行按钮，收起之后仍吞到抬手。
/// True owns the whole contact stream; multitouch/read errors never activate controls, and dismissal still swallows until release.
bool ui_quick_menu_feed(ui_quick_menu_t *menu, const app_ctx_t *ctx, bool allowed,
                       ui_quick_action_t *action);
/// 取消接触但保留已打开的层。/ Cancel a contact while retaining an already open layer.
void ui_quick_menu_cancel_input(ui_quick_menu_t *menu);
/// 只覆盖顶部，保留下层其他像素；44px图标与底栏相同，状态栏正常绘制。
/// Cover the top only, leaving other pixels intact; use the same 44px icon size as navigation and draw the normal status bar.
void ui_quick_menu_draw(uint8_t *fb, bool wifi, bool bluetooth);
EpdRect ui_quick_menu_rect(void);
