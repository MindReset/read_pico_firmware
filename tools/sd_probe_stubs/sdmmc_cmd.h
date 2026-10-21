/* SPDX-License-Identifier: Apache-2.0
 * SD 探测主机测试卡片结构。/ Host SD-probe card structure stub.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "driver/sdmmc_host.h"

typedef struct {
    struct { char name[8]; } cid;
    struct { uint32_t capacity, sector_size; } csd;
} sdmmc_card_t;

esp_err_t sdmmc_card_init(const sdmmc_host_t *host, sdmmc_card_t *card);
esp_err_t sdmmc_read_sectors(sdmmc_card_t *card, void *buffer, size_t start, size_t count);
