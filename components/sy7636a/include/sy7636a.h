/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * SY7636A 墨水屏 PMIC（Silergy）。
 * I2C 只在 EN 为高时有效（STANDBY / ACTIVE）。EN 拉低会复位全部寄存器。
 * 调用方提供 EN / VCOM_EN / PGOOD：可以是 GPIO，也可以是 IO 扩展口回调。
 *
 * SY7636A EPD PMIC (Silergy).
 * I2C is live only while EN is high (STANDBY / ACTIVE). Pulling EN low
 * resets every register. The caller supplies EN / VCOM_EN / PGOOD: GPIO
 * or IO-expander callbacks.
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

typedef struct sy7636a_dev_t* sy7636a_handle_t;

#define SY7636A_ADDR_DEFAULT 0x62
#define SY7636A_I2C_HZ_DEFAULT 400000
#define SY7636A_VCOM_MAX_MV 5000

#define SY7636A_DISCHG_VCOM (1U << 0)
#define SY7636A_DISCHG_VNEG (1U << 1)
#define SY7636A_DISCHG_VPOS (1U << 2)
#define SY7636A_DISCHG_VDDH (1U << 4)
#define SY7636A_DISCHG_MASK 0x17

typedef enum {
    SY7636A_VLDO_1525 = 2,
    SY7636A_VLDO_1500 = 3,
    SY7636A_VLDO_1475 = 4,
    SY7636A_VLDO_1450 = 5,
    SY7636A_VLDO_1425 = 6,
} sy7636a_vldo_t;

typedef enum {
    SY7636A_FAULT_NONE = 0,
    SY7636A_FAULT_UVP_VP = 1,
    SY7636A_FAULT_UVP_VN = 2,
    SY7636A_FAULT_UVP_VPOS = 3,
    SY7636A_FAULT_UVP_VNEG = 4,
    SY7636A_FAULT_UVP_VDDH = 5,
    SY7636A_FAULT_UVP_VEE = 6,
    SY7636A_FAULT_SCP_VP = 7,
    SY7636A_FAULT_SCP_VN = 8,
    SY7636A_FAULT_SCP_VPOS = 9,
    SY7636A_FAULT_SCP_VNEG = 10,
    SY7636A_FAULT_SCP_VDDH = 11,
    SY7636A_FAULT_SCP_VEE = 12,
    SY7636A_FAULT_SCP_VCOM = 13,
    SY7636A_FAULT_OTP = 15,
    SY7636A_FAULT_UNKNOWN = 16,
} sy7636a_fault_t;

typedef struct {
    int vcom_mv;
    sy7636a_vldo_t vldo;
    uint8_t discharge;
    uint8_t dly_ms[4];
    bool vcom_manual;
} sy7636a_power_config_t;

#define SY7636A_POWER_CONFIG_DEFAULT() \
    { \
        .vcom_mv = 1250, \
        .vldo = SY7636A_VLDO_1500, \
        .discharge = 0, \
        .dly_ms = { 2, 2, 2, 2 }, \
        .vcom_manual = true, \
    }

typedef struct {
    bool on;
    bool vcom_manual;
    uint8_t discharge;
    int vcom_mv;
    sy7636a_vldo_t vldo;
    uint8_t dly_ms[4];
    uint8_t fault_reg;
    sy7636a_fault_t fault;
    bool pg;
    int8_t temperature_c;
} sy7636a_status_t;

typedef struct {
    uint8_t i2c_addr;
    uint32_t scl_speed_hz;
    gpio_num_t en_gpio;
    gpio_num_t vcom_en_gpio;
    gpio_num_t pgood_gpio;
    esp_err_t (*en_fn)(bool on);
    esp_err_t (*vcom_en_fn)(bool on);
    int (*pgood_fn)(void);
    uint16_t en_settle_ms;
    uint16_t pgood_timeout_ms;
    uint16_t poweroff_hold_ms;
    sy7636a_power_config_t power;
} sy7636a_config_t;

#define SY7636A_CONFIG_DEFAULT() \
    { \
        .i2c_addr = SY7636A_ADDR_DEFAULT, \
        .scl_speed_hz = SY7636A_I2C_HZ_DEFAULT, \
        .en_gpio = GPIO_NUM_NC, \
        .vcom_en_gpio = GPIO_NUM_NC, \
        .pgood_gpio = GPIO_NUM_NC, \
        .en_fn = NULL, \
        .vcom_en_fn = NULL, \
        .pgood_fn = NULL, \
        .en_settle_ms = 20, \
        .pgood_timeout_ms = 300, \
        .poweroff_hold_ms = 500, \
        .power = SY7636A_POWER_CONFIG_DEFAULT(), \
    }

esp_err_t sy7636a_init(i2c_master_bus_handle_t bus_handle, const sy7636a_config_t* config,
                       sy7636a_handle_t* handle);
esp_err_t sy7636a_deinit(sy7636a_handle_t handle);

/// EN 拉高并等待数字核就绪，不置 ON_OFF。*woke 表示这次是我们拉高的。
/// / Raise EN and wait for the digital core; does not set ON_OFF. *woke means we raised it.
esp_err_t sy7636a_i2c_begin(sy7636a_handle_t h, bool* woke);
/// 若 woke 且高压未开，把 EN 拉低（寄存器回默认）。
/// / If woke and the HV rails are off, drop EN (registers return to default).
void sy7636a_i2c_end(sy7636a_handle_t h, bool woke);

/// 写 VCOM / VLDO / 延时后置 ON_OFF，再等 PGOOD，最后拉 VCOM_EN。
/// / Write VCOM / VLDO / delays, set ON_OFF, wait for PGOOD, then raise VCOM_EN.
esp_err_t sy7636a_power_on(sy7636a_handle_t h);
esp_err_t sy7636a_power_off(sy7636a_handle_t h);
bool sy7636a_rails_on(sy7636a_handle_t h);

esp_err_t sy7636a_read(sy7636a_handle_t h, sy7636a_status_t* status);
const sy7636a_status_t* sy7636a_last(sy7636a_handle_t h);
const sy7636a_power_config_t* sy7636a_get_config(sy7636a_handle_t h);

esp_err_t sy7636a_set_vcom(sy7636a_handle_t h, int mv);
int sy7636a_get_vcom(sy7636a_handle_t h);
esp_err_t sy7636a_set_vcom_manual(sy7636a_handle_t h, bool manual);

// ---------------------------------------------------------------------------
// 危险：改源极幅值、关轨放电或上电间隔会直接作用在面板高压上。
// 接错屏会损坏面板。不要从产品界面调用。
// 改之前对照该型号屏幕规格书（VPOS/VNEG、VCOM、上电顺序）。
// EN 拉低后芯片回默认；下次 power_on 按这里缓存的值重写。
// Dangerous: source amplitude, rail discharge, and power-on spacing hit
// the panel HV. A wrong panel can be damaged. Do not call from product UI.
// Check that panel's spec (VPOS/VNEG, VCOM, power-on order) first.
// EN low restores defaults; the next power_on rewrites this cache.
// ---------------------------------------------------------------------------
esp_err_t sy7636a_set_vldo(sy7636a_handle_t h, sy7636a_vldo_t vldo);
esp_err_t sy7636a_set_discharge(sy7636a_handle_t h, uint8_t bits);
esp_err_t sy7636a_set_poweron_delay(
    sy7636a_handle_t h,
    uint8_t dly1_ms, uint8_t dly2_ms, uint8_t dly3_ms, uint8_t dly4_ms
);

sy7636a_fault_t sy7636a_fault_from_reg(uint8_t fault_reg);
const char* sy7636a_fault_name(sy7636a_fault_t fault);
const char* sy7636a_vldo_name(sy7636a_vldo_t vldo);

#ifdef __cplusplus
}
#endif
