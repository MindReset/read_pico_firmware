/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef int nvs_handle_t;
typedef enum { NVS_TYPE_U8 = 0x01, NVS_TYPE_U32 = 0x04,
               NVS_TYPE_STR = 0x21, NVS_TYPE_BLOB = 0x42, NVS_TYPE_ANY = 0xff } nvs_type_t;
typedef struct nvs_iterator_opaque *nvs_iterator_t;
typedef struct { char namespace_name[16]; char key[16]; nvs_type_t type; } nvs_entry_info_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1
esp_err_t nvs_open(const char *, int, nvs_handle_t *);
void nvs_close(nvs_handle_t);
esp_err_t nvs_get_u8(nvs_handle_t, const char *, uint8_t *);
esp_err_t nvs_set_u8(nvs_handle_t, const char *, uint8_t);
esp_err_t nvs_get_u32(nvs_handle_t, const char *, uint32_t *);
esp_err_t nvs_set_u32(nvs_handle_t, const char *, uint32_t);
esp_err_t nvs_get_str(nvs_handle_t, const char *, char *, size_t *);
esp_err_t nvs_set_str(nvs_handle_t, const char *, const char *);
esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *, size_t *);
esp_err_t nvs_set_blob(nvs_handle_t, const char *, const void *, size_t);
esp_err_t nvs_erase_key(nvs_handle_t, const char *);
esp_err_t nvs_erase_all(nvs_handle_t);
esp_err_t nvs_commit(nvs_handle_t);
esp_err_t nvs_entry_find_in_handle(nvs_handle_t, nvs_type_t, nvs_iterator_t *);
esp_err_t nvs_entry_next(nvs_iterator_t *);
esp_err_t nvs_entry_info(nvs_iterator_t, nvs_entry_info_t *);
void nvs_release_iterator(nvs_iterator_t);
