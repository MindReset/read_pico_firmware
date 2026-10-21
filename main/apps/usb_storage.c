/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：卸载本机 FAT 后用 TinyUSB MSC 向电脑共享 TF 卡，退出时重新挂载。
 * English: Unmount local FAT before sharing SD by TinyUSB MSC, then remount on exit.
 * 冻结：固件和电脑绝不同时挂载 TF 卡；主机弹出时也不自动挂载到第二路径；不自动格式化。
 * Frozen: Firmware and host never mount SD together, including host eject; never auto-format.
 */
#include "ui_avatar.h"
#include "book_cover_jobs.h"
#include "book_lock_collage.h"
#include "push_sink.h"
#include "usb_storage.h"
#include <stdlib.h>
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_private/usb_phy.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "read_pico_sd.h"
#include "sdmmc_cmd.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"
#include "ttf_font.h"

static const char *TAG = "usb_storage";
static sdmmc_card_t *s_card;
static tinyusb_msc_storage_handle_t s_storage;
static usb_phy_handle_t s_serial_phy;
static bool s_host_ready, s_usb_ready, s_msc_ready;
static bool s_active;
static bool s_cleanup_pending;

// MSC 用完内部 PHY 后，把它交还给串口/JTAG，电脑才能再次自动刷写。
// Return the shared internal PHY to Serial/JTAG after MSC so flashing can reconnect.
static void restore_serial_phy(void) {
    if (s_serial_phy) return;
    const usb_phy_config_t config = {
        .controller = USB_PHY_CTRL_SERIAL_JTAG,
        .target = USB_PHY_TARGET_INT,
    };
    esp_err_t err = usb_new_phy(&config, &s_serial_phy);
    if (err != ESP_OK) ESP_LOGE(TAG, "USB Serial/JTAG restore failed: %s", esp_err_to_name(err));
}

static void release_serial_phy(void) {
    if (!s_serial_phy) return;
    esp_err_t err = usb_del_phy(s_serial_phy);
    if (err != ESP_OK) ESP_LOGW(TAG, "USB Serial/JTAG release failed: %s", esp_err_to_name(err));
    s_serial_phy = NULL;
}

bool usb_storage_active(void) { return s_active; }
bool usb_storage_connected(void) { return s_active && !s_cleanup_pending && tud_mounted(); }

void usb_storage_phy_init(void) {
    restore_serial_phy();
}

static void release_card(void) {
    if (s_card) { free(s_card); s_card = NULL; }
    if (s_host_ready) { sdmmc_host_deinit(); s_host_ready = false; }
}

static void restore_local(void) {
    release_card();
    esp_err_t err = read_pico_sd_remount();
    if (err != ESP_OK && err != ESP_ERR_NOT_FINISHED) ESP_LOGW(TAG, "SD remount: %s", esp_err_to_name(err));
}

esp_err_t usb_storage_start(void) {
    book_lock_collage_invalidate();
    // gen18：USB 存储要卸载本机 FAT 独享 TF 卡——武装中的推流让位并留痕。
    // 不停 BLE 的话，手机推来的 BEGIN 会在 part_open_append 撞上「卡没了」，
    // 回一个指错方向的 ERR(5)（覆盖表第 7 行；用户裁决：武装让位 + 提示）。
    // gen18: USB storage unmounts the local FAT to take the card alone - an
    // open push arm yields with a trace. Without stopping it, a pushed BEGIN
    // hits "card gone" in part_open_append and gets a misleading ERR(5)
    // (coverage-table row 7; the ruling: the arm yields, with a prompt).
    push_sink_cancel_why("USB 传书已接管，接收暂停");
    if (!(book_cover_jobs_quiesce(2000) && ui_avatar_quiesce(2000))) return ESP_ERR_NOT_FINISHED;
    if (s_cleanup_pending) return ESP_ERR_INVALID_STATE;
    if (s_active) return ESP_OK;
    read_pico_sd_info_t info = {0};
    if (read_pico_sd_get_info(&info) != ESP_OK || !info.mounted) return ESP_ERR_INVALID_STATE;
    // 字体文件在 SD 上，先关闭句柄再卸载。/ Close the SD-backed font before unmounting.
    if (!ttf_font_is_builtin()) ttf_font_open_builtin();
    esp_err_t err = read_pico_sd_sync();
    if (err != ESP_OK) return err;
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    // USB transfer uses the conservative 20 MHz SD clock.  The 1-bit bus is
    // already slower than the USB endpoint and 40 MHz can stall on long writes.
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = GPIO_NUM_38; slot.cmd = GPIO_NUM_42; slot.d0 = GPIO_NUM_44;
    slot.d1 = slot.d2 = slot.d3 = GPIO_NUM_NC;
    slot.cd = slot.wp = GPIO_NUM_NC;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    err = sdmmc_host_init();
    if (err != ESP_OK) goto fail;
    s_host_ready = true;
    err = sdmmc_host_init_slot(host.slot, &slot);
    if (err != ESP_OK) goto fail;
    s_card = calloc(1, sizeof(*s_card));
    if (!s_card) { err = ESP_ERR_NO_MEM; goto fail; }
    err = sdmmc_card_init(&host, s_card);
    if (err != ESP_OK) goto fail;
    // 电脑弹出磁盘时不可让 MSC 驱动自动挂载 /data；本机只通过 read_pico_sd 恢复 /sdcard。
    // Never auto-mount /data on host eject; only read_pico_sd may restore /sdcard.
    tinyusb_msc_driver_config_t msc = {.user_flags.auto_mount_off = 1};
    err = tinyusb_msc_install_driver(&msc);
    if (err != ESP_OK) goto fail;
    s_msc_ready = true;
    tinyusb_msc_storage_config_t storage = {
        .medium.card = s_card,
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB,
        .fat_fs = { .do_not_format = true, .config.max_files = 4 },
    };
    err = tinyusb_msc_new_storage_sdmmc(&storage, &s_storage);
    if (err != ESP_OK) goto fail;
    tinyusb_config_t usb = TINYUSB_DEFAULT_CONFIG();
    release_serial_phy();
    err = tinyusb_driver_install(&usb);
    if (err != ESP_OK) goto fail;
    s_usb_ready = true;
    s_active = true;
    ESP_LOGI(TAG, "TF card exposed as USB MSC");
    return ESP_OK;
fail:
    ESP_LOGE(TAG, "USB start failed: %s", esp_err_to_name(err));
    // 启动失败也走同一个收尾路径，失败的存储句柄必须保留供重试。
    // Failed starts use the same teardown; retain any storage handle that still needs cleanup.
    s_active = true;
    (void)usb_storage_stop();
    return err;
}

esp_err_t usb_storage_stop(void) {
    if (!s_active && !s_storage && !s_usb_ready && !s_msc_ready && !s_host_ready) {
        restore_serial_phy();
        return ESP_OK;
    }
    s_cleanup_pending = true;
    // 先断开电脑并等待同步写回调结束，再释放 USB 存储对象。
    // Disconnect and let the synchronous write callback finish before freeing USB storage.
    if (s_usb_ready) {
        tud_disconnect();
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    esp_err_t err = ESP_OK;
    for (int attempt = 0; s_storage && attempt < 20; ++attempt) {
        err = tinyusb_msc_delete_storage(s_storage);
        if (err == ESP_OK) { s_storage = NULL; break; }
        if (err != ESP_ERR_INVALID_STATE) goto cleanup;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (s_storage) goto cleanup;
    if (s_usb_ready) {
        err = tinyusb_driver_uninstall();
        if (err != ESP_OK) goto cleanup;
        s_usb_ready = false;
    }
    if (s_msc_ready) {
        err = tinyusb_msc_uninstall_driver();
        if (err != ESP_OK) goto cleanup;
        s_msc_ready = false;
    }

cleanup:
    // 所有退出路径都尝试归还串口；只有 USB/MSC 完全退出后才释放卡和恢复本机挂载。
    // Reclaim the console on every exit; free the card and remount only after USB/MSC fully stop.
    restore_serial_phy();
    if (s_storage || s_usb_ready || s_msc_ready) {
        // 暂停主循环的卡访问与自动休眠，保留句柄供“结束 USB 读卡”再次收尾。
        // Keep card access and idle sleep paused until End USB retries the retained handles.
        s_active = true;
        return err == ESP_OK ? ESP_ERR_INVALID_STATE : err;
    }
    s_active = false;
    s_cleanup_pending = false;
    restore_local();
    return ESP_OK;
}
