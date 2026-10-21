/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：显示回归仅模拟毫秒到时钟节拍的边界。/ English: Display regressions mock only the millisecond-to-tick boundary.
 */
#pragma once
#include <stdint.h>
typedef uint32_t TickType_t;
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
