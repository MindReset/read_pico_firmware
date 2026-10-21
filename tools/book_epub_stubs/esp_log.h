/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：宿主书源测试的日志替身。
 * English: Log shim for the host book-source test.
 * 冻结：仅供测试。/ Frozen: tests only.
 */
#pragma once
#define ESP_LOGE(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGD(tag, ...) ((void)(tag))
