/* SPDX-License-Identifier: Apache-2.0
 * SD 探测主机测试临界区接口。/ Host SD-probe critical-section stub.
 */
#pragma once

typedef int BaseType_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
