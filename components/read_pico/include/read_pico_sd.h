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
    // capacity 是物理卡大小；已用/可用只按挂载的文件系统统计。
    // Physical card size is separate from the mounted filesystem's usable capacity.
    uint64_t filesystem_bytes;
    uint64_t free_bytes;
    bool space_valid;
    esp_err_t space_error;
    esp_err_t error;
} read_pico_sd_info_t;

/// 首次无卡开机后插卡可重新探测；已挂载卡拔出失效仍须显式 remount。
/// / Probe a first insertion after an empty boot; a removed mounted card still needs explicit remount.
esp_err_t read_pico_sd_start_probe(void);
/// 查询有效 CD 并锁存拔卡失效；检测通信失败保留快照。重新插入须显式 remount。
/// / Latch removal only from valid CD; detection I/O failure preserves snapshots. Reinsertion needs explicit remount.
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t* info);
/// 更新空间快照，不重挂载、不写卡；勿在 render 中调用。
/// Refresh allocation statistics without remounting or writing; never call from render.
esp_err_t read_pico_sd_refresh_space(void);
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
