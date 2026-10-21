/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：独立 EPUB 书源，按 OPF spine 顺序提供章节与文本块。
 * English: Independent EPUB source exposing chapters and text blocks in OPF spine order.
 *
 * 冻结：进度使用 spine 原始 HTML 未压缩字节的累计值，不是 ZIP 物理偏移；不接入主循环。
 * Frozen: Progress uses cumulative uncompressed source HTML bytes, not physical ZIP offsets; no event-loop integration.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "html_text.h"

typedef struct book_epub book_epub_t;
/// 打开 EPUB；失败时 out 置空，成功后由 close 释放。/ Open EPUB; clear out on failure, close owns successful cleanup.
esp_err_t book_epub_open(const char *path, book_epub_t **out);
/// 关闭 ZIP 并释放目录，可传 NULL。/ Close ZIP and free the index; NULL is allowed.
void book_epub_close(book_epub_t *book);
/// 是否使用书内自带字体（CSS @font-face 或包内 TTF）；默认开启。关掉后正文回到系统字体，
/// 已经装进槽的字体要等关书才还回去。/ Whether to use embedded faces (CSS @font-face or a
/// packaged TTF); on by default. Turning it off puts body text back on the system face; faces
/// already in a slot are handed back when the book closes.
void book_epub_set_fonts_enabled(book_epub_t *book, bool on);
/// 返回 spine 章节数，上限 8192。/ Return the spine chapter count, at most 8192.
size_t book_epub_chapter_count(const book_epub_t *book);
/// 优先列可识别的正文编号章节；识别不完整时使用书内导航，并略过书前资料；不改变 spine 进度。
/// Prefer numbered body chapters; use authored navigation and omit front matter when detection is incomplete; keep spine progress.
size_t book_epub_navigation_count(book_epub_t *book);
size_t book_epub_navigation_chapter(book_epub_t *book, size_t position);
esp_err_t book_epub_navigation_title(book_epub_t *book, size_t position, char *buf, size_t cap);
const char *book_epub_navigation_anchor(book_epub_t *book, size_t position);
size_t book_epub_navigation_source_offset(book_epub_t *book, size_t position);
/// 复制 UTF-8 标题，容量不足报错而不截断。/ Copy a UTF-8 title; report insufficient capacity instead of truncating.
esp_err_t book_epub_chapter_title(book_epub_t *book, size_t index, char *buf, size_t cap);
/// 加载 UTF-8 与标题/段落块；调用方 html_text_free。/ Load UTF-8 and heading/paragraph blocks; caller uses html_text_free.
esp_err_t book_epub_load(book_epub_t *book, size_t index, html_text_t *out);
esp_err_t book_epub_load_anchor(book_epub_t *book, size_t index, const char *anchor,
                                 size_t *anchor_offset, html_text_t *out);
esp_err_t book_epub_load_target(book_epub_t *book, size_t index, const char *anchor,
                                 size_t source_offset, size_t *text_offset, html_text_t *out);
/// 加载章内 JPEG/PNG；仅允许 ZIP 内相对路径，调用方释放数据。/ Load an in-book JPEG/PNG from a relative path; caller frees data.
esp_err_t book_epub_image(book_epub_t *book, size_t chapter, const char *src, uint8_t **data, size_t *size, bool *png);
/// spine 原始 HTML 未压缩字节总量，不等于 EPUB 文件大小。/ Total uncompressed source HTML bytes in the spine, not EPUB file size.
uint32_t book_epub_total_bytes(const book_epub_t *book);
/// 该章之前所有 spine 原始 HTML 字节累计值。/ Sum of source HTML bytes preceding this chapter in the spine.
uint32_t book_epub_chapter_byte_offset(const book_epub_t *book, size_t index);
/// 从 OPF 封面声明读取 JPEG/PNG 原始数据；调用方 free。/ Read declared EPUB cover bytes; caller frees data.
esp_err_t book_epub_cover(const char *path, uint8_t **data, size_t *size, bool *is_png);
/// 在分配封面前检查调用方的内存预算；不改变阅读器原有封面接口。
/// Check the caller's cover budget before allocating, preserving the reader's existing API.
esp_err_t book_epub_cover_bounded(const char *path, uint8_t **data, size_t *size,
                                  bool *is_png, size_t budget);
/// 只读 OPF 的书名和作者；供书架显示。/ Read OPF title and creator for the shelf.
esp_err_t book_epub_metadata(const char *path, char *title, size_t title_cap, char *author, size_t author_cap);
/// 只读取已有元数据缓存，锁屏路径不打开和解析 EPUB。
/// Read an existing metadata cache only; the lock path must not open or parse an EPUB.
esp_err_t book_epub_metadata_cached(const char *path, char *title, size_t title_cap,
                                    char *author, size_t author_cap);

/// 有界头部读取，仅探测插图尺寸。/ Probe image dimensions from a bounded header.
esp_err_t book_epub_image_dimensions(book_epub_t *book, size_t chapter, const char *src, unsigned *width, unsigned *height);
