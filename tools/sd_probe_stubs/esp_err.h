/* SPDX-License-Identifier: Apache-2.0
 * SD 探测主机测试错误码；不用于固件。/ Host SD-probe error codes; never used by firmware.
 */
#pragma once

typedef int esp_err_t;
enum {
    ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_NO_MEM = 0x101,
    ESP_ERR_INVALID_ARG = 0x102, ESP_ERR_INVALID_STATE = 0x103,
    ESP_ERR_NOT_FOUND = 0x104, ESP_ERR_TIMEOUT = 0x105,
    ESP_ERR_NOT_FINISHED = 0x106,
};
const char* esp_err_to_name(esp_err_t err);
