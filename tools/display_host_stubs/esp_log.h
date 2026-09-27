/* SPDX-License-Identifier: Apache-2.0
 * 测试日志替身。/ Test logging shim.
 */
#pragma once
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
