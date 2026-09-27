/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NVS_NOT_FOUND 3
#define ESP_ERR_NVS_INVALID_LENGTH 4
#define ESP_ERR_NVS_NOT_ENOUGH_SPACE 5
static inline const char* esp_err_to_name(int err) { (void)err; return "test"; }
