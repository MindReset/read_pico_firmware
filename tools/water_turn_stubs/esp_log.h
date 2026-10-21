#pragma once
#include <stdio.h>
#define ESP_LOGI(tag, fmt, ...) do { char line[256]; (void)(tag); \
    (void)snprintf(line, sizeof(line), fmt, __VA_ARGS__); } while (0)
