/* SPDX-License-Identifier: Apache-2.0
 * 中文：顶部边缘识别器只产出快捷动作，不访问页面、硬件或刷新。
 * English: The top-edge recognizer emits quick actions without accessing pages, hardware or presentation.
 */
#include "ui_quick_menu.h"
#include <stdlib.h>
#include <string.h>
#include "ui_kit.h"
#include "ui_menu.h"

static int center_x(int button) { return (button * 2 + 1) * UI_LOCK_WIDTH / 8; }

EpdRect ui_quick_menu_rect(void) { return (EpdRect){0, 0, UI_LOCK_WIDTH, UI_QUICK_HEIGHT}; }

void ui_quick_menu_reset(ui_quick_menu_t *menu) { memset(menu, 0, sizeof(*menu)); }

void ui_quick_menu_cancel_input(ui_quick_menu_t *menu) {
    menu->tracking = false;
    menu->swallow = true;
}

static void dismiss(ui_quick_menu_t *menu, ui_quick_action_t *action) {
    menu->open = menu->tracking = false;
    menu->swallow = true;
    *action = UI_QUICK_CLOSE;
}

static int hit_test(int x, int y) {
    for (int i = 0; i < 4; ++i) {
        const int dx = x - center_x(i), dy = y - UI_QUICK_CIRCLE_Y;
        if (dx * dx + dy * dy <= UI_QUICK_HIT_RADIUS * UI_QUICK_HIT_RADIUS) return i;
    }
    return -1;
}

bool ui_quick_menu_feed(ui_quick_menu_t *menu, const app_ctx_t *ctx, bool allowed,
                       ui_quick_action_t *action) {
    *action = UI_QUICK_NONE;
    if (!allowed) {
        bool owned = menu->tracking || menu->swallow || menu->open;
        if (menu->open) dismiss(menu, action);
        else ui_quick_menu_reset(menu);
        return owned;
    }
    if (!ctx->touch || (ctx->touch->touched && ctx->touch->count != 1)) {
        bool owned = menu->open || menu->tracking || menu->swallow;
        if (owned) ui_quick_menu_cancel_input(menu);
        return owned;
    }
    if (menu->swallow) {
        if (!ctx->touch->touched) menu->swallow = false;
        return true;
    }
    int x = ctx->touch->x, y = ctx->touch->y;
    if (ctx->pressed && ctx->touch->touched) {
        if (!menu->open && y > UI_QUICK_EDGE_PX) return false;
        if (menu->open && ui_key_hit_test(x, y) >= 0) {
            dismiss(menu, action);
            return true;
        }
        menu->tracking = true;
        menu->moved = false;
        menu->x0 = x; menu->y0 = y;
        menu->started_ms = ctx->now_ms;
        menu->hit = hit_test(x, y);
    }
    if (!menu->tracking) return menu->open;
    int dx = x - (int)menu->x0, dy = y - (int)menu->y0;
    if (ctx->now_ms < menu->started_ms) {
        ui_quick_menu_cancel_input(menu);
        return true;
    }
    if (abs(dx) > UI_TOUCH_SLOP_PX || abs(dy) > UI_TOUCH_SLOP_PX) menu->moved = true;
    if (!menu->open && dy >= UI_QUICK_SWIPE_PX && dy >= abs(dx) * 2 &&
        ctx->now_ms - menu->started_ms <= 1500) {
        menu->open = true;
        menu->tracking = false;
        menu->swallow = true;
        *action = UI_QUICK_OPEN;
    } else if (menu->open && dy <= -UI_QUICK_SWIPE_PX && -dy >= abs(dx) * 2) {
        dismiss(menu, action);
    } else if (ctx->released) {
        menu->tracking = false;
        if (menu->open && !menu->moved) {
            if (menu->hit >= 0 && hit_test(x, y) == menu->hit) {
                static const ui_quick_action_t actions[] = {
                    UI_QUICK_WIFI, UI_QUICK_BLUETOOTH, UI_QUICK_FULL, UI_QUICK_LOCK,
                };
                *action = actions[menu->hit];
            } else if (menu->y0 >= UI_QUICK_HEIGHT && y >= UI_QUICK_HEIGHT) {
                dismiss(menu, action);
                menu->swallow = false;
            }
        }
    }
    return true;
}
