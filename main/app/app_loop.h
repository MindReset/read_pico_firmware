/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 事件循环入口。app_main 只负责把硬件句柄填进来。
 *
 * Event-loop entry. app_main only fills in the hardware handles.
 */

#pragma once

#include "app.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    EpdiyHighlevelState* hl;
    uint8_t* fb;
    sc7a20h_handle_t acc;
    cst836u_handle_t tp;
    bool sensor_ready;
    /// 开机停在哪一页。断电续跑自检时是自检页，平时是概览页。/ First page: self-test when resuming a power-cut run, otherwise overview.
    const app_desc_t* first_app;
} app_loop_config_t;

/// 不返回。/ Does not return.
void app_loop_run(const app_loop_config_t* config);

#ifdef __cplusplus
}
#endif
