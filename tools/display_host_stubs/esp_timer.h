/* SPDX-License-Identifier: Apache-2.0
 * 测试时钟替身。/ Test clock shim.
 */
#pragma once
#include <stdint.h>
int64_t esp_timer_get_time(void);
