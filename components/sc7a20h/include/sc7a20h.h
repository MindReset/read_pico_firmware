/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * SC7A20H 三轴加速度计（士兰）。
 * 上电：软复位 0x68=0xA5，WHO_AM_I/VERSION，CTRL5.BOOT。
 *
 * 产品 API：创建、读数、电源、拿起唤醒。
 * 实验室助手（点击、FIFO、6D、自检）在 sc7a20h_lab.h。
 *
 * SC7A20H 3-axis accelerometer (Silan).
 * Bring-up: soft reset 0x68=0xA5, WHO_AM_I/VERSION, CTRL5.BOOT.
 *
 * Product API: create, read, power, pickup-to-wake.
 * Lab helpers (click, FIFO, 6D, self-test) live in sc7a20h_lab.h.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sc7a20h_dev_t* sc7a20h_handle_t;

#define SC7A20H_ADDR_DEFAULT 0x19
#define SC7A20H_WHO_AM_I_VAL 0x11
#define SC7A20H_VERSION_VAL 0x28

#define SC7A20H_AXIS_X 0x01
#define SC7A20H_AXIS_Y 0x02
#define SC7A20H_AXIS_Z 0x04
#define SC7A20H_AXIS_XYZ 0x07

/// 手册 0001 是 1.56 Hz（台上约 3 Hz）。1.48 kHz 以上未测。
/// / Datasheet 0001 is 1.56 Hz (~3 Hz on the bench). 1.48 kHz+ is untested.
typedef enum {
    SC7A20H_ODR_12_5 = 0x02,
    SC7A20H_ODR_25 = 0x03,
    SC7A20H_ODR_50 = 0x04,
    SC7A20H_ODR_100 = 0x05,
    SC7A20H_ODR_200 = 0x06,
    SC7A20H_ODR_400 = 0x07,
    SC7A20H_ODR_800 = 0x08,
} sc7a20h_odr_t;

typedef enum {
    SC7A20H_FS_2G = 0,
    SC7A20H_FS_4G = 1,
    SC7A20H_FS_8G = 2,
    SC7A20H_FS_16G = 3,
} sc7a20h_fs_t;

/// CTRL1 LPen + CTRL0/CTRL4 HR：00 正常、01 LP、10 HR、11 增强。
/// / CTRL1 LPen + CTRL0/CTRL4 HR: 00 normal, 01 LP, 10 HR, 11 enhanced.
typedef enum {
    SC7A20H_MODE_NORMAL = 0,
    SC7A20H_MODE_LP = 1,
    SC7A20H_MODE_HR = 2,
    SC7A20H_MODE_ENHANCED = 3,
} sc7a20h_mode_t;

typedef enum {
    SC7A20H_OSR_OFF = 0,
    SC7A20H_OSR_2 = 1,
    SC7A20H_OSR_4 = 2,
    SC7A20H_OSR_8 = 3,
    SC7A20H_OSR_16 = 4,
    SC7A20H_OSR_32 = 5,
} sc7a20h_osr_t;

typedef enum {
    SC7A20H_DLPF_OFF = 0,
    SC7A20H_DLPF_WEAK = 1,
    SC7A20H_DLPF_MED = 2,
    SC7A20H_DLPF_STRONG = 3,
} sc7a20h_dlpf_t;

/// OFF 保留重力。越大高通截止越快。
/// / OFF keeps gravity. Higher = faster high-pass cutoff.
typedef enum {
    SC7A20H_HPF_OFF = 0,
    SC7A20H_HPF_VERY_SLOW = 1,
    SC7A20H_HPF_SLOW = 2,
    SC7A20H_HPF_MED = 3,
    SC7A20H_HPF_FAST = 4,
} sc7a20h_hpf_t;

typedef struct {
    sc7a20h_odr_t odr;
    sc7a20h_fs_t fs;
    sc7a20h_mode_t mode;
    sc7a20h_osr_t osr;
    sc7a20h_dlpf_t dlpf;
    sc7a20h_hpf_t hpf;
    uint8_t axis_mask;
} sc7a20h_sensor_config_t;

#define SC7A20H_CONFIG_IDLE \
    { \
        .odr = SC7A20H_ODR_12_5, \
        .fs = SC7A20H_FS_2G, \
        .mode = SC7A20H_MODE_LP, \
        .osr = SC7A20H_OSR_OFF, \
        .dlpf = SC7A20H_DLPF_OFF, \
        .hpf = SC7A20H_HPF_OFF, \
        .axis_mask = SC7A20H_AXIS_XYZ, \
    }

#define SC7A20H_SENSOR_CONFIG_DEFAULT SC7A20H_CONFIG_IDLE

typedef struct {
    uint8_t i2c_addr;
    gpio_num_t int1_gpio;
    sc7a20h_sensor_config_t sensor;
} sc7a20h_config_t;

#define SC7A20H_DEVICE_CONFIG_DEFAULT() \
    { \
        .i2c_addr = SC7A20H_ADDR_DEFAULT, \
        .int1_gpio = GPIO_NUM_NC, \
        .sensor = SC7A20H_CONFIG_IDLE, \
    }

typedef struct {
    uint16_t ths_mg;
    uint8_t duration;
    uint8_t axis_mask;
} sc7a20h_motion_cfg_t;

#define SC7A20H_MOTION_DEFAULT() \
    { \
        .ths_mg = 250, \
        .duration = 2, \
        .axis_mask = SC7A20H_AXIS_XYZ, \
    }

typedef struct {
    int16_t x_mg;
    int16_t y_mg;
    int16_t z_mg;
    int16_t x_raw;
    int16_t y_raw;
    int16_t z_raw;
    uint8_t status;
} sc7a20h_sample_t;

esp_err_t sc7a20h_init(i2c_master_bus_handle_t bus_handle, const sc7a20h_config_t* config,
                       sc7a20h_handle_t* handle);
esp_err_t sc7a20h_deinit(sc7a20h_handle_t handle);

esp_err_t sc7a20h_apply_config(sc7a20h_handle_t h, const sc7a20h_sensor_config_t* config);
const sc7a20h_sensor_config_t* sc7a20h_get_config(sc7a20h_handle_t h);

/// CTRL1 ODR=0000，约 0.5 uA。上次配置留给 power_up。
/// / CTRL1 ODR=0000, ~0.5 uA. Last config is kept for power_up.
esp_err_t sc7a20h_power_down(sc7a20h_handle_t h);
esp_err_t sc7a20h_power_up(sc7a20h_handle_t h);
bool sc7a20h_powered(sc7a20h_handle_t h);

esp_err_t sc7a20h_read(sc7a20h_handle_t h, sc7a20h_sample_t* sample);

/// AOI1 高事件 + HPIS1，桌上静止不会被重力误触发。
/// / AOI1 high-event + HPIS1 so gravity does not trip a table-resting part.
esp_err_t sc7a20h_enable_motion(sc7a20h_handle_t h, const sc7a20h_motion_cfg_t* cfg);
/// LP 采样 + 运动 + INT1 路由 + 锁存。cfg 为 NULL 用 SC7A20H_MOTION_DEFAULT。
/// / LP sample + motion + INT1 route + latch. NULL cfg uses SC7A20H_MOTION_DEFAULT.
esp_err_t sc7a20h_arm_pickup_wake(sc7a20h_handle_t h, const sc7a20h_motion_cfg_t* cfg);
/// 输入 + 下拉 + 高电平 GPIO 唤醒。先拆掉 INT1 ISR。
/// / Input + pulldown + high-level GPIO wakeup. Detaches any INT1 ISR first.
esp_err_t sc7a20h_config_light_sleep_wakeup(sc7a20h_handle_t h);
esp_err_t sc7a20h_config_ext1_wakeup(sc7a20h_handle_t h);
/// 读锁存的 INT 源，好让 INT1 落下。
/// / Read latched INT sources so INT1 can fall.
esp_err_t sc7a20h_ack_int(sc7a20h_handle_t h);

esp_err_t sc7a20h_soft_reset(sc7a20h_handle_t h);
esp_err_t sc7a20h_version(sc7a20h_handle_t h, uint8_t* whoami, uint8_t* version);

gpio_num_t sc7a20h_int1_gpio(sc7a20h_handle_t h);
int sc7a20h_int1_level(sc7a20h_handle_t h);

uint16_t sc7a20h_odr_hz(sc7a20h_odr_t odr);
const char* sc7a20h_odr_name(sc7a20h_odr_t odr);
const char* sc7a20h_fs_name(sc7a20h_fs_t fs);
const char* sc7a20h_mode_name(sc7a20h_mode_t mode);

#ifdef __cplusplus
}
#endif
