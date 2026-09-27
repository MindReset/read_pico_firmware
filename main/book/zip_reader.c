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
#define DIRECTORY_MAX (8U * 1024U * 1024U)
#define DIRECTORY_WINDOW 65536U

typedef struct {
    uint32_t name_pos, hash;
    uint16_t name_len;
    uint32_t offset, packed, unpacked, crc;
    uint16_t method, flags;
} zip_entry_t;

struct zip_reader {
    FILE* file;
    zip_entry_t* entries;
    uint32_t size, directory;
    uint16_t count;
    char name_scratch[NAME_MAX_BYTES + 1];
};

static uint16_t u16(const uint8_t* p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t u32(const uint8_t* p) { return (uint32_t)u16(p) | ((uint32_t)u16(p + 2) << 16); }
static bool read_at(zip_reader_t* z, uint32_t pos, void* dst, size_t size) {
    return pos <= z->size && size <= z->size - pos &&
        fseek(z->file, (long)pos, SEEK_SET) == 0 && fread(dst, 1, size, z->file) == size;
}

static uint32_t name_hash(const char* name, size_t len) {
    uint32_t hash = UINT32_C(2166136261);
    while (len--) hash = (hash ^ (uint8_t)*name++) * UINT32_C(16777619);
    return hash;
}
static int entry_compare(const void* a, const void* b) {
    uint32_t x = ((const zip_entry_t*)a)->hash, y = ((const zip_entry_t*)b)->hash;
    return (x > y) - (x < y);
}
static bool entry_name(const zip_reader_t* z, const zip_entry_t* entry, char* out) {
    if (!read_at((zip_reader_t*)z, entry->name_pos, out, entry->name_len)) return false;
    out[entry->name_len] = 0;
    return true;
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
        u32(eocd + 12) == UINT32_MAX || u32(eocd + 16) == UINT32_MAX) goto fail;
    z->count = u16(eocd + 10);
    err = ESP_ERR_INVALID_SIZE;
    if (z->count > ZIP_ENTRY_MAX) goto fail;
    z->directory = u32(eocd + 16);
    uint32_t dir_size = u32(eocd + 12), eocd_pos = tail_pos + end;
    err = ESP_ERR_INVALID_SIZE;
    if (dir_size > DIRECTORY_MAX || z->directory > eocd_pos || dir_size != eocd_pos - z->directory) goto fail;
    free(tail); tail = NULL;
    // 固定窗口扫描目录，三万条记录也不复制整张目录。/ Scan with a fixed window even for thirty thousand entries.
    tail = heap_caps_malloc(DIRECTORY_WINDOW, PSRAM);
    if (!tail) { err = ESP_ERR_NO_MEM; goto fail; }
    uint32_t window_pos = UINT32_MAX;
    size_t window_len = 0;
    if (z->count) {
        z->entries = heap_caps_calloc(z->count, sizeof(*z->entries), PSRAM);
        if (!z->entries) { err = ESP_ERR_NO_MEM; goto fail; }
    }
    uint32_t pos = z->directory;
    for (unsigned i = 0; i < z->count; ++i) {
        err = ESP_ERR_INVALID_SIZE;
        if (pos > eocd_pos || eocd_pos - pos < 46U) goto fail;
        if (window_pos == UINT32_MAX || pos - window_pos + 46U > window_len) {
            window_pos = pos;
            window_len = eocd_pos - pos < DIRECTORY_WINDOW ? eocd_pos - pos : DIRECTORY_WINDOW;
            if (!read_at(z, pos, tail, window_len)) goto fail;
        }
        const uint8_t* h = tail + (pos - window_pos);
        if (u32(h) != UINT32_C(0x02014b50)) goto fail;
        zip_entry_t* entry = &z->entries[i];
        entry->flags = u16(h + 8); entry->method = u16(h + 10);
        entry->crc = u32(h + 16); entry->packed = u32(h + 20); entry->unpacked = u32(h + 24); entry->offset = u32(h + 42);
        uint16_t name_len = u16(h + 28), extra_len = u16(h + 30), comment_len = u16(h + 32);
        uint32_t record_len = 46U + (uint32_t)name_len + extra_len + comment_len;
        err = ESP_ERR_NOT_SUPPORTED;
        if ((entry->flags & ~UINT16_C(0x080e)) || u16(h + 34) ||
            (entry->method != 0 && entry->method != 8)) goto fail;
        err = ESP_ERR_INVALID_SIZE;
        if (entry->packed > ZIP_INPUT_MAX || entry->unpacked > ZIP_OUTPUT_MAX) goto fail;
        if (!name_len || name_len > NAME_MAX_BYTES || record_len > eocd_pos - pos ||
            entry->offset >= z->directory || z->directory - entry->offset < 30 ||
            entry->packed > z->directory - entry->offset - 30 ||
            (entry->method == 0 && entry->packed != entry->unpacked)) goto fail;
        entry->name_pos = pos + 46;
        entry->name_len = name_len;
        if (pos - window_pos + 46U + name_len > window_len) {
            window_pos = pos;
            window_len = eocd_pos - pos < DIRECTORY_WINDOW ? eocd_pos - pos : DIRECTORY_WINDOW;
            if (!read_at(z, pos, tail, window_len)) goto fail;
            h = tail;
        }
        if (memchr(h + 46, 0, name_len)) goto fail;
        entry->hash = name_hash((const char*)h + 46, name_len);
        if (!extras_valid(z, pos + 46U + name_len, extra_len)) goto fail;
        pos += record_len;
    }
    if (pos != eocd_pos) goto fail;
    if (z->count > 1) qsort(z->entries, z->count, sizeof(*z->entries), entry_compare);
    for (unsigned i = 1; i < z->count; ++i) {
        const zip_entry_t* a = &z->entries[i];
        char name[NAME_MAX_BYTES + 1];
        if (a->hash != z->entries[i - 1].hash) continue;
        if (!entry_name(z, a, name)) goto fail;
        for (unsigned j = i; j && z->entries[j - 1].hash == a->hash; --j) {
            if (!entry_name(z, &z->entries[j - 1], z->name_scratch)) goto fail;
            if (!strcmp(name, z->name_scratch)) goto fail;
        }
    }
    free(tail);
    *out = z;
    return ESP_OK;
fail:
    free(tail);
    zip_close(z);
    return err;
}

int zip_find(const zip_reader_t* z, const char* name) {
    if (!z || !name) return -1;
    uint32_t hash = name_hash(name, strlen(name));
    size_t lo = 0, hi = z->count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (z->entries[mid].hash < hash) lo = mid + 1; else hi = mid;
    }
    char candidate[NAME_MAX_BYTES + 1];
    for (; lo < z->count && z->entries[lo].hash == hash; ++lo)
        if (entry_name(z, &z->entries[lo], candidate) && !strcmp(candidate, name)) return (int)lo;
    return -1;
}

const char* zip_entry_name(const zip_reader_t* z, int index) {
    if (!z || index < 0 || index >= z->count) return NULL;
    char* name = ((zip_reader_t*)z)->name_scratch;
    return entry_name(z, &z->entries[index], name) ? name : NULL;
}

size_t zip_entry_count(const zip_reader_t* z) { return z ? z->count : 0; }

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
    if (name_len != entry->name_len || prefix > z->directory - entry->offset ||
        entry->packed > z->directory - entry->offset - prefix) return ESP_ERR_INVALID_SIZE;
    if (!(entry->flags & 8) && (u32(h + 14) != entry->crc || u32(h + 18) != entry->packed || u32(h + 22) != entry->unpacked)) return ESP_ERR_INVALID_SIZE;
    uint8_t name[NAME_MAX_BYTES];
    const char* expected_name = zip_entry_name(z, index);
    if (!expected_name) return ESP_FAIL;
    if (!read_at(z, entry->offset + 30, name, name_len) || memcmp(name, expected_name, name_len) ||
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
