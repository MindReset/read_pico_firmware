/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
// 无操作计时；网络及升级后台不算用户操作。/ Idle time excludes background network and update ticks.
static inline bool auto_lock_due(int64_t now_ms, int64_t last_input_ms, unsigned minutes) {
    return (minutes == 1 || minutes == 5 || minutes == 10) && now_ms >= last_input_ms &&
           now_ms - last_input_ms >= (int64_t)minutes * 60000;
}
