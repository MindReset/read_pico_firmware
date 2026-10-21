/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：把章节 HTML 转为 PSRAM UTF-8 文本与非空块表。
 * English: Convert chapter HTML into PSRAM UTF-8 text and nonempty blocks.
 *
 * 冻结：不加载网络资源、不执行脚本；解释有界的阅读排版 CSS 子集；输入只借用。
 * Frozen: Never load network resources or execute scripts; interpret a bounded reading CSS subset; input is borrowed.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define HTML_TEXT_MAX_BYTES (4u * 1024u * 1024u)
#define HTML_TEXT_MAX_BLOCKS 16384u
#define HTML_RUN_MAX 32768u

/// 块内一段同字体的字节区间。/ A same-face byte span inside one block.
typedef struct {
    size_t offset; ///< 相对块起点的字节偏移 / Byte offset from the block start
    size_t len; ///< 字节数 / Byte count
    uint8_t slot; ///< 字体槽，0 为系统字体 / Font slot, 0 for the system face
} html_run_t;

/// 把 CSS font-family 的名字解析成字体槽；返回 0 表示用系统字体。
/// Resolve a CSS font-family name to a font slot; 0 means the system face.
typedef uint8_t (*html_font_resolver_fn)(void* ctx, const char* family, size_t len);

typedef struct {
    html_font_resolver_fn resolve; ///< 解析回调；NULL 时全部走系统字体 / NULL keeps every run on the system face
    void* ctx; ///< 回调上下文 / Callback context
} html_font_map_t;

typedef struct {
    size_t offset; ///< UTF-8 字节起点 / UTF-8 byte start
    size_t len; ///< 不含段间换行的字节数 / Bytes excluding the block separator
    bool heading; ///< h1–h3 标题块 / h1-h3 heading block
    bool chapter_start; ///< 已核实的正文章首，另起一页 / Verified body chapter start, begins a new page
    bool linked; ///< 全段为链接文字，不能仅凭书名当正文题头 / Entire block is linked text, not a body heading by label alone
    bool auxiliary; ///< 目录及书前信息容器内的文字 / Text inside navigation or front-matter containers
    int image; ///< 图片序号，文字为 -1 / Image index, -1 for text
    uint32_t image_width, image_height; ///< 有界头部探测，未知为零 / Probed dimensions, zero when unknown
    uint8_t align; ///< 0 左/两端，1 居中，2 右 / 0 left/justify, 1 center, 2 right
    uint8_t indent_percent; ///< 首行缩进，相对字号百分比 / First-line indent as a percentage of font size
    uint8_t margin_before_percent; ///< 段前距，相对字号百分比 / Leading margin as a percentage of font size
    uint8_t margin_after_percent; ///< 段后距，相对字号百分比 / Trailing margin as a percentage of font size
    uint8_t heading_level; ///< 原文 h1–h6 层级，普通文字为零 / Original h1-h6 level, zero for ordinary text
    bool line_break_after; ///< 段内换行，下一块前不加段后距 / In-paragraph break, without a paragraph gap before the next block
    size_t run_first; ///< 本块第一个 run 的下标 / Index of this block's first run
    size_t run_count; ///< 本块的 run 数；零表示整块用系统字体 / Runs owned by this block; zero means the system face throughout
} blk_t;

typedef struct {
    char* utf8; ///< 模块分配且零结尾的文本 / Owned NUL-terminated text
    size_t len; ///< 不含结尾零的字节数 / Bytes excluding terminating NUL
    blk_t* blocks; ///< 模块分配的非空块表 / Owned nonempty block table
    size_t count; ///< 块数 / Block count
    char** images; ///< 相对图片路径 / Relative image paths
    size_t image_count; ///< 图片数 / Image count
    html_run_t* runs; ///< 模块分配的 run 表，被块表借用 / Owned run table, borrowed by the blocks
    size_t run_count; ///< run 总数 / Total run count
} html_text_t;

/// 输出须为空；成功交出所有权，失败清空输出；空输入成功且零块。/ Output must be empty; success transfers ownership, failure clears output; empty input succeeds with zero blocks.
esp_err_t html_to_blocks(const char* html, size_t len, html_text_t* out);
/// 先应用书内 CSS，再应用页面内样式；两段输入均只借用。/ Apply in-book CSS before page styles; both inputs are borrowed.
esp_err_t html_to_blocks_with_css(const char* html, size_t len,
                                   const char* css, size_t css_len, html_text_t* out);
/// 同一次解析记录 EPUB #fragment 在规范化正文中的字节位置；找不到时为零。
/// Record a fragment's normalized text offset during parsing; missing anchors map to zero.
esp_err_t html_to_blocks_with_css_anchor(const char* html, size_t len,
                                          const char* css, size_t css_len,
                                          const char* anchor, size_t* anchor_offset,
                                          html_text_t* out);
/// 目录标题没有 id 时，以原 XHTML 标签位置定位，仍在同一次排版解析中换算正文偏移。
/// 另带字体解析：CSS font-family 命中的槽会被记进 run 表，供逐段换字体排版使用。
/// Resolve a source tag position to normalized text during the same parse. The font map turns
/// CSS font-family names into slots and records them as runs for per-span typesetting.
esp_err_t html_to_blocks_with_css_target(const char* html, size_t len,
                                          const char* css, size_t css_len,
                                          const html_font_map_t* fonts,
                                          const char* anchor, size_t source_offset,
                                          size_t* text_offset, html_text_t* out);
/// 释放文本与块表并清零；可重复调用。/ Free text and blocks and reset; safe to repeat.
void html_text_free(html_text_t* text);
