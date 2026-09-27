/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：真实识别器的时序与边界测试。
 * English: Timing and boundary tests for the actual recognizer.
 * 冻结：仅用于主机测试。/ Frozen: Host tests only.
 */
#include "ui_gesture.h"
#include "ui_kit.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
static ui_gesture_t g;
static ui_gesture_event_t event;
static cst836u_touch_t touch;
static app_ctx_t ctx;
static bool feed(int x, int y, int64_t time, bool down, bool press, bool release, bool consumed) {
    touch = (cst836u_touch_t){ .x = x, .y = y, .touched = down, .count = down ? 1 : 0 };
    ctx = (app_ctx_t){ .touch = &touch, .now_ms = time, .pressed = press, .released = release, .consumed = consumed };
    return ui_gesture_feed(&g, &ctx, &event);
}
static void press(int x, int y, int64_t time) {
    ui_gesture_reset(&g);
    assert(feed(x, y, time, true, true, false, false));
    assert(event.type == UI_GESTURE_PRESS && event.hold_ms == 0);
}
int main(void) {
    press(200, 300, 100);
    assert(!feed(200, 300, 100, true, true, false, false));
    assert(feed(210, 310, 200, false, false, true, false));
    assert(event.type == UI_GESTURE_TAP && event.x0 == 200 && event.y0 == 300);
    assert(event.x == 210 && event.y == 310 && event.hold_ms == 100);
    assert(!feed(210, 310, 200, false, false, true, false));
    press(100, 100, 0);
    assert(!feed(124, 100, 499, true, false, false, false));
    assert(feed(124, 100, 500, true, false, false, false));
    assert(event.type == UI_GESTURE_LONG_PRESS);
    assert(!feed(124, 100, 900, true, false, false, false));
    assert(feed(124, 100, 901, false, false, true, false));
    assert(event.type == UI_GESTURE_CANCEL);
    const int dx[] = {-64, 64, 0, 0}, dy[] = {0, 0, -64, 64};
    const ui_gesture_type_t kinds[] = {UI_GESTURE_SWIPE_L, UI_GESTURE_SWIPE_R, UI_GESTURE_SWIPE_U, UI_GESTURE_SWIPE_D};
    for (int i = 0; i < 4; ++i) {
        press(200, 300, 100);
        assert(!feed(200 + dx[i], 300 + dy[i], 150, true, false, false, false));
        assert(feed(200 + dx[i], 300 + dy[i], 200, false, false, true, false));
        assert(event.type == kinds[i]);
    }
    press(100, 100, 0);
    assert(!feed(125, 100, 500, true, false, false, false));
    assert(feed(100, 100, 600, false, false, true, false));
    assert(event.type == UI_GESTURE_CANCEL);
    press(100, 100, 0);
    assert(feed(220, 220, 100, false, false, true, false)); assert(event.type == UI_GESTURE_CANCEL);
    press(100, 100, 0);
    assert(feed(163, 100, 100, false, false, true, false)); assert(event.type == UI_GESTURE_CANCEL);
    press(100, 100, 0);
    assert(feed(100, 163, 100, false, false, true, false)); assert(event.type == UI_GESTURE_CANCEL);
    press(200, 300, 0);
    assert(!feed(136, 300, 500, true, false, false, false));
    assert(feed(136, 300, 501, false, false, true, false)); assert(event.type == UI_GESTURE_SWIPE_L);
    press(100, 100, 0);
    assert(!feed(117, 117, 500, true, false, false, false));
    assert(feed(117, 117, 600, false, false, true, false)); assert(event.type == UI_GESTURE_CANCEL);
    press(100, 100, 0);
    assert(feed(100, 100, 10, true, false, false, true)); assert(event.type == UI_GESTURE_CANCEL);
    assert(!feed(100, 100, 20, false, false, true, false));
    press(0, 0, INT64_MAX - 500);
    assert(feed(0, 0, INT64_MAX, true, false, false, false)); assert(event.type == UI_GESTURE_LONG_PRESS);
    press(65535, 65535, 100);
    assert(feed(0, 65535, 200, false, false, true, false)); assert(event.type == UI_GESTURE_SWIPE_L);
    press(10, 10, 100);
    assert(feed(10, 10, 99, true, false, false, false)); assert(event.type == UI_GESTURE_CANCEL);
    press(10, 10, 100); ctx.touch = NULL;
    assert(ui_gesture_feed(&g, &ctx, &event)); assert(event.type == UI_GESTURE_CANCEL);
    press(10, 10, 100); touch.count = 2; ctx.pressed = false;
    assert(ui_gesture_feed(&g, &ctx, &event)); assert(event.type == UI_GESTURE_CANCEL);
    ui_gesture_reset(&g);
    assert(!feed(10, 10, 100, true, true, false, true));
    assert(feed(10, 10, 100, true, true, false, false));
    assert(feed(10, 10, INT64_MIN, true, false, false, false));
    assert(event.type == UI_GESTURE_CANCEL && event.hold_ms == 0);
    assert(!ui_gesture_feed(NULL, &ctx, &event));
    puts("gesture host tests passed");
}
