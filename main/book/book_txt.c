/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：流式 TXT 编码识别、目录与按章转码。
 * English: Streaming TXT encoding detection, indexing and chapter conversion.
 * 冻结：原始偏移用于进度；目录上限 2048；不加载整本书。
 * Frozen: Progress uses source offsets; at most 2048 entries; never load the whole book.
 */
#include "book_source_internal.h"
#include "gbk.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#define READ_BLOCK (64 * 1024)
#define PART_TARGET (48 * 1024)
typedef struct {
    FILE *file; ///< 只读句柄 / Read-only handle
    unsigned char bytes[READ_BLOCK + 4]; ///< 跨块字符余量 / Room for a cross-block character
    size_t pos, used; ///< 缓冲游标 / Buffer cursors
    uint32_t offset; ///< 原文件游标 / Source cursor
    bool gbk; ///< 编码 / Encoding
} reader_t;

// 验证一个 UTF-8 标量；无效时仅消费首字节。/ Validate one UTF-8 scalar; consume only its first byte on error.
static size_t utf8_one(const unsigned char *s, size_t n, uint32_t *cp) {
    *cp = 0xfffd;
    if (!n) return 0;
    if (s[0] < 0x80) { *cp = s[0]; return 1; }
    size_t k = s[0] >= 0xc2 && s[0] <= 0xdf ? 2 : s[0] >= 0xe0 && s[0] <= 0xef ? 3 : s[0] >= 0xf0 && s[0] <= 0xf4 ? 4 : 0;
    if (!k || k > n) return 1;
    uint32_t c = s[0] & (0x7f >> k);
    for (size_t j = 1; j < k; ++j) { if ((s[j] & 0xc0) != 0x80) return 1; c = (c << 6) | (s[j] & 63); }
    if ((k == 2 && c < 0x80) || (k == 3 && c < 0x800) || (k == 4 && c < 0x10000) || (c >= 0xd800 && c <= 0xdfff) || c > 0x10ffff) return 1;
    *cp = c; return k;
}
static size_t encode(uint32_t cp, char out[4]) {
    if (!cp) cp = 0xfffd;
    if (cp < 0x80) { out[0] = cp; return 1; }
    if (cp < 0x800) { out[0] = 0xc0 | (cp >> 6); out[1] = 0x80 | (cp & 63); return 2; }
    if (cp < 0x10000) { out[0] = 0xe0 | (cp >> 12); out[1] = 0x80 | ((cp >> 6) & 63); out[2] = 0x80 | (cp & 63); return 3; }
    out[0] = 0xf0 | (cp >> 18); out[1] = 0x80 | ((cp >> 12) & 63); out[2] = 0x80 | ((cp >> 6) & 63); out[3] = 0x80 | (cp & 63); return 4;
}
static bool next(reader_t *r, uint32_t *cp) {
    if (r->used - r->pos < 4) {
        size_t tail = r->used - r->pos;
        memmove(r->bytes, r->bytes + r->pos, tail);
        r->used = tail + fread(r->bytes + tail, 1, READ_BLOCK, r->file); r->pos = 0;
    }
    size_t n = r->used - r->pos;
    if (!n) return false;
    const unsigned char *s = r->bytes + r->pos; size_t k = 1;
    if (!r->gbk) k = utf8_one(s, n, cp);
    else if (s[0] < 0x80) *cp = s[0];
    else {
        *cp = 0xfffd;
        if (s[0] >= 0x81 && s[0] <= 0xfe && n > 1 && s[1] >= 0x40 && s[1] <= 0xfe && s[1] != 0x7f) { *cp = gbk_codepoint(s[0], s[1]); k = 2; }
    }
    r->pos += k; r->offset += k; return true;
}
static esp_err_t reset(reader_t *r, book_txt_t *b, uint32_t offset) {
    memset(r, 0, sizeof(*r)); r->file = b->file; r->gbk = b->gbk; r->offset = offset;
    clearerr(b->file);
    return fseek(b->file, (long)offset, SEEK_SET) ? ESP_FAIL : ESP_OK;
}
static bool heading(const char *s) {
    if (!strncmp(s, "Chapter", 7)) {
        s += 7; while (*s == ' ' || *s == '\t') ++s;
        return isdigit((unsigned char)*s) != 0;
    }
    if (strncmp(s, "第", 3)) return false;
    s += 3; bool number = false;
    while (*s) {
        uint32_t cp; size_t n = utf8_one((const unsigned char *)s, strlen(s), &cp);
        char token[5] = {0}; memcpy(token, s, n);
        if ((cp >= '0' && cp <= '9') || (n == 3 && strstr("零一二三四五六七八九十百千万两", token))) { number = true; s += n; }
        else return number && n == 3 && strstr("章节卷回集部篇", token);
    }
    return false;
}
static bool add(book_txt_t *b, uint32_t offset, const char *title) {
    if (b->count == BOOK_CHAPTER_MAX) return false;
    book_entry_t *e = &b->entries[b->count++]; e->offset = offset;
    if (title) memcpy(e->title, title, strlen(title) + 1);
    else snprintf(e->title, sizeof(e->title), "Part %u", (unsigned)b->count);
    return true;
}
static esp_err_t index_book(reader_t *r, book_txt_t *b) {
    esp_err_t err = reset(r, b, b->bom); if (err) return err;
    char line[41] = {0}; size_t len = 0; bool overflow = false, too_many = false;
    uint32_t start = b->bom, cp;
    while (next(r, &cp)) {
        if (cp == '\n') {
            if (!overflow && heading(line) && !add(b, start, line)) too_many = true;
            start = r->offset; len = 0; line[0] = 0; overflow = false;
        } else if (cp != '\r') {
            char bytes[4]; size_t n = encode(cp, bytes);
            if (len + n <= 40 && !overflow) { memcpy(line + len, bytes, n); len += n; line[len] = 0; }
            else overflow = true;
        }
    }
    if (ferror(b->file)) return ESP_FAIL;
    if (!overflow && heading(line) && !add(b, start, line)) too_many = true;
    bool fallback = b->count < 2 || too_many;
    if (b->count) b->entries[0].offset = 0;
    for (size_t i = 0; i < b->count; ++i) {
        uint32_t end = i + 1 < b->count ? b->entries[i + 1].offset : b->total;
        if (end - b->entries[i].offset > BOOK_CHAPTER_BYTES_MAX) fallback = true;
    }
    if (!fallback) return ESP_OK;
    b->count = 0; add(b, 0, NULL); err = reset(r, b, b->bom); if (err) return err;
    uint32_t part = 0;
    while (next(r, &cp)) {
        uint32_t size = r->offset - part;
        // 目标后首个换行切段；无换行时在硬上限前按字符切。/ Split at the first newline after the target, or before the hard cap at a character boundary.
        if (r->offset < b->total && ((size >= PART_TARGET && cp == '\n') || size >= BOOK_CHAPTER_BYTES_MAX - 4)) {
            if (!add(b, r->offset, NULL)) return ESP_ERR_INVALID_SIZE;
            part = r->offset;
        }
    }
    return ferror(b->file) ? ESP_FAIL : ESP_OK;
}
esp_err_t book_txt_open(book_txt_t *b, const char *path) {
    b->file = fopen(path, "rb"); if (!b->file) return ESP_ERR_NOT_FOUND;
    if (fseek(b->file, 0, SEEK_END)) return ESP_FAIL;
    long size = ftell(b->file);
    if (size < 0 || (unsigned long)size > UINT32_MAX || size > LONG_MAX - 4) return ESP_ERR_INVALID_SIZE;
    b->total = (uint32_t)size;
    b->entries = heap_caps_malloc(sizeof(book_entry_t) * BOOK_CHAPTER_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    reader_t *r = heap_caps_malloc(sizeof(*r), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!b->entries || !r) { free(r); return ESP_ERR_NO_MEM; }
    esp_err_t err = reset(r, b, 0);
    if (!err) {
        size_t n = fread(r->bytes, 1, READ_BLOCK + 3, b->file);
        if (ferror(b->file)) err = ESP_FAIL;
        else if (n >= 3 && !memcmp(r->bytes, "\xef\xbb\xbf", 3)) b->bom = 3;
        else for (size_t i = 0; i < n && i < READ_BLOCK;) {
            uint32_t cp; size_t k = utf8_one(r->bytes + i, n - i, &cp);
            if (k == 1 && r->bytes[i] >= 0x80) { b->gbk = true; break; } i += k;
        }
    }
    if (!err) err = index_book(r, b);
    free(r); return err;
}
esp_err_t book_txt_load(book_txt_t *b, size_t i, char **utf8, size_t *len) {
    uint32_t start = b->entries[i].offset, end = i + 1 < b->count ? b->entries[i + 1].offset : b->total;
    if (!start) start = b->bom;
    size_t cap = (size_t)(end - start) * 3 + 1;
    char *out = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    reader_t *r = heap_caps_malloc(sizeof(*r), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!out || !r) { free(out); free(r); return ESP_ERR_NO_MEM; }
    esp_err_t err = reset(r, b, start); size_t used = 0; uint32_t cp;
    if (!err) while (r->offset < end && next(r, &cp)) {
        char bytes[4]; size_t n = encode(cp, bytes);
        // 文件被外部改写时拒绝越过已索引边界。/ Reject crossing indexed bounds if the file changed externally.
        if (r->offset > end || n >= cap - used) { err = ESP_ERR_INVALID_SIZE; break; }
        memcpy(out + used, bytes, n); used += n;
    }
    if (ferror(b->file) || r->offset != end) err = ESP_FAIL;
    free(r);
    if (err) { free(out); return err; }
    out[used] = 0; *utf8 = out; *len = used; return ESP_OK;
}
