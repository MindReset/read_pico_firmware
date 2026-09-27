/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：由主循环喂入触摸快照的纯状态识别器，不解释页面动作。
 * English: Pure state recognizer fed by loop touch snapshots; no page actions.
 *
 * 冻结：翻页不做按下反馈；阈值来自 ui_kit.h；短拖取消以免误翻。
 * Frozen: No press decoration for page turns; thresholds live in ui_kit.h; short drags cancel to avoid accidental turns.
 */
#pragma once
#include "app.h"

typedef enum {
    UI_GESTURE_PRESS, ///< 按下 / Press
    UI_GESTURE_LONG_PRESS, ///< 长按一次 / Single long press
    UI_GESTURE_TAP, ///< 原点轻点 / Tap at origin
    UI_GESTURE_SWIPE_L, ///< 左滑 / Swipe left
    UI_GESTURE_SWIPE_R, ///< 右滑 / Swipe right
    UI_GESTURE_SWIPE_U, ///< 上滑 / Swipe up
    UI_GESTURE_SWIPE_D, ///< 下滑 / Swipe down
    UI_GESTURE_CANCEL, ///< 取消 / Cancel
} ui_gesture_type_t;

typedef struct ui_gesture_event_s {
    ui_gesture_type_t type; ///< 事件类型 / Event type
    uint16_t x0, y0; ///< 按下原点，也是 TAP 的逻辑坐标 / Press origin, also the logical TAP position
    uint16_t x, y; ///< 当前或释放位置，供控件落外取消 / Current or release position for outside-control cancellation
    int64_t hold_ms; ///< 非负持续时间 / Nonnegative duration
} ui_gesture_event_t;

typedef struct {
    bool active; ///< 正在跟踪 / Tracking a contact
    uint16_t x0, y0; ///< 起点 / Origin
    uint16_t x, y; ///< 最近有效坐标 / Last valid coordinates
    int64_t t0_ms; ///< 起始时刻 / Start time
    int64_t last_ms; ///< 最近时刻，倒退时取消 / Last time, cancel on clock regression
    bool moved; ///< 曾超出容差 / Has exceeded movement slop
    bool long_fired; ///< 已发出长按 / Long press emitted
} ui_gesture_t;

/// 清除状态；进出页面时调用。/ Clear state when entering or leaving a page.
void ui_gesture_reset(ui_gesture_t *gesture);
/// 每次最多一个事件；consumed、多点或缺失快照取消活动序列。
/// Emit at most one event; consumed, multitouch or a missing snapshot cancels an active sequence.
bool ui_gesture_feed(ui_gesture_t *gesture, const app_ctx_t *ctx, ui_gesture_event_t *out);
