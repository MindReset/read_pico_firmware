/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 可变 TTF 字形缓存：卡上字体或内置子集，按字重光栅化后画到 framebuffer。
 *
 * Variable TTF glyph cache: SD fonts or the built-in subset, rasterized
 * at the current weight onto the framebuffer.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "epdiy.h"
#include "esp_err.h"

#define TTF_SIZE_SMALL 0
#define TTF_SIZE_LARGE 1

#define TTF_FONT_MAX 24
#define TTF_FONT_NAME_MAX 64
#define TTF_FONT_PATH_MAX 160

typedef struct {
    char name[TTF_FONT_NAME_MAX];
    char path[TTF_FONT_PATH_MAX];
} ttf_font_item_t;

typedef struct {
    uint32_t glyphs;
    uint32_t hits;
    uint32_t misses;
    int64_t read_us;
    int64_t raster_us;
    int64_t total_us;
} ttf_bench_stats_t;

#define TTF_FONT_BUILTIN "builtin"

/// 同时驻留的字体数。0 号槽固定给系统字体，1..N 按需装载书籍内嵌字体。
/// Resident faces. Slot 0 is always the system face; slots 1..N hold embedded book faces.
#define TTF_FONT_SLOTS 4
#define TTF_FONT_SLOT_SYSTEM 0

/// 初始化全部槽位；可重复调用。/ Initialise every slot; idempotent.
void ttf_font_slots_init(void);
/// 把一块 TTF 内存装进空闲槽，返回槽号；槽满或字体无效返回 -1。
/// 引擎接管 data，装载失败或关闭槽位时释放，调用方之后不得再碰它。
/// Load a TTF image into a free slot; returns the slot, or -1 when full or invalid.
/// The engine takes ownership of data and frees it on failure or on slot close.
int ttf_font_open_mem(uint8_t* data, size_t len, const char* label);
/// 选定后续 ttf_* 调用作用在哪个槽；越界忽略。返回原槽号。
/// Pick the slot later ttf_* calls act on; out-of-range is ignored. Returns the previous slot.
int ttf_font_select(int slot);
int ttf_font_selected(void);
bool ttf_font_slot_ready(int slot);
/// 该槽的 cmap 是否覆盖 text 的每个字符；槽未就绪时返回 false。
/// Whether the slot's cmap covers every character of text; false when the slot is empty.
bool ttf_font_slot_has_text(int slot, const char* text);
/// 关闭 0 号以外的所有槽并释放其字节，游标回到系统字体。
/// Close every slot but 0, free its bytes, and put the cursor back on the system face.
void ttf_font_close_embedded(void);

/// 灰阶字覆盖率伽马。小于 1 抬中间覆盖率，抗锯齿边缘更深；满墨仍是 0。
/// Coverage gamma for gray glyphs. Below 1 lifts mid coverage so AA edges are darker; full ink stays 0.
#ifndef TTF_COVER_GAMMA
#define TTF_COVER_GAMMA 0.5f
#endif

esp_err_t ttf_font_init(void);
esp_err_t ttf_font_open(const char* path);
esp_err_t ttf_font_open_builtin(void);
bool ttf_font_is_builtin(void);
bool ttf_font_path_is_builtin(const char* path);
void ttf_font_unload(void);
int ttf_font_scan(void);
int ttf_font_count(void);
const ttf_font_item_t* ttf_font_item(int index);
const char* ttf_font_path(void);
const char* ttf_font_display_name(void);
/// 已知随附字体的中文名称；未知名称原样返回。/ Chinese names for bundled fonts; unknown names pass through.
const char* ttf_font_localized_name(const char* stem);
bool ttf_font_ready(void);
/// 只检查当前字体的字符映射，不读卡、不生成字形或切换字体。
/// Check only the active cmap, without card reads, rasterization or font changes.
bool ttf_font_has_text(const char *text);
int ttf_ascender(int size);
int ttf_ascender_px(int pixel_height);
/// 将设计的 em 字号换算为活动字体的原生栅格高度，不缩放字形位图。
/// Convert a design em size to the active face's native raster height without bitmap scaling.
int ttf_em_height_px(int em_size);

void ttf_draw_text(
    uint8_t* framebuffer, int x, int y, int size, const char* text,
    enum EpdFontFlags align, uint8_t fg, uint8_t bg
);

void ttf_draw_text_px(
    uint8_t* framebuffer, int x, int y, int pixel_height, const char* text,
    enum EpdFontFlags align, uint8_t fg, uint8_t bg
);
/// 将活动字体的原生字形写入有界覆盖率遮罩，复用现有字形缓存，不切换字库。
/// Rasterize the active native face into a bounded coverage mask, reusing its glyph cache.
bool ttf_text_mask_px(uint8_t *mask, unsigned width, unsigned height, int x,
                      int baseline, int pixel_height, const char *text);
/// 从左向右绘制，并在相邻字符之间加入固定像素间距。/ Draw left-aligned text with extra pixels between adjacent characters.
void ttf_draw_text_px_spaced(
    uint8_t* framebuffer, int x, int y, int pixel_height, const char* text,
    int tracking_px, uint8_t fg, uint8_t bg
);
/// 在汉字间均摊行尾余量；标点溢出时可小幅压缩字距。/ Distribute spare width across CJK gaps or slightly compress for hanging punctuation.
void ttf_draw_text_px_fitted(
    uint8_t* framebuffer, int x, int y, int pixel_height, const char* text,
    int tracking_px, int target_width, uint8_t fg, uint8_t bg
);
/// 覆盖率过半才落墨，像素只有 fg/bg。/ Ink only when coverage is over half; pixels are fg/bg only.
void ttf_draw_text_px_bw(
    uint8_t* framebuffer, int x, int y, int pixel_height, const char* text,
    enum EpdFontFlags align, uint8_t fg, uint8_t bg
);

/// 竖排：把每个字形在字身框内顺时针转 90° 后沿列向下排，返回推进的列高。
/// 字身框是 pixel_height 见方，(x, y) 是第一个字身框的左上角；拉丁字母、数字、括号和
/// 横向符号用它，汉字与假名保持正立由调用方直接绘制。
/// Vertical setting: turn each glyph 90° clockwise inside its em box and stack the boxes down a
/// column, returning the height consumed. The em box is pixel_height square and (x, y) is the
/// first box's top-left. Latin letters, digits, brackets and horizontal marks use it; ideographs
/// and kana stay upright and the caller draws them directly.
int ttf_draw_text_px_rotated(
    uint8_t* framebuffer, int x, int y, int pixel_height, const char* text,
    uint8_t fg, uint8_t bg
);

/// 竖排单列：从 (x, y) 的字身框起逐字向下，汉字与假名正立、全角句读抬到右上角、拉丁与括号
/// 转 90°；tracking_px 加在字与字之间，用来摊平列内余量。返回推进的列高。
/// One vertical column: glyphs run down from the em box at (x, y), with ideographs and kana
/// upright, fullwidth stops lifted to the top-right corner and Latin and brackets turned 90°.
/// tracking_px goes between glyphs and spreads a column's slack. Returns the height consumed.
int ttf_draw_text_px_vertical(
    uint8_t* framebuffer, int x, int y, int pixel_height, const char* text,
    int tracking_px, uint8_t fg, uint8_t bg
);

/// 竖排下一列能放多少像素：ttf_draw_text_px_vertical 的返回值口径，供界面排版预留位置。
/// How many pixels a vertical column of this text takes; same measure ttf_draw_text_px_vertical
/// returns, so UI layout can reserve room for it.
int ttf_vertical_text_height_px(int pixel_height, const char* text, int tracking_px);

void ttf_measure_line(int size, const char* text, int* above, int* below);
void ttf_measure_line_px(int pixel_height, const char* text, int* above, int* below);
int ttf_text_width_px(int pixel_height, const char* text);

/// 绘制期的字体切换点：从 offset 起改用 slot，直到下一个切换点。
/// A face switch while drawing: bytes from `offset` use `slot` until the next switch.
typedef struct {
    uint32_t offset; ///< text 内的字节偏移，首项必须为 0 / byte offset in text, first must be 0
    uint8_t slot; ///< 对应字体槽 / matching font slot
} ttf_run_t;

/// 指定下一次绘制按 run 换字体。runs 借用，须在整个绘制调用期间存活；
/// 传 NULL 或 0 恢复单字体。不改变行宽、字距与两端对齐的算法。
/// Set per-run faces for the next draw. runs are borrowed and must stay alive for the whole
/// call; NULL or 0 restores single-face drawing. Width, tracking and justification math is
/// unchanged.
void ttf_draw_set_runs(const ttf_run_t* runs, size_t count);
/// 首字符左侧留白，不生成位图；用于段首开标点的视觉对齐。
/// First-glyph left bearing without rasterizing; used for paragraph-opening punctuation alignment.
int ttf_text_left_bearing_px(int pixel_height, const char* text);
void ttf_set_weight(int wght);
/// Reader stroke adjustment -2..2, zero preserves native rasterization.
void ttf_set_stroke_adjust(int level);
int ttf_get_stroke_adjust(void);
/// 1/16像素笔画覆盖率微调，不改变字宽。/ Adjust stroke coverage in 1/16-pixel steps without changing advances.
void ttf_set_stroke_adjust_fine(int steps);
int ttf_get_stroke_adjust_fine(void);
/// 绘制时使用灰阶覆盖或二值边缘，复用同一份字形缓存，不改变字宽。/ Select gray coverage or binary edges while drawing, reusing glyphs without changing advances.
void ttf_set_antialias(bool enabled);
bool ttf_get_antialias(void);
int ttf_get_weight(void);

void ttf_font_cache_clear(void);
/// 清空所有槽的字形位图与分块缓冲并把占用还回去，字体本身保持装载；返回回收的字节数。
/// 开书前调用：系统字体的缓存是 PSRAM 里最容易腾的一块，书内字体要的正是这块地方。
/// Drop every slot's glyph bitmaps and block buffers and hand the memory back, keeping the
/// faces loaded; returns the bytes reclaimed. Call it before opening a book: the system face's
/// cache is the easiest PSRAM to reclaim and embedded faces need exactly that room.
size_t ttf_font_cache_clear_all(void);
void ttf_bench_begin(void);
void ttf_bench_end(ttf_bench_stats_t* out);
