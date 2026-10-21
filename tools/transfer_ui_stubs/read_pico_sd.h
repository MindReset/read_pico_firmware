#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef struct { uint64_t capacity_bytes; } read_pico_sd_info_t;
static inline esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *info) {
    info->capacity_bytes = 64ULL * 1024 * 1024 * 1024;
    return ESP_OK;
}
