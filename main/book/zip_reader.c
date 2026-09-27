/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 验证 ZIP 中央目录与本地头，用 ROM miniz 解压到调用方缓冲。
 * Validate ZIP central/local headers and inflate through ROM miniz into caller buffers.
 *
 * 冻结：只读文件，PSRAM 有界；拒绝加密、ZIP64、多磁盘及重复路径。
 * Frozen: read-only files and bounded PSRAM; reject encryption, ZIP64, multiple disks and duplicate paths.
 */
#include "zip_reader.h"
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "miniz.h"

#define PSRAM (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define NAME_MAX_BYTES 1024U
#define DIRECTORY_MAX (2U * 1024U * 1024U)

typedef struct {
    char* name;
    uint32_t offset, packed, unpacked, crc;
    uint16_t method, flags;
} zip_entry_t;

struct zip_reader {
    FILE* file;
    zip_entry_t* entries;
    uint32_t size, directory;
    uint16_t count;
};

static uint16_t u16(const uint8_t* p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t u32(const uint8_t* p) { return (uint32_t)u16(p) | ((uint32_t)u16(p + 2) << 16); }
static bool read_at(zip_reader_t* z, uint32_t pos, void* dst, size_t size) {
    return pos <= z->size && size <= z->size - pos &&
        fseek(z->file, (long)pos, SEEK_SET) == 0 && fread(dst, 1, size, z->file) == size;
}

// ZIP64 扩展即使没有哨兵值也拒绝；扩展字段必须完整。
// Reject ZIP64 extras even without sentinel sizes; every extra field must be complete.
static bool extras_valid(zip_reader_t* z, uint32_t pos, uint16_t len) {
    while (len) {
        uint8_t h[4];
        if (len < 4 || !read_at(z, pos, h, 4) || u16(h) == 1 || u16(h + 2) > len - 4) return false;
        uint32_t step = 4U + u16(h + 2);
        pos += step;
        len = (uint16_t)(len - step);
    }
    return true;
}

static uint32_t zip_crc32(const uint8_t* data, size_t len) {
    uint32_t crc = UINT32_MAX;
    while (len--) {
        crc ^= *data++;
        for (int i = 0; i < 8; ++i) crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & (0U - (crc & 1U)));
    }
    return ~crc;
}

void zip_close(zip_reader_t* z) {
    if (!z) return;
    if (z->file) fclose(z->file);
    if (z->entries) for (unsigned i = 0; i < z->count; ++i) free(z->entries[i].name);
    free(z->entries);
    free(z);
}

esp_err_t zip_open(const char* path, zip_reader_t** out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    if (!path || !*path) return ESP_ERR_INVALID_ARG;
    zip_reader_t* z = calloc(1, sizeof(*z));
    if (!z) return ESP_ERR_NO_MEM;
    esp_err_t err = ESP_ERR_INVALID_SIZE;
    uint8_t* tail = NULL;
    z->file = fopen(path, "rb");
    if (!z->file) { err = ESP_ERR_NOT_FOUND; goto fail; }
    if (fseek(z->file, 0, SEEK_END)) goto fail;
    long size = ftell(z->file);
    if (size < 22 || (uint64_t)size > UINT32_MAX || (uint64_t)size > INT32_MAX) goto fail;
    z->size = (uint32_t)size;
    size_t tail_len = z->size < 66U * 1024U ? z->size : 66U * 1024U;
    tail = heap_caps_malloc(tail_len, PSRAM);
    if (!tail) { err = ESP_ERR_NO_MEM; goto fail; }
    uint32_t tail_pos = z->size - tail_len;
    if (!read_at(z, tail_pos, tail, tail_len)) goto fail;
    size_t end = tail_len - 22;
    for (;;) {
        if (u32(tail + end) == UINT32_C(0x06054b50) && end + 22U + u16(tail + end + 20) == tail_len) break;
        if (!end) goto fail;
        --end;
    }
    const uint8_t* eocd = tail + end;
    err = ESP_ERR_NOT_SUPPORTED;
    if (u16(eocd + 4) || u16(eocd + 6) || u16(eocd + 8) != u16(eocd + 10) ||
        u16(eocd + 10) > ZIP_ENTRY_MAX || u32(eocd + 12) == UINT32_MAX || u32(eocd + 16) == UINT32_MAX) goto fail;
    z->count = u16(eocd + 10);
    z->directory = u32(eocd + 16);
    uint32_t dir_size = u32(eocd + 12), eocd_pos = tail_pos + end;
    err = ESP_ERR_INVALID_SIZE;
    if (dir_size > DIRECTORY_MAX || z->directory > eocd_pos || dir_size != eocd_pos - z->directory) goto fail;
    free(tail); tail = NULL;
    if (z->count) {
        z->entries = heap_caps_calloc(z->count, sizeof(*z->entries), PSRAM);
        if (!z->entries) { err = ESP_ERR_NO_MEM; goto fail; }
    }
    uint32_t pos = z->directory;
    for (unsigned i = 0; i < z->count; ++i) {
        uint8_t h[46];
        err = ESP_ERR_INVALID_SIZE;
        if (pos > eocd_pos || eocd_pos - pos < sizeof(h) || !read_at(z, pos, h, sizeof(h)) || u32(h) != UINT32_C(0x02014b50)) goto fail;
        zip_entry_t* entry = &z->entries[i];
        entry->flags = u16(h + 8); entry->method = u16(h + 10);
        entry->crc = u32(h + 16); entry->packed = u32(h + 20); entry->unpacked = u32(h + 24); entry->offset = u32(h + 42);
        uint16_t name_len = u16(h + 28), extra_len = u16(h + 30), comment_len = u16(h + 32);
        uint32_t record_len = sizeof(h) + (uint32_t)name_len + extra_len + comment_len;
        err = ESP_ERR_NOT_SUPPORTED;
        if ((entry->flags & ~UINT16_C(0x080e)) || u16(h + 34) ||
            (entry->method != 0 && entry->method != 8) || entry->packed > ZIP_INPUT_MAX || entry->unpacked > ZIP_OUTPUT_MAX) goto fail;
        err = ESP_ERR_INVALID_SIZE;
        if (!name_len || name_len > NAME_MAX_BYTES || record_len > eocd_pos - pos ||
            entry->offset >= z->directory || z->directory - entry->offset < 30 ||
            entry->packed > z->directory - entry->offset - 30 ||
            (entry->method == 0 && entry->packed != entry->unpacked)) goto fail;
        entry->name = heap_caps_malloc((size_t)name_len + 1, PSRAM);
        if (!entry->name) { err = ESP_ERR_NO_MEM; goto fail; }
        if (!read_at(z, pos + 46, entry->name, name_len) || memchr(entry->name, 0, name_len)) goto fail;
        entry->name[name_len] = 0;
        for (unsigned j = 0; j < i; ++j) if (!strcmp(entry->name, z->entries[j].name)) goto fail;
        if (!extras_valid(z, pos + 46 + name_len, extra_len)) goto fail;
        pos += record_len;
    }
    if (pos != eocd_pos) goto fail;
    *out = z;
    return ESP_OK;
fail:
    free(tail);
    zip_close(z);
    return err;
}

int zip_find(const zip_reader_t* z, const char* name) {
    if (z && name) for (unsigned i = 0; i < z->count; ++i) if (!strcmp(z->entries[i].name, name)) return (int)i;
    return -1;
}

size_t zip_entry_size(const zip_reader_t* z, int index) {
    return z && index >= 0 && index < z->count ? z->entries[index].unpacked : 0;
}

esp_err_t zip_extract(zip_reader_t* z, int index, void* dst, size_t cap) {
    if (!z || index < 0 || index >= z->count) return ESP_ERR_INVALID_ARG;
    const zip_entry_t* entry = &z->entries[index];
    if (cap < entry->unpacked || (!dst && entry->unpacked)) return ESP_ERR_INVALID_SIZE;
    uint8_t h[30];
    if (!read_at(z, entry->offset, h, sizeof(h)) || u32(h) != UINT32_C(0x04034b50) ||
        u16(h + 6) != entry->flags || u16(h + 8) != entry->method ||
        u32(h + 18) == UINT32_MAX || u32(h + 22) == UINT32_MAX) return ESP_ERR_INVALID_SIZE;
    uint16_t name_len = u16(h + 26), extra_len = u16(h + 28);
    uint32_t prefix = 30U + name_len + extra_len;
    if (name_len != strlen(entry->name) || prefix > z->directory - entry->offset ||
        entry->packed > z->directory - entry->offset - prefix) return ESP_ERR_INVALID_SIZE;
    if (!(entry->flags & 8) && (u32(h + 14) != entry->crc || u32(h + 18) != entry->packed || u32(h + 22) != entry->unpacked)) return ESP_ERR_INVALID_SIZE;
    uint8_t name[NAME_MAX_BYTES];
    if (!read_at(z, entry->offset + 30, name, name_len) || memcmp(name, entry->name, name_len) ||
        !extras_valid(z, entry->offset + 30 + name_len, extra_len)) return ESP_ERR_INVALID_SIZE;
    uint32_t data_pos = entry->offset + prefix;
    uint8_t empty_output;
    uint8_t* output = dst ? dst : &empty_output;
    if (entry->method == 0) {
        if (!read_at(z, data_pos, output, entry->unpacked)) return ESP_FAIL;
    } else {
        uint8_t* input = heap_caps_malloc(entry->packed ? entry->packed : 1, PSRAM);
        tinfl_decompressor* state = heap_caps_malloc(sizeof(*state), PSRAM);
        if (!input || !state) { free(input); free(state); return ESP_ERR_NO_MEM; }
        bool ok = read_at(z, data_pos, input, entry->packed);
        if (ok) {
            tinfl_init(state);
            size_t in_size = entry->packed, out_size = entry->unpacked;
            tinfl_status status = tinfl_decompress(state, input, &in_size, output, output, &out_size,
                                                   TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
            ok = status == TINFL_STATUS_DONE && in_size == entry->packed && out_size == entry->unpacked;
        }
        free(input); free(state);
        if (!ok) return ESP_ERR_INVALID_SIZE;
    }
    return zip_crc32(output, entry->unpacked) == entry->crc ? ESP_OK : ESP_ERR_INVALID_CRC;
}
