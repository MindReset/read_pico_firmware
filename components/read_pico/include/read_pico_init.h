/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 小纸 Pico 板级 bring-up：墨水屏、SD、加速度计、PMU、触摸。
 *
 * Read Pico board bring-up: EPD, SD, accelerometer, PMU, touch.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cst836u.h"
#include "epd_highlevel.h"
#include "esp_err.h"
#include "sc7a20h.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    EpdiyHighlevelState hl;
    uint8_t* framebuffer;
    sc7a20h_handle_t sensor;
    cst836u_handle_t touch;
    bool sensor_ready;
    bool touch_ready;
    bool pmu_ready;
    uint8_t sensor_whoami;
    uint8_t sensor_version;
} read_pico_handle_t;

/// 墨水屏、SD 探测、加速度计（校验后掉电）、PMU、触摸。
/// 外设失败只清对应 ready；墨水屏起不来才返回错误。
/// / EPD, SD probe, accelerometer (power-down after check), PMU, touch.
/// Peripheral failures clear that ready flag; only EPD failure is fatal.
esp_err_t read_pico_init(read_pico_handle_t* handle);
void read_pico_deinit(read_pico_handle_t* handle);

/// 加速度计只在 AXIS 页采样，离开后关掉 ODR。
/// / Sample the accelerometer only on the AXIS page; drop ODR on leave.
void read_pico_sensor_wake(sc7a20h_handle_t h);
void read_pico_sensor_sleep(sc7a20h_handle_t h);

/// 芯片坐标 → 设备坐标。平放屏幕朝上为 +Z；立在 X 正边为 +X。
/// Xd=-Yc，Yd=-Xc，Zd=-Zc。
/// / Chip frame to device frame. Face-up is +Z; standing on +X is +X.
/// Xd=-Yc, Yd=-Xc, Zd=-Zc.
void read_pico_accel_to_device(sc7a20h_sample_t* sample);
uint8_t read_pico_accel_map_aoi(uint8_t src);
esp_err_t read_pico_accel_read(sc7a20h_handle_t h, sc7a20h_sample_t* sample);

#ifdef __cplusplus
}
#endif
