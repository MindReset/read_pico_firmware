/* SPDX-License-Identifier: Apache-2.0
 * SD 探测主机测试任务接口。/ Host SD-probe task stub.
 */
#pragma once

#include "freertos/FreeRTOS.h"

BaseType_t xTaskCreate(void (*fn)(void*), const char* name, int stack,
                       void* arg, int priority, void* handle);
void vTaskDelay(int ticks);
void vTaskDelete(void* handle);
