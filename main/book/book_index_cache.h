/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：TF 卡图书索引缓存的原子文件外壳。
 * English: Atomic file wrapper for TF-card book index caches.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t source_size;
    int64_t modified;
    uint64_t path_hash;
} book_index_cache_header_t;

/// 仅为 /sdcard 下的普通文件生成缓存键。/ Build a cache key only for regular files under /sdcard.
bool book_index_cache_prepare(const char* source, const char* tag, uint32_t version,
                              char* path, size_t cap, book_index_cache_header_t* header);
/// 校验文件头并返回已定位到载荷的只读文件。/ Validate the header and return a stream positioned at the payload.
FILE* book_index_cache_open_read(const char* path, const book_index_cache_header_t* expected);
/// 创建临时文件并写入文件头。/ Create a temporary file and write its header.
FILE* book_index_cache_open_write(const char* path, const book_index_cache_header_t* header,
                                  char* temp, size_t temp_cap);
/// 关闭并原子替换；失败时删除临时文件。/ Close and atomically replace; remove the temporary file on failure.
bool book_index_cache_finish_write(FILE* file, const char* temp, const char* path, bool payload_ok);
