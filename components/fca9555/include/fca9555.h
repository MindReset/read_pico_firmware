/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * FCA9555 / xCA9555 16 位 I2C GPIO 扩展（Nyfea，兼容 PCA9555）。
 * 总线由调用方持有。读走写指针 + Repeated START（手册图 5/6）。
 * INT# 开漏、不锁存；读 Input 就松开。
 *
 * FCA9555 / xCA9555 16-bit I2C GPIO expander (Nyfea, PCA9555-compatible).
 * Caller owns the i2c_master bus. Read uses write pointer + Repeated START
 * (datasheet Figure 5/6). INT# is open-drain, not latched; reading Input
 * releases it.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fca9555_dev_t* fca9555_handle_t;

#define FCA9555_ADDR_BASE 0x20
#define FCA9555_ADDR_DEFAULT 0x24
#define FCA9555_I2C_HZ_DEFAULT 400000

#define FCA9555_I2C_TIMEOUT_MS_DEFAULT 100

/// 由 A2/A1/A0 得到 7 位地址。A2=1 A1=0 A0=0 → 0x24。
/// / 7-bit address from A2/A1/A0. A2=1 A1=0 A0=0 → 0x24.
#define FCA9555_ADDR_FROM_A(a2, a1, a0) \
    ((uint8_t)(FCA9555_ADDR_BASE | (((a2) & 1) << 2) | (((a1) & 1) << 1) | ((a0) & 1)))

#define FCA9555_P00 0x0001
#define FCA9555_P01 0x0002
#define FCA9555_P02 0x0004
#define FCA9555_P03 0x0008
#define FCA9555_P04 0x0010
#define FCA9555_P05 0x0020
#define FCA9555_P06 0x0040
#define FCA9555_P07 0x0080
#define FCA9555_P10 0x0100
#define FCA9555_P11 0x0200
#define FCA9555_P12 0x0400
#define FCA9555_P13 0x0800
#define FCA9555_P14 0x1000
#define FCA9555_P15 0x2000
#define FCA9555_P16 0x4000
#define FCA9555_P17 0x8000

#define FCA9555_REG_IN0 0
#define FCA9555_REG_IN1 1
#define FCA9555_REG_OUT0 2
#define FCA9555_REG_OUT1 3
#define FCA9555_REG_INV0 4
#define FCA9555_REG_INV1 5
#define FCA9555_REG_CFG0 6
#define FCA9555_REG_CFG1 7

/// 手册顺序 IN0/1 OUT0/1 INV0/1 CFG0/1。小端：Port0 在低字节。
/// / Datasheet order IN0/1 OUT0/1 INV0/1 CFG0/1. Little-endian: Port0 in the low byte.
typedef struct {
    uint16_t input;
    uint16_t output;
    uint16_t invert;
    uint16_t config;
} fca9555_map_t;

_Static_assert(sizeof(fca9555_map_t) == 8, "FCA9555 map must be 8 bytes");

typedef struct {
    uint8_t i2c_addr;
    uint32_t scl_speed_hz;
    gpio_num_t int_gpio;
    uint16_t i2c_timeout_ms;
} fca9555_config_t;

#define FCA9555_CONFIG_DEFAULT() \
    { \
        .i2c_addr = FCA9555_ADDR_DEFAULT, \
        .scl_speed_hz = FCA9555_I2C_HZ_DEFAULT, \
        .int_gpio = GPIO_NUM_NC, \
        .i2c_timeout_ms = FCA9555_I2C_TIMEOUT_MS_DEFAULT, \
    }

esp_err_t fca9555_init(i2c_master_bus_handle_t bus_handle, const fca9555_config_t* config,
                       fca9555_handle_t* handle);
esp_err_t fca9555_deinit(fca9555_handle_t handle);

esp_err_t fca9555_read_reg(fca9555_handle_t h, uint8_t reg, uint8_t* value);
esp_err_t fca9555_read_n(fca9555_handle_t h, uint8_t start, uint8_t* data, size_t n);
/// 连读全部 8 寄存器。刷新 CFG/INV 缓存。
/// / Sequential read of all 8 registers. Refreshes CFG/INV cache.
esp_err_t fca9555_read_all(fca9555_handle_t h, fca9555_map_t* map);
/// 连读 IN/OUT。CFG/INV 走缓存。
/// / Sequential read of IN/OUT. CFG/INV come from cache.
esp_err_t fca9555_read_io(fca9555_handle_t h, fca9555_map_t* map);
esp_err_t fca9555_read_input(fca9555_handle_t h, int port, uint8_t* value);
esp_err_t fca9555_set_output(fca9555_handle_t h, int port, uint8_t value);

/// 读 Input 以松开 INT#。Port1 空闲的板子只读 port 0。
/// / Read Input to release INT#. Port1 unused boards only need port 0.
esp_err_t fca9555_clear_int(fca9555_handle_t h);

int fca9555_int_level(fca9555_handle_t h);
bool fca9555_int_asserted(fca9555_handle_t h);
gpio_num_t fca9555_int_gpio(fca9555_handle_t h);

bool fca9555_dir_cached(fca9555_handle_t h);
void fca9555_get_dir_cache(fca9555_handle_t h, uint16_t* invert, uint16_t* config);

// ---------------------------------------------------------------------------
// 危险：改方向可能把感应脚（PGOOD、卡检测）打成输出。
// 极性翻转会把同样这些信号反过来。不要挂到产品界面。
// CFG 1=输入，0=输出。INV 1=翻转 Input。
// Dangerous: changing direction can drive a sense pin (PGOOD, card detect).
// Inverting polarity flips those same signals. Do not expose on product UI.
// CFG 1 = input, 0 = output. INV 1 = invert Input.
// ---------------------------------------------------------------------------
esp_err_t fca9555_set_config(fca9555_handle_t h, int port, uint8_t value);
esp_err_t fca9555_set_inversion(fca9555_handle_t h, int port, uint8_t value);

#ifdef __cplusplus
}
#endif
