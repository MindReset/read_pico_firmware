/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 实验室 / 上电助手。产品代码只应包含 sc7a20h.h。
 *
 * Lab / bring-up helpers. Product code should include sc7a20h.h only.
 */

#pragma once

#include "sc7a20h.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SC7A20H_INT1_CLICK 0x80
#define SC7A20H_INT1_AOI1 0x40
#define SC7A20H_INT1_AOI2 0x20
#define SC7A20H_INT1_DRDY 0x10
#define SC7A20H_INT1_WTM 0x04
#define SC7A20H_INT1_OVERRUN 0x02

#define SC7A20H_AOI_XL 0x01
#define SC7A20H_AOI_XH 0x02
#define SC7A20H_AOI_YL 0x04
#define SC7A20H_AOI_YH 0x08
#define SC7A20H_AOI_ZL 0x10
#define SC7A20H_AOI_ZH 0x20
#define SC7A20H_AOI_6D 0x40
#define SC7A20H_AOI_AND 0x80
#define SC7A20H_AOI_XYZ \
    (SC7A20H_AOI_XL | SC7A20H_AOI_XH | SC7A20H_AOI_YL | SC7A20H_AOI_YH \
        | SC7A20H_AOI_ZL | SC7A20H_AOI_ZH)

#define SC7A20H_FIFO_MAX 32
#define SC7A20H_DUMP_LEN 16

typedef enum {
    SC7A20H_AOI1 = 0,
    SC7A20H_AOI2 = 1,
} sc7a20h_aoi_t;

/// 芯片坐标系的 6D 面。产品轴映射放在应用层。
/// / Chip-frame 6D face. Map to product axes in the application.
typedef enum {
    SC7A20H_ORIENT_UNKNOWN = 0,
    SC7A20H_ORIENT_PX,
    SC7A20H_ORIENT_NX,
    SC7A20H_ORIENT_PY,
    SC7A20H_ORIENT_NY,
    SC7A20H_ORIENT_PZ,
    SC7A20H_ORIENT_NZ,
} sc7a20h_orient_t;

typedef enum {
    SC7A20H_FIFO_BYPASS = 0,
    SC7A20H_FIFO_MODE = 1,
    SC7A20H_FIFO_STREAM = 2,
    SC7A20H_FIFO_TRIGGER = 3,
} sc7a20h_fifo_mode_t;

/// 点击阈值步进 0..7。命名值是常用预设。
/// / Click threshold step 0..7. Named values are the useful presets.
typedef enum {
    SC7A20H_CLICK_SOFT = 1,
    SC7A20H_CLICK_NORMAL = 3,
    SC7A20H_CLICK_FIRM = 5,
    SC7A20H_CLICK_HARD = 7,
} sc7a20h_click_ths_t;

#define SC7A20H_CLICK_THS_MAX 7

typedef struct {
    uint8_t cfg;
    uint16_t ths_mg;
    uint8_t duration;
} sc7a20h_aoi_cfg_t;

typedef struct {
    uint8_t src;
    bool ia;
    bool zh;
    bool zl;
    bool yh;
    bool yl;
    bool xh;
    bool xl;
} sc7a20h_aoi_src_t;

typedef struct {
    uint8_t src;
    uint8_t count;
} sc7a20h_click_src_t;

typedef struct {
    sc7a20h_fifo_mode_t mode;
    uint8_t watermark;
    bool trigger_aoi2;
    bool bit8;
} sc7a20h_fifo_cfg_t;

typedef struct {
    uint8_t raw;
    bool wtm;
    bool overrun;
    bool empty;
    uint8_t fss;
} sc7a20h_fifo_status_t;

typedef struct {
    int16_t dx_mg;
    int16_t dy_mg;
    int16_t dz_mg;
    bool pass_x;
    bool pass_y;
    bool pass_z;
} sc7a20h_selftest_t;

typedef struct {
    int16_t mean_x;
    int16_t mean_y;
    int16_t mean_z;
    int16_t pp_x;
    int16_t pp_y;
    int16_t pp_z;
    uint16_t std_x;
    uint16_t std_y;
    uint16_t std_z;
    uint16_t odr_x10;
    uint16_t overrun;
    uint16_t n;
} sc7a20h_stats_t;

typedef struct {
    uint8_t click_src;
    uint8_t aoi1_src;
    uint8_t aoi2_src;
    uint8_t fifo_src;
    uint8_t status;
    uint32_t int1_count;
    int int1_level;
} sc7a20h_events_t;

esp_err_t sc7a20h_read_new(sc7a20h_handle_t h, sc7a20h_sample_t* sample);
esp_err_t sc7a20h_dump_regs(sc7a20h_handle_t h, uint8_t* out, size_t length);

esp_err_t sc7a20h_click_config(
    sc7a20h_handle_t h, uint8_t axis_mask, sc7a20h_click_ths_t ths
);
esp_err_t sc7a20h_click_read(sc7a20h_handle_t h, sc7a20h_click_src_t* src);

esp_err_t sc7a20h_aoi_config(sc7a20h_handle_t h, sc7a20h_aoi_t unit, const sc7a20h_aoi_cfg_t* cfg);
esp_err_t sc7a20h_aoi_read(sc7a20h_handle_t h, sc7a20h_aoi_t unit, sc7a20h_aoi_src_t* src);
void sc7a20h_aoi_decode(uint8_t raw, sc7a20h_aoi_src_t* src);

esp_err_t sc7a20h_orientation_arm(
    sc7a20h_handle_t h, bool enable_4d, uint16_t ths_mg, uint8_t duration
);
sc7a20h_orient_t sc7a20h_orientation(const sc7a20h_aoi_src_t* src);
esp_err_t sc7a20h_freefall_config(sc7a20h_handle_t h, uint16_t ths_mg, uint8_t duration);
esp_err_t sc7a20h_activity_config(sc7a20h_handle_t h, uint16_t ths_mg, uint8_t duration);

esp_err_t sc7a20h_fifo_config(sc7a20h_handle_t h, const sc7a20h_fifo_cfg_t* cfg);
esp_err_t sc7a20h_fifo_status(sc7a20h_handle_t h, sc7a20h_fifo_status_t* status);
esp_err_t sc7a20h_fifo_read(
    sc7a20h_handle_t h, sc7a20h_sample_t* samples, size_t max, size_t* out_n
);
esp_err_t sc7a20h_fifo_clear(sc7a20h_handle_t h);

esp_err_t sc7a20h_self_test(sc7a20h_handle_t h, sc7a20h_selftest_t* result);
esp_err_t sc7a20h_stats_collect(sc7a20h_handle_t h, uint16_t n, sc7a20h_stats_t* stats);

esp_err_t sc7a20h_int_route(sc7a20h_handle_t h, uint8_t mask);
esp_err_t sc7a20h_int1_begin(sc7a20h_handle_t h);
uint32_t sc7a20h_int1_count(sc7a20h_handle_t h);
esp_err_t sc7a20h_read_events(sc7a20h_handle_t h, sc7a20h_events_t* events);

uint8_t sc7a20h_ths_lsb_fs(sc7a20h_fs_t fs, uint16_t ths_mg);
uint16_t sc7a20h_ths_mg_fs(sc7a20h_fs_t fs, uint8_t lsb);
uint8_t sc7a20h_ths_lsb(sc7a20h_handle_t h, uint16_t ths_mg);
uint16_t sc7a20h_ths_mg(sc7a20h_handle_t h, uint8_t lsb);

const char* sc7a20h_orient_name(sc7a20h_orient_t orient);
const char* sc7a20h_click_name(uint8_t count);
const char* sc7a20h_fifo_mode_name(sc7a20h_fifo_mode_t mode);
const char* sc7a20h_int_route_name(uint8_t mask);

#ifdef __cplusplus
}
#endif
