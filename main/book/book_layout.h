/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：借用章节 UTF-8 文本，生成分页并绘制正文。
 * English: Paginate borrowed chapter UTF-8 text and draw its body.
 *
 * 冻结：不释放原文、不刷新屏幕；调用方持有字体绘制互斥锁。
 * Frozen: Never free source text or present the display; caller holds the font draw lock.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "epdiy.h"
#include "html_text.h"

/// 重排借用文本；空文一页，失败清空布局，超过 4096 页返回 false。/ Borrow and paginate; empty text has one page, failure clears layout, over 4096 pages fails.
bool book_layout_build(const char* utf8, size_t len, EpdRect rect, int px);
/// 借用块表；标题字号加8，块间单换行；原文与块表须存活至free。/ Borrow blocks; headings add 8 px, with one newline between blocks; text and blocks must outlive layout.
bool book_layout_build_blocks(const char* utf8, size_t len, const blk_t* blocks, size_t count, EpdRect rect, int px);
/// 初始化并只排前两页；后续由调用方分批推进，原文继续借用。/ Initialize the first two pages; caller advances later batches while retaining source ownership.
bool book_layout_begin_blocks(const char* utf8, size_t len, const blk_t* blocks, size_t count, EpdRect rect, int px);
/// 最多增加指定页数；失败清空布局，调用方必须停止阅读。/ Add at most the requested pages; failure clears layout and requires ending reading.
bool book_layout_extend(size_t pages);
/// 是否完成整章分页。/ Whether the whole chapter has been paginated.
bool book_layout_complete(void);
/// 释放页表，不释放原文。/ Free layout storage, never the borrowed text.
void book_layout_free(void);
/// 返回已完成页数；未建立布局时为零，分批排版时不是整章总页数。/ Return completed pages, zero without a layout; incremental counts are not the chapter total.
size_t book_layout_page_count(void);
/// 使用建立布局时的宽高与字号绘图；不匹配或越界时不绘制。/ Draw with the built dimensions and size; mismatches or invalid pages do nothing.
void book_layout_draw_page(uint8_t* fb, size_t page, EpdRect rect, int px);
/// 命中本页未加载图片占位，返回块索引及可选矩形；未命中为 SIZE_MAX；与绘制使用相同锁。
/// Hit an unloaded image placeholder, returning its block index and optional bounds; SIZE_MAX if absent; use the drawing lock.
size_t book_layout_image_at(size_t page, EpdRect rect, int x, int y, EpdRect* hit);
/// 查找字节偏移所属页，越界偏移夹到末页。/ Find page containing a byte offset; excessive offsets clamp to the last page.
size_t book_layout_page_for_offset(size_t off);
/// 返回页首偏移；已完成页数索引是待排页起点（完成后为文本末尾），更大索引返回文本长度。
/// Return a page start; index equal to completed count marks the pending page (EOF when complete), larger indexes return text length.
size_t book_layout_page_start_offset(size_t page);
