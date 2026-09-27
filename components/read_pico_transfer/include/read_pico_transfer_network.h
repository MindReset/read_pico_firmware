/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 扫描结果类型，不暴露网络凭据。/ Scan result types without network credentials.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define READ_PICO_TRANSFER_SCAN_MAX 16
typedef struct {
    char ssid[33]; ///< 最多32字节SSID / SSID of at most 32 bytes
    int8_t rssi; ///< 信号强度dBm / Signal strength in dBm
    uint8_t authmode; ///< ESP-IDF认证类型数值 / ESP-IDF authentication mode value
    bool requires_password; ///< 需要密码 / Password required
    bool supported; ///< 当前凭据模型支持该认证 / Supported by the current credential model
} read_pico_transfer_network_t;
