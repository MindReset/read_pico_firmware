/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 单blob凭据读写，失败不更新公开快照。/ Single-blob credential I/O; failures do not update public snapshots.
 */
#pragma once
#ifndef READ_PICO_TRANSFER_NVS_TEST
#include "nvs.h"
#endif
#include "transfer_policy.h"

static esp_err_t load_credentials(transfer_credentials_t *out) {
    memset(out, 0, sizeof(*out));
    nvs_handle_t handle;
    esp_err_t err = nvs_open("rp_wifi", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    size_t size = sizeof(*out);
    err = nvs_get_blob(handle, "credentials", out, &size);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) { memset(out, 0, sizeof(*out)); return ESP_OK; }
    if (err != ESP_OK || size != sizeof(*out) || !credentials_valid(out)) {
        clear_secret(out, sizeof(*out));
        return err == ESP_OK ? ESP_ERR_INVALID_STATE : err;
    }
    return ESP_OK;
}

static esp_err_t store_credentials(const transfer_credentials_t *next) {
    if (next && !credentials_valid(next)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("rp_wifi", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = next ? nvs_set_blob(handle, "credentials", next, sizeof(*next)) : nvs_erase_key(handle, "credentials");
    if (!next && err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
