/* SPDX-License-Identifier: Apache-2.0
 * 宿主端替身：把 ESP_LOGx 变成 printf，方便单测里直接看告警。
 * Host stub: turn ESP_LOGx into printf so tests can see the warnings.
 */
#pragma once
#include <stdio.h>
#define ESP_LOGE(tag, fmt, ...) printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) printf("[D][%s] " fmt "\n", tag, ##__VA_ARGS__)
