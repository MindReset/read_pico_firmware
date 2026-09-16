/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 出厂 VCOM 标定入口。PMU 未标定时由 app_main 拦住开机。
 *
 * Factory VCOM calibration entry. app_main gates boot when the PMU is unset.
 */

#pragma once

#include "cst836u.h"
#include "epd_highlevel.h"

#ifdef __cplusplus
extern "C" {
#endif

/// PMU 未标定 VCOM 时拦住开机，写进 PMU 后才返回。/ Gate boot when VCOM is unset; returns only after it is written to the PMU.
void vcom_setup_run(EpdiyHighlevelState* hl, uint8_t* fb, cst836u_handle_t tp);

#ifdef __cplusplus
}
#endif
