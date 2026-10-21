/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <inttypes.h>
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "soc/soc_caps.h"
#include "msc_storage.h"


#if SOC_SDMMC_HOST_SUPPORTED
#include "diskio_sdmmc.h"

static const char *TAG = "storage_sdmmc";

static sdmmc_card_t *_scard = NULL;

static esp_err_t storage_sdmmc_mount(BYTE pdrv)
{
    assert(_scard != NULL);
    ff_diskio_register_sdmmc(pdrv, _scard);
    ff_sdmmc_set_disk_status_check(pdrv, false);
    return ESP_OK;
}

static esp_err_t storage_sdmmc_unmount(void)
{
    BYTE pdrv;
    pdrv = ff_diskio_get_pdrv_card(_scard);
    if (pdrv == 0xff) {
        ESP_LOGE(TAG, "Invalid state");
        return ESP_ERR_INVALID_STATE;
    }

    char drv[3] = {(char)('0' + pdrv), ':', 0};
    f_mount(0, drv, 0);
    ff_diskio_unregister(pdrv);

    return ESP_OK;
}

static inline size_t storage_sdmmc_get_sector_count(void)
{
    assert(_scard);
    return (size_t)_scard->csd.capacity;
}

static inline size_t storage_sdmmc_get_sector_size(void)
{
    assert(_scard);
    assert(_scard->csd.sector_size != 0); // Sector size must be non-zero if SDMMC card was initialized correctly
    return (size_t)_scard->csd.sector_size;
}

static esp_err_t storage_sdmmc_check_range(uint32_t lba, uint32_t offset, size_t size,
                                           uint64_t *position)
{
    const uint32_t sector_size = storage_sdmmc_get_sector_size();
    const uint64_t capacity = (uint64_t)storage_sdmmc_get_sector_count() * sector_size;
    *position = (uint64_t)lba * sector_size + offset;
    ESP_RETURN_ON_FALSE(sector_size == 512, ESP_ERR_NOT_SUPPORTED, TAG,
                        "Unsupported sector size: %" PRIu32, sector_size);
    ESP_RETURN_ON_FALSE(*position <= capacity && size <= capacity - *position,
                        ESP_ERR_INVALID_SIZE, TAG, "MSC access beyond card capacity");
    return ESP_OK;
}

static esp_err_t storage_sdmmc_sector_read(uint32_t lba, uint32_t offset, size_t size, void *dest)
{
    assert(_scard);
    uint64_t position;
    ESP_RETURN_ON_ERROR(storage_sdmmc_check_range(lba, offset, size, &position), TAG, "Invalid MSC read");
    uint8_t *out = dest;
    uint8_t sector[512] __attribute__((aligned(4)));
    while (size) {
        uint32_t block = (uint32_t)(position / 512);
        size_t within = (size_t)(position % 512);
        size_t count = size >= 512 && !within ? size / 512 : 0;
        if (count > 16) count = 16;
        if (count) {
            ESP_RETURN_ON_ERROR(sdmmc_read_sectors(_scard, out, block, count), TAG, "MSC sector read failed");
            count *= 512;
        } else {
            ESP_RETURN_ON_ERROR(sdmmc_read_sectors(_scard, sector, block, 1), TAG, "MSC partial read failed");
            count = size < 512 - within ? size : 512 - within;
            memcpy(out, sector + within, count);
        }
        out += count;
        position += count;
        size -= count;
    }
    return ESP_OK;
}

static esp_err_t storage_sdmmc_sector_write(uint32_t lba, uint32_t offset, size_t size, const void *src)
{
    assert(_scard);
    uint64_t position;
    ESP_RETURN_ON_ERROR(storage_sdmmc_check_range(lba, offset, size, &position), TAG, "Invalid MSC write");
    const uint8_t *in = src;
    uint8_t sector[512] __attribute__((aligned(4)));
    while (size) {
        uint32_t block = (uint32_t)(position / 512);
        size_t within = (size_t)(position % 512);
        size_t count = size >= 512 && !within ? size / 512 : 0;
        if (count > 16) count = 16;
        if (count) {
            ESP_RETURN_ON_ERROR(sdmmc_write_sectors(_scard, in, block, count), TAG, "MSC sector write failed");
            count *= 512;
        } else {
            ESP_RETURN_ON_ERROR(sdmmc_read_sectors(_scard, sector, block, 1), TAG, "MSC read before partial write failed");
            count = size < 512 - within ? size : 512 - within;
            memcpy(sector + within, in, count);
            ESP_RETURN_ON_ERROR(sdmmc_write_sectors(_scard, sector, block, 1), TAG, "MSC partial write failed");
        }
        in += count;
        position += count;
        size -= count;
    }
    return ESP_OK;
}

static esp_err_t storage_sdmmc_get_info(storage_info_t *info)
{
    ESP_RETURN_ON_FALSE(info, ESP_ERR_INVALID_ARG, TAG, "Storage info pointer can't be NULL");

    info->total_sectors = (uint32_t) storage_sdmmc_get_sector_count();
    info->sector_size = (uint32_t) storage_sdmmc_get_sector_size();
    return ESP_OK;
}

static void storage_sdmmc_close(void)
{
    _scard = NULL;
}

// Constant struct of function pointers
const storage_medium_t sdmmc_storage_medium = {
    .type = STORAGE_MEDIUM_TYPE_SDMMC,
    .mount = &storage_sdmmc_mount,
    .unmount = &storage_sdmmc_unmount,
    .read = &storage_sdmmc_sector_read,
    .write = &storage_sdmmc_sector_write,
    .get_info = &storage_sdmmc_get_info,
    .close = &storage_sdmmc_close,
};

esp_err_t storage_sdmmc_open_medium(sdmmc_card_t *card, const storage_medium_t **medium)
{
    ESP_RETURN_ON_FALSE(medium != NULL, ESP_ERR_INVALID_ARG, TAG, "Storage API pointer can't be NULL");
    ESP_RETURN_ON_FALSE(card, ESP_ERR_INVALID_ARG, TAG, "SDMMC card handle can't be NULL");

    _scard = card;
    *medium = &sdmmc_storage_medium;
    return ESP_OK;
}
#endif // SOC_SDMMC_HOST_SUPPORTED
