/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：检查并安装 TF 卡根目录中的 Pico OTA 应用镜像。
 * English: inspect and install a Pico OTA application image from the TF-card root.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PICO_OTA_UPDATE_PATH "/sdcard/Pico-update.bin"

typedef struct {
    char current_version[33];
    char candidate_version[33];
    char message[96];
    size_t image_size;
    bool ready;
} pico_ota_info_t;

/// 检查升级包头、项目名、版本和空闲 OTA 分区。/ Check image header, project, version, and inactive slot.
esp_err_t pico_ota_inspect(const char *path, pico_ota_info_t *info);

/// 把已检查的应用镜像写入空闲槽并设为下次启动。/ Write a checked image to the inactive slot and select it for next boot.
esp_err_t pico_ota_install(const char *path, char *message, size_t message_size);

/// 在硬件与界面启动成功后确认新槽，取消自动回退。/ Confirm a newly booted slot after hardware and UI startup.
void pico_ota_confirm_running(void);

#ifdef __cplusplus
}
#endif
