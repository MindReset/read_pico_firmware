/* SPDX-License-Identifier: Apache-2.0
 * 宿主端替身：词典打开失败的退避用不到真实时钟，测试里恒为 0。
 * Host stub: the open back-off does not need a real clock in tests, so it stays at zero. */
#pragma once
#include <stdint.h>
static inline int64_t esp_timer_get_time(void) { return 0; }
