/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * EPUB 使用的只读、有界 ZIP 条目访问。
 * Read-only, bounded ZIP entry access for EPUB.
 *
 * 冻结：长篇书籍允许最多 32768 条目、每条输出 4 MiB；不写文件、不支持加密或 ZIP64。
 * Frozen: allow long books up to 32768 entries and 4 MiB output per entry; no writes, encryption or ZIP64.
 */
#pragma once
#include <stddef.h>
#include "esp_err.h"

#define ZIP_ENTRY_MAX 32768
#define ZIP_OUTPUT_MAX (4U * 1024U * 1024U)
#define ZIP_INPUT_MAX (ZIP_OUTPUT_MAX + 65536U)

/// 持有文件与 PSRAM 目录；由 zip_close 释放。/ Owns the file and PSRAM directory; released by zip_close.
typedef struct zip_reader zip_reader_t;
/// 打开并验证中央目录，失败时 *out 为 NULL。/ Open and validate the central directory; set *out to NULL on failure.
esp_err_t zip_open(const char* path, zip_reader_t** out);
/// 释放所有资源，允许 NULL。/ Release all resources; accepts NULL.
void zip_close(zip_reader_t* reader);
/// 按精确路径查找，未找到返回 -1。/ Find an exact path, returning -1 if absent.
int zip_find(const zip_reader_t* reader, const char* name);
/// 返回验证后的条目数量。/ Return the validated entry count.
size_t zip_entry_count(const zip_reader_t* reader);
/// 借用目录路径，下次查询路径或解压前有效；无效索引返回 NULL。/ Borrow entry path until the next path query or extraction; invalid indices return NULL.
const char* zip_entry_name(const zip_reader_t* reader, int index);
/// 返回解压大小，无效索引返回 0；合法条目也可能为空。/ Return output size, or zero for an invalid index or empty entry.
size_t zip_entry_size(const zip_reader_t* reader, int index);
/// 解压并校验 CRC；调用方持有 dst，不补 NUL。失败后缓冲内容未定义。
/// Extract and verify CRC; caller owns dst, with no NUL appended. Buffer contents are undefined on failure.
esp_err_t zip_extract(zip_reader_t* reader, int index, void* dst, size_t cap);
