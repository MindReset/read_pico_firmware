/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef int nvs_handle_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1
esp_err_t nvs_open(const char*, int, nvs_handle_t*);
void nvs_close(nvs_handle_t);
esp_err_t nvs_get_blob(nvs_handle_t, const char*, void*, size_t*);
esp_err_t nvs_set_blob(nvs_handle_t, const char*, const void*, size_t);
esp_err_t nvs_get_str(nvs_handle_t, const char*, char*, size_t*);
esp_err_t nvs_set_str(nvs_handle_t, const char*, const char*);
esp_err_t nvs_erase_key(nvs_handle_t, const char*);
esp_err_t nvs_commit(nvs_handle_t);
esp_err_t nvs_get_u32(nvs_handle_t, const char*, uint32_t*);
esp_err_t nvs_set_u32(nvs_handle_t, const char*, uint32_t);
/* 枚举替身：只覆盖 book_progress_list 用到的部分。/ Iterator stand-in, only what book_progress_list uses. */
#define NVS_DEFAULT_PART_NAME "nvs"
typedef enum { NVS_TYPE_ANY = 0, NVS_TYPE_BLOB } nvs_type_t;
/* 真实 IDF 里也是不透明指针，所以调用方可以用 NULL 初始化和释放。
   Opaque pointer in real IDF too, so callers may initialise and release it with NULL. */
typedef struct nvs_iterator_stub* nvs_iterator_t;
typedef struct {
    char namespace_name[16];
    char key[16];
    nvs_type_t type;
} nvs_entry_info_t;
esp_err_t nvs_entry_find(const char*, const char*, nvs_type_t, nvs_iterator_t*);
esp_err_t nvs_entry_next(nvs_iterator_t*);
esp_err_t nvs_entry_info(nvs_iterator_t, nvs_entry_info_t*);
void nvs_release_iterator(nvs_iterator_t);
