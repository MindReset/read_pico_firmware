/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：单指手势状态机，页面负责解释事件与绘制。
 * English: Single-finger gesture state machine; pages interpret and draw events.
 * 冻结：翻页不画按下态；引用 ui_kit.h 阈值；短拖取消以免误翻。
 * Frozen: No pressed decoration for page turns; use ui_kit.h thresholds; short drags cancel to avoid accidental turns.
 */
#include "ui_gesture.h"
#include "ui_kit.h"
#include <string.h>

void ui_gesture_reset(ui_gesture_t *g) {
    if (g) memset(g, 0, sizeof(*g));
}

static bool emit(const ui_gesture_t *g, ui_gesture_type_t type, int64_t now, ui_gesture_event_t *out) {
    *out = (ui_gesture_event_t){
        .type = type, .x0 = g->x0, .y0 = g->y0, .x = g->x, .y = g->y,
        .hold_ms = now >= g->t0_ms ? now - g->t0_ms : 0,
    };
    return true;
}

bool ui_gesture_feed(ui_gesture_t *g, const app_ctx_t *ctx, ui_gesture_event_t *out) {
    if (!g || !ctx || !out) return false;
    const cst836u_touch_t *touch = ctx->touch;
    bool invalid = ctx->consumed || !touch || ctx->now_ms < 0;
    if (touch && touch->count > 1) invalid = true;
    if (g->active && ctx->now_ms < g->last_ms) invalid = true;
    if (invalid) {
        if (!g->active) return false;
        bool result = emit(g, UI_GESTURE_CANCEL, ctx->now_ms, out);
        ui_gesture_reset(g);
        return result;
    }
    if (!g->active) {
        if (!ctx->pressed || ctx->released || !touch->touched) return false;
        *g = (ui_gesture_t){ .active = true, .x0 = touch->x, .y0 = touch->y,
            .x = touch->x, .y = touch->y, .t0_ms = ctx->now_ms, .last_ms = ctx->now_ms };
        return emit(g, UI_GESTURE_PRESS, ctx->now_ms, out);
    }
    g->last_ms = ctx->now_ms;
    g->x = touch->x; g->y = touch->y;
    int32_t dx = (int32_t)g->x - g->x0, dy = (int32_t)g->y - g->y0;
    int32_t ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    // 容差使用二维距离，移回起点也不恢复轻点资格。/ Slop uses two-dimensional distance; returning to the origin never restores tap eligibility.
    if ((int64_t)dx * dx + (int64_t)dy * dy > (int64_t)UI_TOUCH_SLOP_PX * UI_TOUCH_SLOP_PX) g->moved = true;
    if (ctx->released || !touch->touched) {
        ui_gesture_type_t type = UI_GESTURE_CANCEL;
        if (ctx->released && !g->long_fired) {
            if (ax >= UI_SWIPE_MIN_PX && ax > ay) type = dx < 0 ? UI_GESTURE_SWIPE_L : UI_GESTURE_SWIPE_R;
            else if (ay >= UI_SWIPE_MIN_PX && ay > ax) type = dy < 0 ? UI_GESTURE_SWIPE_U : UI_GESTURE_SWIPE_D;
            else if (!g->moved) type = UI_GESTURE_TAP;
        }
        bool result = emit(g, type, ctx->now_ms, out);
        ui_gesture_reset(g);
        return result;
    }
    if (!g->moved && !g->long_fired && ctx->now_ms - g->t0_ms >= UI_LONG_PRESS_MS) {
        g->long_fired = true;
        return emit(g, UI_GESTURE_LONG_PRESS, ctx->now_ms, out);
    }
    return false;
}
