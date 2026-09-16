/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 全屏菜单和屏幕下方那条触摸按键区。菜单条目从 app 注册表取，不自己维护表。
 *
 * Full-screen menu and the touch-key strip below the display. Items come
 * from the app registry; this file does not keep its own table.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_MENU_ITEMS_PER_PAGE 8
#define UI_MENU_HIT_NONE (-1)
#define UI_MENU_HIT_PREV (-2)
#define UI_MENU_HIT_NEXT (-3)

// 触摸面板比显示区长，屏幕下方那条感应区里有三个按键。它们不在画面里，
// 没有东西可画，只能按坐标识别：实测中心 x=80/240/400，y≈1500。
// 显示区最大 y 是 1216，所以用 1300 当分界就不会和底部标签栏抢点击。
// The touch panel is taller than the display; three keys sit in the strip
// below the image. They are not drawn; hit-test is by coordinate. Measured
// centers are x=80/240/400, y≈1500. Display y max is 1216, so 1300 as the
// split does not steal taps from the bottom bar.
#define UI_KEY_COUNT 3
#define UI_KEY_AREA_TOP 1300
#define UI_KEY_PITCH 160
#define UI_KEY_1 0
#define UI_KEY_2 1
#define UI_KEY_3 2

/// 底栏最右那格的菜单把手，箭头向上表示能打开菜单。所有页都要画。
/// Menu handle in the rightmost bar cell; an up arrow means the menu can open. Every page draws it.
void ui_draw_menu_handle(uint8_t* framebuffer, bool menu_open);
bool ui_menu_handle_hit_test(uint16_t x, uint16_t y);

void ui_draw_menu_page(uint8_t* framebuffer, const app_desc_t* current, int leaf);
/// 命中条目时返回它在注册表里的下标，翻页返回 UI_MENU_HIT_PREV / NEXT。
/// Returns the registry index on a row hit, or UI_MENU_HIT_PREV / NEXT for paging.
int ui_menu_hit_test(uint16_t x, uint16_t y, int leaf);
int ui_menu_leaf_count(void);
int ui_menu_leaf_for_app(const app_desc_t* app);

/// 屏幕下方触摸按键区的命中测试，返回 UI_KEY_1/2/3，不在按键区时返回 -1。
/// Hit-test for the keys below the display. Returns UI_KEY_1/2/3, or -1 outside the key strip.
int ui_key_hit_test(uint16_t x, uint16_t y);

#ifdef __cplusplus
}
#endif
