/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：长按电源的滑动关机层；不分配、刷屏、写设置或调用硬件。
 * English: Slide-to-off layer for a power hold; no allocation, presentation, settings writes or hardware calls.
 */
#include "ui_power_dialog.h"
#include <stdlib.h>
#include <string.h>
#include "ui_kit.h"

enum { HIT_NONE, HIT_SLIDER, HIT_RESTART, HIT_CANCEL };

static bool in_circle(int x, int y, int cx, int cy, int r) {
    int dx = x - cx, dy = y - cy;
    return abs(dx) <= r && abs(dy) <= r && dx * dx + dy * dy <= r * r;
}

static EpdRect slider_change(int before, int after) {
    if (before == after) return (EpdRect){0};
    int left = before < after ? before : after;
    return (EpdRect){UI_POWER_THUMB_X + left - UI_POWER_THUMB_R - 1,
        UI_POWER_THUMB_Y - UI_POWER_THUMB_R - 1,
        abs(after - before) + 2 * UI_POWER_THUMB_R + 3, 2 * UI_POWER_THUMB_R + 3};
}

static EpdRect button_area(int hit) {
    int x = hit == HIT_RESTART ? UI_POWER_RESTART_X : UI_POWER_CANCEL_X;
    return (EpdRect){x - UI_POWER_BUTTON_R - 1, UI_POWER_BUTTON_Y - UI_POWER_BUTTON_R - 1,
        2 * UI_POWER_BUTTON_R + 3, 2 * UI_POWER_BUTTON_R + 3};
}

void ui_power_dialog_reset(ui_power_dialog_t *dialog, bool touching) {
    if (!dialog) return;
    memset(dialog, 0, sizeof(*dialog));
    dialog->swallow = touching;
}

void ui_power_dialog_cancel_input(ui_power_dialog_t *dialog, EpdRect *dirty) {
    if (dirty) *dirty = (EpdRect){0};
    if (!dialog) return;
    if (dirty) {
        if (dialog->position) *dirty = slider_change(dialog->position, 0);
        else if (dialog->pressed) *dirty = button_area(dialog->hit);
    }
    ui_power_dialog_reset(dialog, true);
}

ui_power_action_t ui_power_dialog_feed(ui_power_dialog_t *dialog, const app_ctx_t *ctx, EpdRect *dirty) {
    if (!dirty) return UI_POWER_ACTION_NONE;
    *dirty = (EpdRect){0};
    if (!dialog) return UI_POWER_ACTION_NONE;
    if (!ctx || ctx->consumed || !ctx->touch ||
        (ctx->touch->touched && (ctx->touch->count != 1 ||
          ctx->touch->x >= UI_LOCK_WIDTH || ctx->touch->y >= UI_LOCK_HEIGHT))) {
        ui_power_dialog_cancel_input(dialog, dirty);
        return UI_POWER_ACTION_NONE;
    }
    const cst836u_touch_t *touch = ctx->touch;
    if (dialog->swallow) {
        if (!touch->touched) dialog->swallow = false;
        return UI_POWER_ACTION_NONE;
    }
    int x = touch->x, y = touch->y;
    if (ctx->pressed && touch->touched && !dialog->tracking) {
        dialog->tracking = true;
        dialog->x0 = x; dialog->y0 = y;
        dialog->started_ms = ctx->now_ms;
        int cx = UI_POWER_THUMB_X + dialog->position;
        if (in_circle(x, y, cx, UI_POWER_THUMB_Y, UI_POWER_THUMB_R + 6)) {
            dialog->hit = HIT_SLIDER;
            dialog->grab_x = x - cx;
        } else if (in_circle(x, y, UI_POWER_RESTART_X, UI_POWER_BUTTON_Y, UI_POWER_BUTTON_R)) {
            dialog->hit = HIT_RESTART;
        } else if (in_circle(x, y, UI_POWER_CANCEL_X, UI_POWER_BUTTON_Y, UI_POWER_BUTTON_R)) {
            dialog->hit = HIT_CANCEL;
        }
        if (dialog->hit >= HIT_RESTART) {
            dialog->pressed = true;
            *dirty = button_area(dialog->hit);
        }
    }
    if (!dialog->tracking) return UI_POWER_ACTION_NONE;
    if (ctx->now_ms < dialog->started_ms || (!touch->touched && !ctx->released)) {
        ui_power_dialog_cancel_input(dialog, dirty);
        return UI_POWER_ACTION_NONE;
    }
    if (dialog->hit == HIT_SLIDER) {
        // 捕获后可水平越界；垂直滑出取消，不能在别处抬手触发关机。
        // Capture permits horizontal overshoot; leaving vertically cancels, preventing off-track release shutdown.
        if (abs(y - UI_POWER_THUMB_Y) > UI_POWER_TRACK_H) {
            ui_power_dialog_cancel_input(dialog, dirty);
            return UI_POWER_ACTION_NONE;
        }
        if (touch->touched) {
            int next = x - dialog->grab_x - UI_POWER_THUMB_X;
            if (next < 0) next = 0;
            if (next > UI_POWER_SLIDE_RANGE) next = UI_POWER_SLIDE_RANGE;
            *dirty = slider_change(dialog->position, next);
            dialog->position = next;
        } else if (ctx->released) {
            if (dialog->position >= UI_POWER_SLIDE_COMMIT) {
                ui_power_dialog_reset(dialog, false);
                return UI_POWER_ACTION_SHUTDOWN;
            }
            ui_power_dialog_cancel_input(dialog, dirty);
            dialog->swallow = false;
        }
    } else if (dialog->hit >= HIT_RESTART) {
        int cx = dialog->hit == HIT_RESTART ? UI_POWER_RESTART_X : UI_POWER_CANCEL_X;
        if (!in_circle(x, y, cx, UI_POWER_BUTTON_Y, UI_POWER_BUTTON_R) ||
            abs(x - dialog->x0) > UI_TOUCH_SLOP_PX || abs(y - dialog->y0) > UI_TOUCH_SLOP_PX) {
            ui_power_dialog_cancel_input(dialog, dirty);
            if (!touch->touched) dialog->swallow = false;
        } else if (ctx->released) {
            ui_power_action_t action = dialog->hit == HIT_RESTART ? UI_POWER_ACTION_RESTART : UI_POWER_ACTION_CANCEL;
            ui_power_dialog_reset(dialog, false);
            return action;
        }
    } else if (ctx->released) {
        ui_power_dialog_reset(dialog, false);
    }
    return UI_POWER_ACTION_NONE;
}

EpdRect ui_power_dialog_rect(void) { return (EpdRect){0, 0, UI_LOCK_WIDTH, UI_LOCK_HEIGHT}; }

static void draw_circle_button(uint8_t *fb, const ui_power_dialog_t *dialog, int hit, int cx) {
    epd_fill_circle(cx, UI_POWER_BUTTON_Y, UI_POWER_BUTTON_R,
        dialog->pressed && dialog->hit == hit ? 0xd0 : UI_GRAY_WHITE, fb);
    epd_draw_circle(cx, UI_POWER_BUTTON_Y, UI_POWER_BUTTON_R, 0x60, fb);
    epd_draw_circle(cx, UI_POWER_BUTTON_Y, UI_POWER_BUTTON_R - 1, 0x60, fb);
    if (hit == HIT_RESTART) ui_draw_icon(fb, cx, UI_POWER_BUTTON_Y, 46, UI_ICON_ROTATE_CW, UI_GRAY_BLACK);
    else {
        // 圆帽取消符号，圆钮外部保持原白底。/ Round-ended cancel mark; preserve the white outside the circle.
        for (int i = -14; i <= 14; ++i) {
            epd_fill_circle(cx + i, UI_POWER_BUTTON_Y + i, 2, UI_GRAY_BLACK, fb);
            epd_fill_circle(cx + i, UI_POWER_BUTTON_Y - i, 2, UI_GRAY_BLACK, fb);
        }
    }
}

void ui_power_dialog_draw_controls(uint8_t *fb, const ui_power_dialog_t *dialog) {
    if (!fb || !dialog) return;
    EpdRect track = {UI_POWER_TRACK_X, UI_POWER_TRACK_Y, UI_POWER_TRACK_W, UI_POWER_TRACK_H};
    ui_fill_round_rect(fb, track, UI_POWER_TRACK_H / 2, 0xe0);
    ui_draw_control_frame(fb, track, UI_POWER_TRACK_H / 2, 0x60);
    ui_text_box(fb, (EpdRect){254, UI_POWER_TRACK_Y + 28, 302, 56}, 30,
        "滑动关机", EPD_DRAW_ALIGN_CENTER, false);
    int cx = UI_POWER_THUMB_X + dialog->position;
    epd_fill_circle(cx, UI_POWER_THUMB_Y, UI_POWER_THUMB_R, UI_GRAY_WHITE, fb);
    epd_draw_circle(cx, UI_POWER_THUMB_Y, UI_POWER_THUMB_R, 0x60, fb);
    epd_draw_circle(cx, UI_POWER_THUMB_Y, UI_POWER_THUMB_R - 1, 0x60, fb);
    ui_draw_icon(fb, cx, UI_POWER_THUMB_Y, 46, UI_ICON_POWER, UI_GRAY_BLACK);
    draw_circle_button(fb, dialog, HIT_RESTART, UI_POWER_RESTART_X);
    draw_circle_button(fb, dialog, HIT_CANCEL, UI_POWER_CANCEL_X);
}

void ui_power_dialog_draw(uint8_t *fb, const ui_power_dialog_t *dialog) {
    if (!fb || !dialog) return;
    ui_clear_page(fb);
    ui_text_box(fb, (EpdRect){48, 166, UI_LOCK_WIDTH - 96, 84}, 44,
        "电源选项", EPD_DRAW_ALIGN_CENTER, false);
    ui_text_box(fb, (EpdRect){72, 458, UI_LOCK_WIDTH - 144, 48}, 24,
        "向右滑动，松手后关机", EPD_DRAW_ALIGN_CENTER, false);
    ui_power_dialog_draw_controls(fb, dialog);
    ui_text_box(fb, (EpdRect){UI_POWER_RESTART_X - 90, 1044, 180, 54}, 28,
        "重启", EPD_DRAW_ALIGN_CENTER, false);
    ui_text_box(fb, (EpdRect){UI_POWER_CANCEL_X - 90, 1044, 180, 54}, 28,
        "取消", EPD_DRAW_ALIGN_CENTER, false);
}

void ui_power_final_draw(uint8_t *fb, bool restarting) {
    ui_clear_page(fb);
    const int cx = UI_LOCK_WIDTH / 2;
    ui_draw_icon(fb, cx, 486, 72, restarting ? UI_ICON_ROTATE_CW : UI_ICON_POWER, UI_GRAY_BLACK);
    ui_text(fb, cx, 548, 48, restarting ? "正在重启" : "kiikoread 已关机", EPD_DRAW_ALIGN_CENTER, false);
    if (!restarting) ui_text(fb, cx, 624, 26, "长按电源键开机", EPD_DRAW_ALIGN_CENTER, false);
}
