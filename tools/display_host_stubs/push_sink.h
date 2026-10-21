/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：display 宿主测试的 push_sink 替身。display.c 只问一句「现在推流活跃吗」，
 * 为它把整条 NimBLE + push_sink 拉进宿主测试不值得（会引入 GATT/NimBLE 全链依赖，
 * 在 CI 里直接把宿主测试拖垮）。这里只给 display.c 真正用到的那点符号。
 *
 * English: push_sink stand-in for the display host test. display.c only asks "is a
 * push active right now"; dragging the whole NimBLE + push_sink chain into a host
 * test to answer that is not worth it (it would pull GATT/NimBLE and break the CI
 * host job). This provides just the symbols display.c actually uses.
 */
#pragma once

typedef enum {
    PUSH_STATE_IDLE = 0,
    PUSH_STATE_ARMED,
    PUSH_STATE_WINDOW,
    PUSH_STATE_CONNECTED,
    PUSH_STATE_RECEIVING,
    PUSH_STATE_PROCESSING,
    PUSH_STATE_COOLDOWN,
} push_state_t;

static inline push_state_t push_sink_state(void) {
    return PUSH_STATE_IDLE;
}
