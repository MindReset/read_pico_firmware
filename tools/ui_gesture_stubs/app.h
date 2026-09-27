/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：识别器的主机触摸输入契约。/ English: Host touch input contract for the recognizer.
 * 冻结：仅用于测试。/ Frozen: Tests only.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct { bool touched; uint8_t count; uint16_t x, y; } cst836u_touch_t;
typedef struct {
    int64_t now_ms;
    const cst836u_touch_t *touch;
    bool pressed, released, consumed;
} app_ctx_t;
