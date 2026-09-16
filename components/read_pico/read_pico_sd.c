/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * SDMMC 探测、挂载、格式化。
 *
 * SDMMC probe, mount, and format.
 */

#include "read_pico_sd.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>

#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "read_pico_board.h"
#include "sdmmc_cmd.h"

#define SD_MOUNT_POINT "/sdcard"
#define SD_PIN_CLK GPIO_NUM_38
#define SD_PIN_CMD GPIO_NUM_42
#define SD_PIN_D0 GPIO_NUM_44

static const char* TAG = "sd_card";
static sdmmc_card_t* card;
static volatile int probe_state;
static read_pico_sd_info_t cached_info;

static void fill_info(read_pico_sd_info_t* info, esp_err_t mount_err) {
    memset(info, 0, sizeof(*info));
    info->present = read_pico_sd_present();
    info->error = mount_err;
    info->needs_format = info->present && !info->mounted
        && mount_err != ESP_OK
        && mount_err != ESP_ERR_TIMEOUT
        && mount_err != ESP_ERR_NOT_FOUND
        && mount_err != ESP_ERR_NOT_FINISHED;
    if (mount_err != ESP_OK || card == NULL) return;

    info->mounted = true;
    info->needs_format = false;
    strlcpy(info->name, card->cid.name, sizeof(info->name));
    info->capacity_bytes = (uint64_t)card->csd.capacity * card->csd.sector_size;
    uint64_t total_bytes = 0;
    info->error = esp_vfs_fat_info(
        SD_MOUNT_POINT, &total_bytes, &info->free_bytes
    );
}

static void ensure_font_dirs(void) {
    if (mkdir("/sdcard/assets", 0777) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "mkdir assets: %d", errno);
    }
    if (mkdir("/sdcard/assets/fonts", 0777) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "mkdir assets/fonts: %d", errno);
    }
    if (mkdir("/sdcard/fonts", 0777) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "mkdir fonts: %d", errno);
    }
}

static esp_err_t mount_card(bool format_if_failed) {
    if (card != NULL) return ESP_OK;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    // 1-bit 只能靠提时钟换带宽。20MHz 默认对随机小读太慢，40MHz 多数卡能稳住。
    // / 1-bit only buys bandwidth by raising the clock. 20 MHz is too slow for
    // random small reads; 40 MHz holds on most cards.
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = SD_PIN_CLK;
    slot.cmd = SD_PIN_CMD;
    slot.d0 = SD_PIN_D0;
    slot.d1 = GPIO_NUM_NC;
    slot.d2 = GPIO_NUM_NC;
    slot.d3 = GPIO_NUM_NC;
    slot.cd = GPIO_NUM_NC;
    slot.wp = GPIO_NUM_NC;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = format_if_failed,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };

    esp_err_t err = esp_vfs_fat_sdmmc_mount(
        SD_MOUNT_POINT, &host, &slot, &mount_config, &card
    );
    // 上电后首次时钟协商常超时，等卡就绪再挂一次。
    // / First clock negotiate after power-up often times out; wait and remount.
    if (err == ESP_ERR_TIMEOUT) {
        card = NULL;
        vTaskDelay(pdMS_TO_TICKS(200));
        err = esp_vfs_fat_sdmmc_mount(
            SD_MOUNT_POINT, &host, &slot, &mount_config, &card
        );
    }
    if (err != ESP_OK) {
        card = NULL;
        ESP_LOGW(TAG, "Mount failed: %s", esp_err_to_name(err));
    }
    return err;
}

static void close_card(void) {
    if (card != NULL) {
        esp_err_t err = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, card);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "unmount %s", esp_err_to_name(err));
        }
        card = NULL;
    }
}

static void probe_task(void* arg) {
    (void)arg;
    read_pico_sd_info_t info = { 0 };
    info.present = read_pico_sd_present();
    if (!info.present) {
        info.error = ESP_ERR_NOT_FOUND;
        cached_info = info;
        probe_state = 2;
        ESP_LOGI(TAG, "SD_CD absent, skip mount");
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = mount_card(false);
    fill_info(&info, err);
    if (err == ESP_OK) {
        ESP_LOGI(
            TAG,
            "Mounted %s, capacity=%llu MB, free=%llu MB",
            info.name,
            (unsigned long long)(info.capacity_bytes / (1024 * 1024)),
            (unsigned long long)(info.free_bytes / (1024 * 1024))
        );
    }
    cached_info = info;
    probe_state = 2;
    if (info.needs_format) {
        ESP_LOGW(TAG, "SD present but no FAT, ask user to format");
    }
    vTaskDelete(NULL);
}

esp_err_t read_pico_sd_start_probe(void) {
    if (probe_state == 1) return ESP_ERR_NOT_FINISHED;
    if (probe_state == 2) {
        if (cached_info.mounted) return ESP_OK;
        if (!cached_info.present) return ESP_ERR_NOT_FOUND;
        return cached_info.error != ESP_OK ? cached_info.error : ESP_FAIL;
    }

    memset(&cached_info, 0, sizeof(cached_info));
    cached_info.present = read_pico_sd_present();
    if (!cached_info.present) {
        cached_info.error = ESP_ERR_NOT_FOUND;
        probe_state = 2;
        ESP_LOGI(TAG, "SD_CD absent, skip mount");
        return ESP_ERR_NOT_FOUND;
    }
    cached_info.error = ESP_ERR_NOT_FINISHED;
    probe_state = 1;
    BaseType_t created = xTaskCreate(
        probe_task, "sd_probe", 4096, NULL, 5, NULL
    );
    if (created != pdPASS) {
        probe_state = 0;
        cached_info.error = ESP_ERR_NO_MEM;
        return ESP_ERR_NO_MEM;
    }
    return ESP_ERR_NOT_FINISHED;
}

esp_err_t read_pico_sd_get_info(read_pico_sd_info_t* info) {
    if (info == NULL) return ESP_ERR_INVALID_ARG;
    *info = cached_info;
    if (probe_state == 0) return ESP_ERR_INVALID_STATE;
    if (probe_state == 1) return ESP_ERR_NOT_FINISHED;
    return info->error;
}

esp_err_t read_pico_sd_remount(void) {
    if (probe_state == 1) return ESP_ERR_NOT_FINISHED;
    close_card();
    memset(&cached_info, 0, sizeof(cached_info));
    probe_state = 0;
    return read_pico_sd_start_probe();
}

esp_err_t read_pico_sd_sync(void) {
    if (probe_state == 1) return ESP_ERR_NOT_FINISHED;
    if (card == NULL) return ESP_OK;
    close_card();
    cached_info.mounted = false;
    return ESP_OK;
}

esp_err_t read_pico_sd_format(void) {
    if (probe_state == 1) return ESP_ERR_NOT_FINISHED;
    if (!read_pico_sd_present()) {
        cached_info.present = false;
        cached_info.mounted = false;
        cached_info.error = ESP_ERR_NOT_FOUND;
        probe_state = 2;
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = ESP_OK;
    if (card == NULL) {
        err = mount_card(true);
    } else {
        err = esp_vfs_fat_sdcard_format(SD_MOUNT_POINT, card);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "format %s", esp_err_to_name(err));
        }
    }

    read_pico_sd_info_t info = { 0 };
    fill_info(&info, err);
    if (err == ESP_OK) {
        ensure_font_dirs();
        fill_info(&info, ESP_OK);
        ESP_LOGI(TAG, "formatted %s", info.name);
    }
    cached_info = info;
    probe_state = 2;
    return err;
}
