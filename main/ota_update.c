/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：本地 OTA 只接收完整 ESP 应用镜像，写入空闲槽并依赖 bootloader 回退。
 * English: local OTA accepts complete ESP app images, writes the inactive slot, and relies on bootloader rollback.
 *
 * 冻结：不得覆盖当前运行分区，不在校验结束前切换启动分区。
 * Frozen: never overwrite the running partition or switch boot slots before validation completes.
 */
#include "ota_update.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"

static const char *TAG = "kiikoread_ota";

static void set_message(char *out, size_t size, const char *text) {
    if (!out || !size) return;
    snprintf(out, size, "%s", text ? text : "");
}

static bool copy_field(char *out, size_t out_size, const char *field, size_t field_size) {
    if (!out || !out_size || !field) return false;
    size_t length = strnlen(field, field_size);
    if (!length || length == field_size || length >= out_size) return false;
    memcpy(out, field, length);
    out[length] = 0;
    return true;
}

static esp_err_t read_candidate(FILE *file, esp_image_header_t *header, esp_app_desc_t *desc) {
    esp_image_segment_header_t segment = {0};
    if (fseek(file, 0, SEEK_SET) != 0 || fread(header, 1, sizeof(*header), file) != sizeof(*header) ||
        fread(&segment, 1, sizeof(segment), file) != sizeof(segment) ||
        fread(desc, 1, sizeof(*desc), file) != sizeof(*desc)) return ESP_ERR_INVALID_SIZE;
    if (header->magic != ESP_IMAGE_HEADER_MAGIC || desc->magic_word != ESP_APP_DESC_MAGIC_WORD ||
        segment.data_len < sizeof(*desc)) return ESP_ERR_INVALID_RESPONSE;
    return ESP_OK;
}

esp_err_t pico_ota_inspect(const char *path, pico_ota_info_t *info) {
    if (!path || !info) return ESP_ERR_INVALID_ARG;
    memset(info, 0, sizeof(*info));
    const esp_app_desc_t *running_desc = esp_app_get_description();
    if (!copy_field(info->current_version, sizeof(info->current_version), running_desc->version,
                    sizeof(running_desc->version))) snprintf(info->current_version, sizeof(info->current_version), "未知");

    struct stat file_stat = {0};
    if (stat(path, &file_stat) != 0) {
        set_message(info->message, sizeof(info->message), "TF 卡根目录未找到固件升级包");
        return ESP_ERR_NOT_FOUND;
    }
    if (file_stat.st_size <= 0) {
        set_message(info->message, sizeof(info->message), "升级包为空");
        return ESP_ERR_INVALID_SIZE;
    }

    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (!target) {
        set_message(info->message, sizeof(info->message), "当前固件没有可用的 OTA 分区");
        return ESP_ERR_NOT_SUPPORTED;
    }
    if ((size_t)file_stat.st_size > target->size) {
        set_message(info->message, sizeof(info->message), "升级包超过固件分区容量");
        return ESP_ERR_INVALID_SIZE;
    }

    FILE *file = fopen(path, "rb");
    if (!file) {
        set_message(info->message, sizeof(info->message), "升级包无法读取");
        return ESP_FAIL;
    }
    esp_image_header_t header = {0};
    esp_app_desc_t candidate = {0};
    esp_err_t err = read_candidate(file, &header, &candidate);
    fclose(file);
    if (err != ESP_OK) {
        set_message(info->message, sizeof(info->message), "升级包不是有效的 kiikoread 固件");
        return err;
    }

    char current_project[33] = {0};
    char candidate_project[33] = {0};
    if (!copy_field(current_project, sizeof(current_project), running_desc->project_name,
                    sizeof(running_desc->project_name)) ||
        !copy_field(candidate_project, sizeof(candidate_project), candidate.project_name,
                    sizeof(candidate.project_name)) || strcmp(current_project, candidate_project) != 0) {
        set_message(info->message, sizeof(info->message), "升级包不属于当前 kiikoread 固件");
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (!copy_field(info->candidate_version, sizeof(info->candidate_version), candidate.version,
                    sizeof(candidate.version))) {
        set_message(info->message, sizeof(info->message), "升级包版本信息无效");
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (strcmp(info->current_version, info->candidate_version) == 0) {
        set_message(info->message, sizeof(info->message), "升级包与当前版本相同");
        return ESP_ERR_INVALID_STATE;
    }

    info->image_size = (size_t)file_stat.st_size;
    info->ready = true;
    set_message(info->message, sizeof(info->message), "升级包已通过初步检查");
    return ESP_OK;
}

esp_err_t pico_ota_install(const char *path, char *message, size_t message_size) {
    pico_ota_info_t info = {0};
    esp_err_t err = pico_ota_inspect(path, &info);
    if (err != ESP_OK) {
        set_message(message, message_size, info.message);
        return err;
    }

    FILE *file = fopen(path, "rb");
    if (!file) {
        set_message(message, message_size, "升级包无法读取");
        return ESP_FAIL;
    }
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    esp_ota_handle_t handle = 0;
    err = target ? esp_ota_begin(target, info.image_size, &handle) : ESP_ERR_NOT_FOUND;
    if (err != ESP_OK) {
        fclose(file);
        set_message(message, message_size, "无法准备空闲固件分区");
        return err;
    }

    // OTA 在主 UI 任务执行，缓冲区必须放在堆上，不能占用有限的任务栈。
    // OTA runs on the main UI task, so keep the transfer buffer off its limited stack.
    const size_t buffer_size = 8 * 1024;
    uint8_t *buffer = malloc(buffer_size);
    if (!buffer) {
        esp_ota_abort(handle);
        fclose(file);
        set_message(message, message_size, "内存不足，无法开始升级");
        return ESP_ERR_NO_MEM;
    }
    size_t total = 0;
    while (!feof(file)) {
        size_t count = fread(buffer, 1, buffer_size, file);
        if (count) {
            err = esp_ota_write(handle, buffer, count);
            if (err != ESP_OK) break;
            total += count;
        }
        if (ferror(file)) {
            err = ESP_FAIL;
            break;
        }
    }
    fclose(file);
    free(buffer);
    if (err == ESP_OK && total != info.image_size) err = ESP_ERR_INVALID_SIZE;
    if (err != ESP_OK) {
        esp_ota_abort(handle);
        set_message(message, message_size, "写入中断，当前版本保持不变");
        return err;
    }

    err = esp_ota_end(handle);
    if (err != ESP_OK) {
        set_message(message, message_size, "固件完整性校验失败");
        return err;
    }
    err = esp_ota_set_boot_partition(target);
    if (err != ESP_OK) {
        set_message(message, message_size, "无法切换到新固件");
        return err;
    }
    set_message(message, message_size, "安装完成，即将重新启动");
    ESP_LOGI(TAG, "OTA image %s (%u bytes) written to %s", info.candidate_version,
             (unsigned)info.image_size, target->label);
    return ESP_OK;
}

void pico_ota_confirm_running(void) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (!running || esp_ota_get_state_partition(running, &state) != ESP_OK ||
        state != ESP_OTA_IMG_PENDING_VERIFY) return;
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) ESP_LOGI(TAG, "running OTA slot confirmed: %s", running->label);
    else ESP_LOGE(TAG, "failed to confirm OTA slot: %s", esp_err_to_name(err));
}
