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
/// 查询实时 CD 并锁存失效，不卸载卡；拔卡后容量清零，重新插入也须显式 remount。
/// / Sample live CD and latch invalidation without unmounting; removal clears capacity and reinsertion requires explicit remount.
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t* info);
/// 调用前关闭所有字体、阅读及上传句柄；这是拔卡失效后恢复挂载的唯一入口。
/// / Close all font, reader and upload handles first; this is the only recovery entry after removal invalidation.
esp_err_t read_pico_sd_remount(void);
/// 仅显式用户确认后调用；失效挂载必须先 remount，绝不自动建议格式化拔出的卡。
/// / Explicit user confirmation only; an invalidated mount needs remount first, never a removal-triggered format suggestion.
esp_err_t read_pico_sd_format(void);
/// 已挂载则卸载落盘。探测未完成返回 ESP_ERR_NOT_FINISHED。
/// / Unmount if mounted so writes land. ESP_ERR_NOT_FINISHED while probing.
esp_err_t read_pico_sd_sync(void);

#ifdef __cplusplus
}
#endif
