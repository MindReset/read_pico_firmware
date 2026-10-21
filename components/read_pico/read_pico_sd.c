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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "read_pico_board.h"
#include "sdmmc_cmd.h"
#include "ff.h"
#include "diskio_sdmmc.h"

#define SD_MOUNT_POINT "/sdcard"
#define SD_PIN_CLK GPIO_NUM_38
#define SD_PIN_CMD GPIO_NUM_42
#define SD_PIN_D0 GPIO_NUM_44

static const char* TAG = "sd_card";
static sdmmc_card_t* card;
static int probe_state;
static bool filesystem_unreadable;
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static bool media_invalidated;
static read_pico_sd_info_t cached_info;

// 失效只影响快照，消费者释放句柄前不得卸载卡。/ Invalidation changes snapshots only; consumers must close handles before unmount.
static void observe_media_locked(bool present) {
    bool inserted = present && !cached_info.present;
    if (!present && (cached_info.mounted || probe_state == 1)) media_invalidated = true;
    // 首次开机无卡时没有文件句柄；后插卡可重新探测。
    // A card inserted after an empty boot has no stale handles and can be probed.
    if (inserted && probe_state == 2 && !media_invalidated && !cached_info.mounted) {
        probe_state = 0;
        cached_info.error = ESP_ERR_INVALID_STATE;
    }
    cached_info.present = present;
    if (!present || media_invalidated) {
        memset(&cached_info, 0, sizeof(cached_info));
        cached_info.present = present;
        cached_info.error = present ? ESP_ERR_INVALID_STATE : ESP_ERR_NOT_FOUND;
    }
}

static void publish_info(const read_pico_sd_info_t* info) {
    bool present = false;
    esp_err_t detect_err = read_pico_sd_detect(&present);
    portENTER_CRITICAL(&state_lock);
    cached_info = *info;
    // 已经确认的拔卡失效不会被后续通信错误或探测成功覆盖。
    // A confirmed removal latch survives later detection faults and probe success.
    if (detect_err == ESP_OK || media_invalidated)
        observe_media_locked(detect_err == ESP_OK ? present : cached_info.present);
    probe_state = 2;
    portEXIT_CRITICAL(&state_lock);
}

static esp_err_t fill_space(read_pico_sd_info_t *info) {
    uint64_t total = 0, available = 0;
    esp_err_t err = esp_vfs_fat_info(SD_MOUNT_POINT, &total, &available);
    // 出错或损坏的计数不等于「卡已满」，也不能发生无符号减法下溢。
    // Failed/corrupt counters never mean a full card, and must not underflow subtraction.
    if (err == ESP_OK && (!total || available > total || total > info->capacity_bytes)) err = ESP_FAIL;
    info->space_valid = err == ESP_OK;
    info->space_error = err;
    info->filesystem_bytes = err == ESP_OK ? total : 0;
    info->free_bytes = err == ESP_OK ? available : 0;
    return err;
}

// FAT32 的 FSInfo 可能仍是电脑上旧的计数。每次挂载仅一次作废它，首轮统计只读扫描 FAT。
// Invalidate possibly stale FAT32 FSInfo once per mount; the first read-only FAT scan then counts allocations.
static void invalidate_mount_space(void) {
    BYTE drive = ff_diskio_get_pdrv_card(card);
    if (drive == 0xff) return;
    char path[12]; snprintf(path,sizeof(path),"%u:",(unsigned)drive);
    FF_DIR root;
    if (f_opendir(&root,path) != FR_OK) return;
    FATFS *fs = root.obj.fs;
    (void)f_closedir(&root);
    if (!fs) return;
#if FF_FS_REENTRANT
    if (!ff_mutex_take(fs->ldrv)) return;
#endif
    fs->free_clst = UINT32_MAX;
#if FF_FS_REENTRANT
    ff_mutex_give(fs->ldrv);
#endif
}

static void fill_info(read_pico_sd_info_t* info, esp_err_t mount_err) {
    memset(info, 0, sizeof(*info));
    bool present = false;
    esp_err_t detect_err = read_pico_sd_detect(&present);
    // 成功挂载是有卡的证据；检测通信失败不覆盖挂载结果。
    // A successful mount proves presence; failed detection must not override it.
    info->present = detect_err == ESP_OK ? present : mount_err != ESP_ERR_NOT_FOUND;
    info->error = mount_err;
    // 挂载错误本身不证明文件系统问题；仅只读识别到 exFAT 或空白卡才提示。
    // Mount errors alone are inconclusive; only read-only confirmation of exFAT/blank media enables the hint.
    info->needs_format = info->present && filesystem_unreadable;
    if (mount_err != ESP_OK || card == NULL) return;

    info->mounted = true;
    info->needs_format = false;
    strlcpy(info->name, card->cid.name, sizeof(info->name));
    info->capacity_bytes = (uint64_t)card->csd.capacity * card->csd.sector_size;
    // 空间统计失败不覆盖「挂载成功」，仍允许正常读取文件。
    // Allocation-query failure must not turn a mounted filesystem into a mount failure.
    (void)fill_space(info);
}

static void ensure_media_dirs(void) {
    if (mkdir("/sdcard/assets", 0777) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "mkdir assets: %d", errno);
    }
    if (mkdir("/sdcard/assets/fonts", 0777) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "mkdir assets/fonts: %d", errno);
    }
    if (mkdir("/sdcard/fonts", 0777) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "mkdir fonts: %d", errno);
    }
    if (mkdir("/sdcard/books", 0777) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "mkdir books: %d", errno);
    }
    if (mkdir("/sdcard/pictures", 0777) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "mkdir pictures: %d", errno);
    }
    if (mkdir("/sdcard/dict", 0777) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "mkdir dict: %d", errno);
    }
}

// 只识别明确不支持的文件系统，不写卡，也不根据通用 ESP_FAIL 猜测。
// Identify known unsupported media without writing or interpreting generic ESP_FAIL as a filesystem error.
static bool unsupported_boot_sector(const uint8_t *sector) {
    if (!memcmp(sector + 3, "EXFAT   ", 8)) return true;
    for (unsigned i = 0; i < 512; ++i) if (sector[i]) return false;
    return true;
}
static bool probe_unsupported_filesystem(sdmmc_host_t host, const sdmmc_slot_config_t *slot) {
    sdmmc_card_t *probe = calloc(1, sizeof(*probe));
    uint8_t *sector = heap_caps_malloc(512, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    bool owned = false, unsupported = false;
    if (!probe || !sector) goto done;
    host.max_freq_khz = 10000;
    if (host.init() != ESP_OK) goto done; // 不接管其他已初始化会话。/ Never take over another initialized session.
    owned = true;
    if (sdmmc_host_init_slot(host.slot, slot) != ESP_OK || sdmmc_card_init(&host, probe) != ESP_OK ||
        probe->csd.sector_size != 512 || sdmmc_read_sectors(probe, sector, 0, 1) != ESP_OK) goto done;
    unsupported = unsupported_boot_sector(sector);
    if (!unsupported && sector[510] == 0x55 && sector[511] == 0xaa) {
        uint32_t starts[4] = {0};
        for (unsigned i = 0; i < 4; ++i) {
            const uint8_t *entry = sector + 446 + 16 * i;
            if (!entry[4]) continue;
            starts[i] = (uint32_t)entry[8] | (uint32_t)entry[9] << 8 | (uint32_t)entry[10] << 16 | (uint32_t)entry[11] << 24;
        }
        for (unsigned i = 0; i < 4 && !unsupported; ++i) {
            if (!starts[i] || starts[i] >= probe->csd.capacity) continue;
            if (sdmmc_read_sectors(probe, sector, starts[i], 1) != ESP_OK) goto done;
            // 分区必须明确标识 exFAT；普通 FAT 或未知损坏不建议格式化。
            // A partition must explicitly identify exFAT; do not suggest formatting FAT or unknown corruption.
            unsupported = !memcmp(sector + 3, "EXFAT   ", 8);
        }
    }
done:
    if (owned) host.deinit();
    free(probe); heap_caps_free(sector);
    return unsupported;
}

static esp_err_t mount_card(bool format_if_failed) {
    filesystem_unreadable = false;
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
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
    };

    // 冷启动先等待并重试高速，再降速；CMD6 忙也可能返回 INVALID_STATE，不得直接放弃。
    // Settle/retry high speed before lowering clocks; CMD6 busy may return INVALID_STATE and must retry.
    const int clocks[] = {SDMMC_FREQ_HIGHSPEED, SDMMC_FREQ_HIGHSPEED, 20000, 10000};
    esp_err_t err = ESP_FAIL;
    for (size_t attempt = 0; attempt < sizeof(clocks) / sizeof(clocks[0]); ++attempt) {
        bool present = false;
        esp_err_t detect_err = read_pico_sd_detect(&present);
        if (detect_err == ESP_OK && !present) return ESP_ERR_NOT_FOUND;
        if (detect_err != ESP_OK) ESP_LOGW(TAG, "SD detect unavailable: %s; try card I/O", esp_err_to_name(detect_err));
        host.max_freq_khz = clocks[attempt];
        card = NULL;
        err = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot, &mount_config, &card);
        if (err == ESP_OK) break;
        if (err == ESP_ERR_NO_MEM || err == ESP_ERR_INVALID_ARG) break;
        ESP_LOGW(TAG, "SD probe %u at %d kHz failed: %s", (unsigned)attempt + 1,
                 clocks[attempt], esp_err_to_name(err));
        card = NULL;
        // VFS mount 在失败时只清理自己建立的 host；不额外 deinit 别的存储会话。
        // Failed VFS mount cleans up its own host; never forcibly deinit another storage session.
        if (attempt + 1 < sizeof(clocks) / sizeof(clocks[0])) vTaskDelay(pdMS_TO_TICKS(250));
    }
    if (err != ESP_OK) {
        card = NULL;
        ESP_LOGW(TAG, "Mount failed: %s", esp_err_to_name(err));
        if (!format_if_failed && err == ESP_FAIL) filesystem_unreadable = probe_unsupported_filesystem(host, &slot);
    } else {
        invalidate_mount_space();
        ensure_media_dirs();
    }
    return err;
}

static esp_err_t close_card(void) {
    if (card != NULL) {
        esp_err_t err = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, card);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "unmount %s", esp_err_to_name(err));
            return err;
        }
        card = NULL;
    }
    return ESP_OK;
}

static void probe_task(void* arg) {
    (void)arg;
    read_pico_sd_info_t info = { 0 };
    esp_err_t detect_err = read_pico_sd_detect(&info.present);
    if (detect_err == ESP_OK && !info.present) {
        info.error = ESP_ERR_NOT_FOUND;
        publish_info(&info);
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
    publish_info(&info);
    vTaskDelete(NULL);
}

esp_err_t read_pico_sd_start_probe(void) {
    bool present = true;
    esp_err_t detect_err = read_pico_sd_detect(&present);
    portENTER_CRITICAL(&state_lock);
    if (detect_err == ESP_OK) observe_media_locked(present);
    esp_err_t err = ESP_OK;
    if (probe_state == 1) err = ESP_ERR_NOT_FINISHED;
    else if (media_invalidated) err = cached_info.error;
    else if (probe_state == 2) err = cached_info.mounted ? ESP_OK :
        cached_info.error != ESP_OK ? cached_info.error : ESP_FAIL;
    else if (!present) {
        probe_state = 2;
        err = ESP_ERR_NOT_FOUND;
    } else {
        memset(&cached_info, 0, sizeof(cached_info));
        cached_info.present = true;
        cached_info.error = ESP_ERR_NOT_FINISHED;
        probe_state = 1;
    }
    bool start = err == ESP_OK && probe_state == 1;
    portEXIT_CRITICAL(&state_lock);
    if (!start) return err;
    BaseType_t created = xTaskCreate(
        probe_task, "sd_probe", 4096, NULL, 5, NULL
    );
    if (created != pdPASS) {
        portENTER_CRITICAL(&state_lock);
        probe_state = 0;
        cached_info.error = ESP_ERR_NO_MEM;
        portEXIT_CRITICAL(&state_lock);
        return ESP_ERR_NO_MEM;
    }
    return ESP_ERR_NOT_FINISHED;
}

esp_err_t read_pico_sd_get_info(read_pico_sd_info_t* info) {
    if (info == NULL) return ESP_ERR_INVALID_ARG;
    bool present = false;
    esp_err_t detect_err = read_pico_sd_detect(&present);
    portENTER_CRITICAL(&state_lock);
    if (detect_err == ESP_OK) observe_media_locked(present);
    *info = cached_info;
    esp_err_t err = probe_state == 0 && detect_err != ESP_OK ? detect_err :
        !info->present || media_invalidated ? info->error :
        probe_state == 0 ? ESP_ERR_INVALID_STATE :
        probe_state == 1 ? ESP_ERR_NOT_FINISHED : info->error;
    portEXIT_CRITICAL(&state_lock);
    return err;
}

// 驱动操作期间占用忙状态，不在临界区执行 I/O。/ Reserve busy state across driver I/O outside the critical section.
static bool begin_operation(void) {
    portENTER_CRITICAL(&state_lock);
    bool ready = probe_state != 1;
    if (ready) probe_state = 1;
    portEXIT_CRITICAL(&state_lock);
    return ready;
}

esp_err_t read_pico_sd_refresh_space(void) {
    read_pico_sd_info_t info;
    esp_err_t err = read_pico_sd_get_info(&info);
    if (err != ESP_OK || !info.mounted) return err != ESP_OK ? err : ESP_ERR_INVALID_STATE;
    if (!begin_operation()) return ESP_ERR_NOT_FINISHED;
    err = fill_space(&info);
    publish_info(&info);
    return err;
}

esp_err_t read_pico_sd_remount(void) {
    if (!begin_operation()) return ESP_ERR_NOT_FINISHED;
    esp_err_t err = close_card();
    if (err != ESP_OK) {
        read_pico_sd_info_t info = { .present = true, .error = err };
        (void)read_pico_sd_detect(&info.present);
        publish_info(&info);
        return err;
    }
    portENTER_CRITICAL(&state_lock);
    memset(&cached_info, 0, sizeof(cached_info));
    media_invalidated = false;
    probe_state = 0;
    portEXIT_CRITICAL(&state_lock);
    return read_pico_sd_start_probe();
}

esp_err_t read_pico_sd_sync(void) {
    if (!begin_operation()) return ESP_ERR_NOT_FINISHED;
    esp_err_t err = close_card();
    if (err != ESP_OK) {
        read_pico_sd_info_t info = { .present = true, .mounted = true, .error = err };
        (void)read_pico_sd_detect(&info.present);
        publish_info(&info);
        return err;
    }
    read_pico_sd_info_t info = { .error = ESP_ERR_INVALID_STATE };
    publish_info(&info);
    return ESP_OK;
}

esp_err_t read_pico_sd_format(void) {
    read_pico_sd_info_t current;
    (void)read_pico_sd_get_info(&current);
    portENTER_CRITICAL(&state_lock);
    bool invalid = media_invalidated;
    bool busy = probe_state == 1;
    if (!invalid && !busy) probe_state = 1;
    portEXIT_CRITICAL(&state_lock);
    if (invalid) return current.present ? ESP_ERR_INVALID_STATE : ESP_ERR_NOT_FOUND;
    if (busy) return ESP_ERR_NOT_FINISHED;
    bool present = false;
    if (read_pico_sd_detect(&present) == ESP_OK && !present) {
        read_pico_sd_info_t info = { .error = ESP_ERR_NOT_FOUND };
        publish_info(&info);
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
        ensure_media_dirs();
        fill_info(&info, ESP_OK);
        ESP_LOGI(TAG, "formatted %s", info.name);
    }
    publish_info(&info);
    return err;
}
