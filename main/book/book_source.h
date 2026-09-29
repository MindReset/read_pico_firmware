/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：单实例 TXT/EPUB 书源与章节进度接口。
 * English: Singleton TXT/EPUB source and chapter progress interface.
 * 冻结：正文由调用方释放；不依赖 UI。
 * Frozen: The caller frees loaded text; no UI dependencies.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "html_text.h"
typedef enum {
    BOOK_KIND_TXT, ///< 文本 / Plain text
    BOOK_KIND_EPUB, ///< EPUB 容器 / EPUB container
} book_kind_t;
/// 打开并替换当前书源。/ Open and replace the current source.
esp_err_t book_open(const char *path);
/// 关闭书源并释放目录。/ Close the source and free its index.
void book_close(void);
/// 返回章节数。/ Return the chapter count.
size_t book_chapter_count(void);
/// 复制 UTF-8 标题，空间不足返回错误。/ Copy a UTF-8 title, failing if capacity is insufficient.
esp_err_t book_chapter_title(size_t i, char *buf, size_t cap);
/// 加载 NUL 结尾的 PSRAM UTF-8；调用方 free。/ Load NUL-terminated PSRAM UTF-8; caller frees it.
esp_err_t book_chapter_load(size_t i, char **utf8, size_t *len);
/// 保留 EPUB 标题/段落及图片块；TXT 的 blocks 为 NULL，调用方 html_text_free。
/// Preserve EPUB heading/paragraph and image blocks; TXT has NULL blocks; caller uses html_text_free.
esp_err_t book_chapter_load_blocks(size_t i, html_text_t *out);
/// 按需解码指定章节的单幅本地图片；失败清空输出，成功像素由调用方 free，TXT 不支持。
/// Decode one local chapter image on demand; clear outputs on failure, caller frees successful pixels; unsupported for TXT.
esp_err_t book_chapter_load_image(size_t i, const char *reference, uint8_t **pixels, uint16_t *width, uint16_t *height);
/// TXT 为源文件字节；EPUB 为 spine 原始 HTML 未压缩累计字节，不是 ZIP 文件大小。
/// TXT uses source-file bytes; EPUB uses cumulative uncompressed spine HTML bytes, not ZIP file size.
uint32_t book_total_bytes(void);
/// TXT 为原文件章起点；EPUB 为前序 spine 原始 HTML 字节总和。
/// TXT uses original chapter offsets; EPUB uses the sum of preceding spine source HTML bytes.
uint32_t book_chapter_byte_offset(size_t i);
/// 当前类型；关闭时默认 TXT。/ Current kind, defaulting to TXT when closed.
book_kind_t book_kind(void);
