/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * EPUB 使用的只读、有界 ZIP 条目访问。
 * Read-only, bounded ZIP entry access for EPUB.
 *
 * 冻结：最多 8192 条目，单次解压最多 8 MiB；大于解压上限的未使用资源可留在书内。
 * Frozen: at most 8192 entries and 8 MiB per extraction; unused larger resources may remain in a book.
 * 为兼容大 EPUB 调整容量决策；仍不写文件、不支持加密或 ZIP64。
 * Capacity policy updated for large EPUBs; still no writes, encryption or ZIP64.
 */
#pragma once
#include <stddef.h>
#include "esp_err.h"

#define ZIP_ENTRY_MAX 8192
#define ZIP_OUTPUT_MAX (8U * 1024U * 1024U)
#define ZIP_INPUT_MAX (ZIP_OUTPUT_MAX + 65536U)

/// 持有文件与 PSRAM 目录；由 zip_close 释放。/ Owns the file and PSRAM directory; released by zip_close.
typedef struct zip_reader zip_reader_t;
/// 打开并验证中央目录，失败时 *out 为 NULL。/ Open and validate the central directory; set *out to NULL on failure.
esp_err_t zip_open(const char* path, zip_reader_t** out);
/// 释放所有资源，允许 NULL。/ Release all resources; accepts NULL.
void zip_close(zip_reader_t* reader);
/// 按精确路径查找，未找到返回 -1。/ Find an exact path, returning -1 if absent.
int zip_find(const zip_reader_t* reader, const char* name);
/// 已验证条目的借用路径；读者关闭后失效。/ Borrowed validated entry path, valid until close.
const char* zip_entry_name(const zip_reader_t* reader, int index);
size_t zip_entry_count(const zip_reader_t* reader);
/// 返回解压大小，无效索引返回 0；合法条目也可能为空。/ Return output size, or zero for an invalid index or empty entry.
size_t zip_entry_size(const zip_reader_t* reader, int index);
/// 解压并校验 CRC；调用方持有 dst，不补 NUL。失败后缓冲内容未定义。
/// Extract and verify CRC; caller owns dst, with no NUL appended. Buffer contents are undefined on failure.
esp_err_t zip_extract(zip_reader_t* reader, int index, void* dst, size_t cap);

/// 只读取资源前缀（最多 64 KiB），仅供尺寸探测，不代替完整提取的 CRC 校验。
/// Read at most 64 KiB for advisory dimensions; full extraction still verifies CRC.
esp_err_t zip_extract_prefix(zip_reader_t *reader, int index, void *dst, size_t cap);
