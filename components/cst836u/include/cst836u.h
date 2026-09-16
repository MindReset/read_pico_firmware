/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * CST836U 自容触摸（Hynitron）。
 * 产品 API：创建、两点读取、动态上报 / 深睡。
 * 手册待机（手势唤醒）依赖固件，本驱动未实现。
 *
 * CST836U self-cap touch (Hynitron).
 * Product API: create, 2-point read, dynamic report / deep sleep.
 * Datasheet standby (gesture wake) depends on firmware and is not implemented.
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

typedef struct cst836u_dev_t* cst836u_handle_t;

#define CST836U_ADDR_DEFAULT 0x15
#define CST836U_MAX_POINTS 2
#define CST836U_RAW_LEN 15
#define CST836U_I2C_HZ_DEFAULT 400000

/// 动态上报（约 5 mA）或深睡（约 10 µA，RST 唤醒）。
/// / Dynamic report (~5 mA) or deep sleep (~10 µA, RST to wake).
typedef enum {
    CST836U_MODE_NORMAL = 0,
    CST836U_MODE_DEEPSLEEP,
} cst836u_mode_t;

typedef struct {
    bool active;
    uint8_t id;
    uint8_t event;
    uint16_t x;
    uint16_t y;
} cst836u_point_t;

typedef struct {
    bool touched;
    uint8_t count;
    uint16_t x;
    uint16_t y;
    cst836u_point_t points[CST836U_MAX_POINTS];
    uint8_t raw[CST836U_RAW_LEN];
} cst836u_touch_t;

typedef struct {
    uint8_t fw;
    uint8_t id;
    uint8_t module;
    uint8_t project;
    uint16_t type;
} cst836u_info_t;

typedef struct {
    uint8_t i2c_addr;
    uint32_t scl_speed_hz;
    gpio_num_t int_gpio;
    gpio_num_t rst_gpio;
    /// rst_gpio 为 GPIO_NUM_NC 时用（IO 扩展口等）。
    /// / Used when rst_gpio is GPIO_NUM_NC (IO expander, etc.).
    esp_err_t (*reset_fn)(void);
} cst836u_config_t;

#define CST836U_CONFIG_DEFAULT() \
    { \
        .i2c_addr = CST836U_ADDR_DEFAULT, \
        .scl_speed_hz = CST836U_I2C_HZ_DEFAULT, \
        .int_gpio = GPIO_NUM_NC, \
        .rst_gpio = GPIO_NUM_NC, \
        .reset_fn = NULL, \
    }

esp_err_t cst836u_init(i2c_master_bus_handle_t bus_handle, const cst836u_config_t* config,
                       cst836u_handle_t* handle);
esp_err_t cst836u_deinit(cst836u_handle_t handle);

esp_err_t cst836u_read(cst836u_handle_t h, cst836u_touch_t* touch);
esp_err_t cst836u_get_info(cst836u_handle_t h, cst836u_info_t* info);

esp_err_t cst836u_set_mode(cst836u_handle_t h, cst836u_mode_t mode);
cst836u_mode_t cst836u_get_mode(cst836u_handle_t h);

/// 脉冲 RST（GPIO 或 reset_fn）。深睡后 I2C 直到这次脉冲才应答。
/// / Pulse RST (gpio or reset_fn). Deep sleep ignores I2C until this.
esp_err_t cst836u_reset(cst836u_handle_t h);
/// RST 后再发 0xFE00。
/// / RST then 0xFE00.
esp_err_t cst836u_wake(cst836u_handle_t h);

/// IRQ 开漏、低有效。
/// / IRQ is open-drain, active low.
bool cst836u_int_asserted(cst836u_handle_t h);
int cst836u_int_level(cst836u_handle_t h);
gpio_num_t cst836u_int_gpio(cst836u_handle_t h);

const char* cst836u_mode_name(cst836u_mode_t mode);
const char* cst836u_event_name(uint8_t event);

#ifdef __cplusplus
}
#endif
