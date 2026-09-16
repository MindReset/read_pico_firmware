/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * ECC200 方形 Data Matrix 编码。输出 modules 供 ui_kit 绘制。
 *
 * ECC200 square Data Matrix encoder. Writes modules for ui_kit to draw.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DM_MAX_DIM 32

/// ECC200 方形 Data Matrix。modules 为 DM_MAX_DIM * DM_MAX_DIM、行优先，1 = 黑。
/// ECC200 square Data Matrix. modules is DM_MAX_DIM * DM_MAX_DIM, row-major, 1 = black.
int dm_encode(const char* text, uint8_t* modules, int* dim);

#ifdef __cplusplus
}
#endif
