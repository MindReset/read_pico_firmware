/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 小纸 Pico SDMMC 探测、挂载、格式化。
 *
 * Read Pico SDMMC probe, mount, and format.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool present;
    bool mounted;
    bool needs_format;
    char name[8];
    uint64_t capacity_bytes;
    uint64_t free_bytes;
    esp_err_t error;
} read_pico_sd_info_t;

esp_err_t read_pico_sd_start_probe(void);
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t* info);
esp_err_t read_pico_sd_remount(void);
esp_err_t read_pico_sd_format(void);
/// 已挂载则卸载落盘。探测未完成返回 ESP_ERR_NOT_FINISHED。
/// / Unmount if mounted so writes land. ESP_ERR_NOT_FINISHED while probing.
esp_err_t read_pico_sd_sync(void);

#ifdef __cplusplus
}
#endif
