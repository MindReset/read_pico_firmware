/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：借用章节 UTF-8 文本，生成分页并绘制正文。
 * English: Paginate borrowed chapter UTF-8 text and draw its body.
 *
 * 冻结：不释放原文、不刷新屏幕；调用方串行化字体访问，推屏期间的预绘必须在返回UI前交还。
 * Frozen: Never free source text or present the display; the caller serializes font access and hands any output-overlapped prefetch back before UI work resumes.
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
/// 借用字体 run 表（来自同一次 html_text 解析），绘制与测量按段换字体；传 NULL 恢复单字体。
/// 与块表同样是借用，须存活到 free；run 表里的未装载槽自动退回系统字体。
/// Borrow the font run table from the same html_text parse so measuring and drawing switch
/// faces span by span; NULL restores single-face layout. It is borrowed like the block table
/// and must outlive free; slots that never loaded fall back to the system face.
void book_layout_set_runs(const html_run_t* runs, size_t count);
/// 设置行高百分比与段后距离百分比，重排时生效。/ Set line height and paragraph gap percentages for the next layout.
void book_layout_set_spacing(unsigned line_percent, unsigned paragraph_percent);
/// 正文额外字间距为 -4/-2/0/+2/+4 像素。/ Extra body tracking is -4/-2/0/+2/+4 pixels.
void book_layout_set_typography(int tracking_px);
/// 普通正文首行缩进 0..3 字；不改变标题及居中、右对齐块。/ Indent ordinary body paragraphs by 0..3 em; keep headings and aligned blocks unchanged.
void book_layout_set_first_line_indent(unsigned em);
/// 正文首行按像素微调-20..20，0为默认，不改变标题/对齐块或无缩进。/ Fine-tune body indent by -20..20px; default 0, leaving headings/aligned/no-indent blocks intact.
void book_layout_set_first_line_indent_adjust(int px);
/// 把正文宽度收齐到完整汉字列并居中，避免折行余量只堆在右侧。/ Center complete CJK columns so wrap slack does not accumulate on the right.
EpdRect book_layout_balanced_rect(EpdRect outer, int px, int tracking_px);
/// 正文阅读线：0 无，1 虚线，2 点线。/ Reading guides: none, dashed or dotted.
void book_layout_set_reading_line(unsigned style);
/// 阅读线相对默认位置上下移动 -8..+8 像素；负值上移。/ Move guide -8..+8 px; negative is up.
void book_layout_set_reading_line_offset(int offset_px);
/// 关闭插图时，排版跳过图片块，不为其保留页或正文空位。/ When false, omit image blocks without reserving pages or body space.
void book_layout_set_images_visible(bool visible);
/// 竖排：正文自上而下成列、列序自右向左，标点按竖排禁则处理，拉丁与括号转 90°，插图独占一页。
/// 关闭时是原来的横排，排版与绘制一字未变；重排前调用。
/// Vertical setting: body runs down in columns ordered right to left, punctuation follows the
/// vertical kinsoku rules, Latin and brackets turn 90°, and an illustration takes a page of its
/// own. Off is the original horizontal setting, unchanged; call it before a re-layout.
void book_layout_set_vertical(bool vertical);
bool book_layout_vertical(void);
/// EPUB 章节首页预留标题区并跳过已经在题头显示的前置标题块；每章重排前调用。
/// Reserve a first-page chapter heading and skip heading blocks already shown there; call before each chapter layout.
void book_layout_set_chapter_lead(size_t skip_bytes, unsigned height_px);
/// 竖排首页从右边缘起预留的题头列宽（像素）；题头由调用方按同一宽度竖向绘制。
/// 横排忽略它。每章重排前与 chapter_lead 一起调用。
/// Column width reserved from the right of the first vertical page for a vertical chapter
/// heading, in pixels; the caller draws the heading into that same width. Horizontal setting
/// ignores it. Call it with chapter_lead before each chapter layout.
void book_layout_set_chapter_lead_width(unsigned width_px);
/// 返回该页的 EPUB 图片序号；文字页为 -1。/ Return an EPUB image index for this page, or -1 for text.
/// 查询插图原始像素尺寸；返回 false 表示取不到，排版会退回整页显示。
/// / Query an image's pixel dimensions; false means unavailable and layout falls back to a
/// whole-page image.
typedef bool (*book_layout_image_dims_fn)(void* ctx, int image, int* width, int* height);
/// 注册尺寸回调；传 NULL 取消。排版不持有它，调用方负责生命周期。
/// / Register the dimensions callback; NULL clears it. Layout borrows it, the caller owns it.
void book_layout_set_image_dims(book_layout_image_dims_fn fn, void* ctx);

/// 插图通栏宽度（整屏像素宽）。设成屏宽后插图按整屏等比缩放、不再受正文边距约束；
/// 0 关闭，退回正文栏宽。它是面板常量，改边距或字号后的重排不必重新调用。
/// Full-bleed width for illustrations in panel pixels. Set to the panel width, illustrations
/// scale to the whole screen instead of the body margins; 0 disables it and they fall back to
/// the body column. It is a panel constant, so re-layouts after margin or size changes need no
/// update.
void book_layout_set_image_bleed_width(int screen_width);
/// 插图可用区域：通栏开启时整屏宽、与正文同带；未开启或未排版时等于正文栏。
/// Region illustrations may occupy: panel-wide and as tall as the body when full bleed is on;
/// the body column itself when it is off or before the first layout.
EpdRect book_layout_image_rect(void);

/// 本页插图张数；越界返回 0。
/// / Number of images placed on this page; 0 when out of range.
int book_layout_page_image_count(size_t page);
/// 取本页第 i 张图：序号、页内 y、显示宽高。越界返回 false。
/// / Image i of this page: index, y within the page, and display width/height.
bool book_layout_page_image_at(size_t page, int i, int* image, int* y, int* width, int* height);

int book_layout_page_image(size_t page);
/// 布局代次，重排或释放后改变，用于使页内位图缓存失效。/ Layout generation changes after rebuild/free, invalidating page bitmap caches.
uint32_t book_layout_generation(void);
/// 建立布局时的正文矩形；调用方据此发现版面尺寸变化(如横竖屏切换)并及时重排。
/// The rect the layout was built with; callers detect body size changes (e.g. an
/// orientation switch) from it and reflow in time.
EpdRect book_layout_body_rect(void);
/// 释放页表，不释放原文。/ Free layout storage, never the borrowed text.
void book_layout_free(void);
/// 返回页数；未建立布局时为零。/ Return page count, zero without a layout.
size_t book_layout_page_count(void);
/// 使用建立布局时的宽高与字号绘图；不匹配或越界时不绘制。/ Draw with the built dimensions and size; mismatches or invalid pages do nothing.
void book_layout_draw_page(uint8_t* fb, size_t page, EpdRect rect, int px);
/// 查找字节偏移所属页，越界偏移夹到末页。/ Find page containing a byte offset; excessive offsets clamp to the last page.
size_t book_layout_page_for_offset(size_t off);
/// 点词定位：把页面内坐标扩成原文字节单词区间；须持调用方绘制锁（与 draw_page 共享行缓冲）。
/// Hit-test a page coordinate into a source byte word; caller holds the font draw lock.
bool book_layout_hit_test(size_t page, int x, int y, size_t* word_start, size_t* word_end);
/// 返回页首原文字节偏移；越界页返回文本长度。/ Return source byte offset at page start; invalid pages return text length.
size_t book_layout_page_start_offset(size_t page);

/// 空闲预绘游标；不持有正文或字体指针，布局代次变化立即失效。
/// Idle drawing cursor: borrows no text/font pointers and expires on layout generation changes.
typedef struct {
    uint32_t generation, image_at;
    size_t page, offset;
    int64_t used;
    bool started, complete, failed;
} book_layout_draw_cursor_t;
/// 每次只画一行或一列，不推屏；返回-1失败、0待续、1完成。只在UI空闲线程调用。
/// Draw at most one row/column without presenting; -1 failed,0 pending,1 complete. UI idle thread only.
int book_layout_draw_slice(uint8_t *fb,size_t page,EpdRect rect,int px,book_layout_draw_cursor_t *cursor);
