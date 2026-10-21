/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：单实例 TXT/EPUB 书源与章节进度接口。
 * English: Singleton TXT/EPUB source and chapter progress interface.
 * 冻结：正文由调用方释放；不依赖 UI。
 * Frozen: The caller frees loaded text; no UI dependencies.
 */
#pragma once
#include <stdbool.h>
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
/// 当前书是 EPUB 时切换书内自带字体；TXT 无事发生。改动在下一章解析时生效。
/// Toggle embedded faces when the open book is an EPUB; a no-op for TXT. The change takes
/// effect when the next chapter is parsed.
void book_set_embedded_fonts(bool on);
/// 返回章节数。/ Return the chapter count.
size_t book_chapter_count(void);
/// 可见目录优先采用正文编号题头，仍映射到原始 spine 章节；进度继续使用 spine 索引。
/// Prefer numbered body headings in the TOC while mapping back to original spine indices for progress.
size_t book_navigation_count(void);
size_t book_navigation_chapter(size_t position);
esp_err_t book_navigation_title(size_t position, char *buf, size_t cap);
const char *book_navigation_anchor(size_t position);
size_t book_navigation_source_offset(size_t position);
/// 复制 UTF-8 标题，空间不足返回错误。/ Copy a UTF-8 title, failing if capacity is insufficient.
esp_err_t book_chapter_title(size_t i, char *buf, size_t cap);
/// 加载 NUL 结尾的 PSRAM UTF-8；调用方 free。/ Load NUL-terminated PSRAM UTF-8; caller frees it.
esp_err_t book_chapter_load(size_t i, char **utf8, size_t *len);
/// 保留 EPUB 标题/段落块；TXT 的 blocks 为 NULL，调用方 html_text_free。
/// Preserve EPUB heading/paragraph blocks; TXT has NULL blocks; caller uses html_text_free.
esp_err_t book_chapter_load_blocks(size_t i, html_text_t *out);
esp_err_t book_chapter_load_blocks_anchor(size_t i, const char *anchor,
                                           size_t *anchor_offset, html_text_t *out);
esp_err_t book_chapter_load_blocks_target(size_t i, const char *anchor,
                                           size_t source_offset, size_t *text_offset,
                                           html_text_t *out);
/// 读取当前 EPUB 章内图片。/ Read an image from the current EPUB chapter.
esp_err_t book_chapter_image(size_t chapter, const char *src, uint8_t **data, size_t *size, bool *png);
/// TXT 为源文件字节；EPUB 为 spine 原始 HTML 未压缩累计字节，不是 ZIP 文件大小。
/// TXT uses source-file bytes; EPUB uses cumulative uncompressed spine HTML bytes, not ZIP file size.
uint32_t book_total_bytes(void);
/// TXT 为原文件章起点；EPUB 为前序 spine 原始 HTML 字节总和。
/// TXT uses original chapter offsets; EPUB uses the sum of preceding spine source HTML bytes.
uint32_t book_chapter_byte_offset(size_t i);
/// 当前类型；关闭时默认 TXT。/ Current kind, defaulting to TXT when closed.
book_kind_t book_kind(void);

esp_err_t book_chapter_image_dimensions(size_t chapter, const char *src, unsigned *width, unsigned *height);
