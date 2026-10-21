/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 阅读进度换算和晃动判定，不访问硬件。/ Reading position and shake policy without hardware access.
 * 用户修订：横向一次动作翻页，按方向返回 -1/+1；冷却800ms并静止120ms后再接受动作。
 * User revision: one horizontal impulse returns -1/+1; rearm after 800ms cooldown and 120ms at rest.
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
    bool ready, armed;
    int gravity[3]; ///< 随姿态缓慢更新的重力基准 / Slowly tracked gravity baseline
    int previous_x;
    int64_t last_ms, quiet_ms;
    int64_t cooldown_ms; ///< 冷却截止 / Cooldown deadline
} book_shake_gate_t;

static inline int book_shake_abs(int value) { return value < 0 ? -value : value; }

/// 输入为屏幕坐标的毫克加速度；慢倾斜不触发，强动作先解除待触发以屏蔽回弹。
/// Input is screen-axis acceleration in mg; reject slow tilts and disarm on strong motion to suppress rebounds.
static inline int book_shake_feed(book_shake_gate_t* g, int x, int y, int z,
                                  bool suppressed, int64_t now) {
    if (!g) return 0;
    if (!g->ready || suppressed || now < g->last_ms || now - g->last_ms > 250) {
        g->ready = true;
        g->armed = false;
        g->gravity[0] = x; g->gravity[1] = y; g->gravity[2] = z;
        g->previous_x = x;
        g->last_ms = g->quiet_ms = now;
        return 0;
    }
    g->last_ms = now;
    const int dx = x - g->gravity[0], dy = y - g->gravity[1], dz = z - g->gravity[2];
    const int ax = book_shake_abs(dx), ay = book_shake_abs(dy), az = book_shake_abs(dz);
    const int jerk = book_shake_abs(x - g->previous_x);
    g->previous_x = x;
    if (g->armed && now >= g->cooldown_ms && ax >= 210 && jerk >= 35 &&
        ax > ay * 3 / 2 + 30 && ax > az * 3 / 2 + 30) {
        g->armed = false;
        g->quiet_ms = now;
        g->cooldown_ms = now + 800;
        return dx > 0 ? 1 : -1;
    }
    const bool quiet = ax < 80 && ay < 80 && az < 80;
    if (!quiet) g->quiet_ms = now;
    if (now < g->cooldown_ms || ax >= 210 || ay >= 210 || az >= 210) g->armed = false;
    if (quiet && now >= g->cooldown_ms && now - g->quiet_ms >= 120) g->armed = true;
    // 静止或缓慢倾斜时跟踪姿态，不让重力产生翻页信号。/ Track posture at rest or during slow tilts.
    if (quiet || jerk < 35) {
        g->gravity[0] += dx / 8; g->gravity[1] += dy / 8; g->gravity[2] += dz / 8;
    }
    return 0;
}
