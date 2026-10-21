/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：按路径、大小与修改时间失效的图书索引缓存。
 * English: Book index cache invalidated by path, size and modification time.
 */
#include "book_index_cache.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#define CACHE_MAGIC UINT32_C(0x52494331)

static uint64_t cache_hash(const char* source, const char* tag) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (const unsigned char* p = (const unsigned char*)source; *p; ++p)
        hash = (hash ^ *p) * UINT64_C(1099511628211);
    for (const unsigned char* p = (const unsigned char*)tag; *p; ++p)
        hash = (hash ^ *p) * UINT64_C(1099511628211);
    return hash;
}

bool book_index_cache_prepare(const char* source, const char* tag, uint32_t version,
                              char* path, size_t cap, book_index_cache_header_t* header) {
    if (!source || strncmp(source, "/sdcard/", 8) || !tag || !*tag || !path || !cap || !header)
        return false;
    struct stat st;
    if (stat(source, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
        (uint64_t)st.st_size > UINT32_MAX) return false;
    uint64_t hash = cache_hash(source, tag);
    *header = (book_index_cache_header_t){
        .magic = CACHE_MAGIC, .version = version, .source_size = (uint32_t)st.st_size,
        .modified = (int64_t)st.st_mtime, .path_hash = hash,
    };
    int n = snprintf(path, cap, "/sdcard/.readpico/index/%016llx-%s.bin",
                     (unsigned long long)hash, tag);
    return n > 0 && (size_t)n < cap;
}

FILE* book_index_cache_open_read(const char* path, const book_index_cache_header_t* expected) {
    if (!path || !expected) return NULL;
    FILE* file = fopen(path, "rb");
    if (!file) return NULL;
    book_index_cache_header_t found;
    bool ok = fread(&found, 1, sizeof(found), file) == sizeof(found) &&
        found.magic == expected->magic && found.version == expected->version &&
        found.source_size == expected->source_size && found.modified == expected->modified &&
        found.path_hash == expected->path_hash;
    if (!ok) { fclose(file); return NULL; }
    return file;
}

FILE* book_index_cache_open_write(const char* path, const book_index_cache_header_t* header,
                                  char* temp, size_t temp_cap) {
    if (!path || !header || !temp || !temp_cap) return NULL;
    if (mkdir("/sdcard/.readpico", 0777) && errno != EEXIST) return NULL;
    if (mkdir("/sdcard/.readpico/index", 0777) && errno != EEXIST) return NULL;
    int n = snprintf(temp, temp_cap, "%s.tmp", path);
    if (n <= 0 || (size_t)n >= temp_cap) return NULL;
    FILE* file = fopen(temp, "wb");
    if (!file) return NULL;
    if (fwrite(header, 1, sizeof(*header), file) != sizeof(*header)) {
        fclose(file); remove(temp); return NULL;
    }
    return file;
}

bool book_index_cache_finish_write(FILE* file, const char* temp, const char* path, bool payload_ok) {
    if (!file || !temp || !path) return false;
    bool ok = payload_ok;
    if (ok && fflush(file) != 0) ok = false;
    if (fclose(file) != 0) ok = false;
    if (!ok) {
        remove(temp);
        return false;
    }
    remove(path);
    if (rename(temp, path) == 0) return true;
    remove(temp);
    return false;
}
