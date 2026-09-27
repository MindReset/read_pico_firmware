/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 阅读进度换算和晃动判定，不访问硬件。/ Reading position and shake policy without hardware access.
 * 冻结：两次上升沿600ms内触发，冷却800ms（按实机响应反馈缩短）。
 * Frozen: two rising edges within 600ms, then 800ms cooldown, shortened after hardware response feedback.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/// UTF8章内偏移映射回文件进度，避免GBK字节单位混用。/ Map UTF8 chapter offsets to source bytes, including GBK.
static inline uint32_t book_position_bytes(uint32_t start, uint32_t end, size_t off, size_t len) {
    if (end < start || !len) return start;
    if (off > len) off = len;
    return start + (uint64_t)(end - start) * off / len;
}

typedef struct {
    bool high; ///< 上次AOI状态 / Previous AOI state
    bool pending; ///< 等待第二次沿 / Waiting for second edge
    int64_t first_ms; ///< 首次沿时间 / First edge time
    int64_t cooldown_ms; ///< 冷却截止 / Cooldown deadline
} book_shake_gate_t;

/// 屏蔽时清除累计动作，持续高电平不重复计数。/ Suppression resets partial gestures; a held level counts once.
static inline bool book_shake_feed(book_shake_gate_t* g, bool high, bool suppressed, int64_t now) {
    bool rising = high && !g->high;
    g->high = high;
    if (suppressed || now < g->cooldown_ms) {
        g->pending = false;
        return false;
    }
    if (!rising) return false;
    if (g->pending && now - g->first_ms <= 600) {
        g->pending = false;
        g->cooldown_ms = now + 800;
        return true;
    }
    g->pending = true;
    g->first_ms = now;
    return false;
}
