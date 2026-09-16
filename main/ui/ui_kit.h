/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 页面共用的排版常量与绘制原语。每个 demo 页只依赖这一层，不互相引用。
 *
 * Shared layout constants and drawing primitives. Each demo page depends
 * only on this layer and does not include another page.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "epdiy.h"
#include "fallback.h"

#ifdef __cplusplus
extern "C" {
#endif

// 屏幕逻辑尺寸（竖屏）。布局常量都从这两个数推，换屏只要改这里。
// Logical screen size (portrait). Layout constants derive from these two; change them for a new panel.
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216

// 字号是字形的像素高。这块屏像素密度接近手机，正文低于 36px 在正常观看距离就看
// 不清了，所以正文取 40px；一行只放一组标签和数值，40px 也铺得开 604 的内容宽度。
// 字段特别多的地方用 _SM 那一档走双列紧凑行，靠对齐和分隔线保证可读，而不是缩字号。
// Font sizes are glyph pixel heights. This panel is phone-dense; body text below
// 36px is hard to read at a normal viewing distance, so body is 40px. One
// label/value pair also fits the 604px content width at 40px. Dense pages use
// the _SM pair in two columns and stay readable via alignment and rules, not a
// smaller size.
#define UI_PX_TITLE 60
#define UI_PX_SUB 30
#define UI_PX_SECTION 30
#define UI_PX_LABEL 40
#define UI_PX_VALUE 40
#define UI_PX_LABEL_SM 28
#define UI_PX_VALUE_SM 34
#define UI_PX_BODY 38
#define UI_PX_CAPTION 28
// 底栏三格时每格只有 157px，按钮字号压到 34 才放得下「复位触摸」这种四字标签。
// A 3-cell bar is 157px per cell; 34px is what fits a four-character label such as 「复位触摸」.
#define UI_PX_BTN 34
#define UI_PX_HERO 92

// 全页共用的排版：边距、间距、按钮、键值行。/ Shared page metrics: margins, gaps, buttons, key-value rows.
#define UI_MARGIN 40
#define UI_GAP 12
#define UI_PAD 16
#define UI_BTN_RADIUS 14
#define UI_CHIP_RADIUS 8
#define UI_BTN_H 84
#define UI_ROW_H 68
#define UI_ROW_H_SM 58
#define UI_SECTION_GAP 28

// ui_draw_section() 占掉的高度：标题字号 + 到分隔线的 8 + 线到首行的 10。
// Height taken by ui_draw_section(): title size + 8 to the rule + 10 to the first row.
#define UI_SEC_HEAD (UI_PX_SECTION + 18)

// 固定页眉带：所有页的标题、副标题、分隔线都在同一组 y 上，翻页时标题不跳。
// Fixed header band: title, subtitle and rule share the same y so they do not jump on page change.
#define UI_HEADER_TITLE_Y 32
#define UI_HEADER_SUB_Y 104
#define UI_HEADER_RULE_Y 152
#define UI_CONTENT_TOP 176

// 底栏贴屏幕下沿，右端固定留给菜单按钮，两者同高同基线。
// Bottom bar sits on the lower edge; the right cell is reserved for the menu button. Same height and baseline.
#define UI_BAR_H 96
#define UI_BAR_MARGIN 24
#define UI_BAR_TOP (UI_LOCK_HEIGHT - UI_BAR_MARGIN - UI_BAR_H)
#define UI_CONTENT_BOTTOM (UI_BAR_TOP - 20)

// 菜单按钮是底栏最右一格，箭头向上表示能打开全屏一级菜单。
// The menu button is the rightmost bar cell; an up arrow means the root menu can open.
#define UI_MENU_BTN_SIZE UI_BAR_H

// epdiy 图形接口取 8 位灰度（仅高 4 位有效），字体属性取 4 位灰度，两者不可混用。
// epdiy graphics uses 8-bit gray (high nibble only); font attrs use 4-bit gray. Do not mix them.
#define UI_GRAY_WHITE 0xFF
#define UI_GRAY_LIGHT 0xB0
#define UI_GRAY_BLACK 0x00
#define UI_INK_WHITE 15
#define UI_INK_BLACK 0

/// 文字按「行的上沿」定位，基线由字号推出，调用处不用再算 ascender。
/// Text is placed by the top of the line; the baseline comes from the size so callers skip the ascender.
void ui_text(
    uint8_t* framebuffer, int x, int y_top, int px, const char* text,
    enum EpdFontFlags align, bool inverted
);
void ui_text_bw(
    uint8_t* framebuffer, int x, int y_top, int px, const char* text,
    enum EpdFontFlags align, bool inverted
);
/// 让文字以 center_y 为视觉中心，用字形的基线上下延伸量推算基线。
/// Center text on center_y using the glyph's above/below extents to find the baseline.
void ui_text_vc(
    uint8_t* framebuffer, int x, int center_y, int px, const char* text,
    enum EpdFontFlags align, bool inverted
);
void ui_blit_bmp(
    uint8_t* framebuffer, int x, int y, const ui_fallback_bmp_t* bmp
);

void ui_draw_round_rect(
    uint8_t* framebuffer, EpdRect rect, int radius, uint8_t color
);
/// 选中：外框保留，内缩一圈加粗，不整块填黑，减轻残影。
/// Selected: keep the outer frame and thicken an inset ring; do not fill solid black, which ghosts.
void ui_draw_selected_round_rect(uint8_t* framebuffer, EpdRect rect, int radius);
void ui_draw_choice_round_rect(
    uint8_t* framebuffer, EpdRect rect, int radius, bool on
);
/// 圆角矩形填充：中间矩形 + 左右两侧半圆帽（epdiy 无原生圆角接口）。
/// Filled rounded rect: center rectangle plus left/right caps (epdiy has no native rounded fill).
void ui_fill_round_rect(
    uint8_t* framebuffer, EpdRect rect, int radius, uint8_t color
);
EpdRect ui_inset_rect(EpdRect r, int d);

bool ui_rect_hit(EpdRect rect, uint16_t x, uint16_t y);
/// 等分网格：cols 列、从 y0 起按行高 h + UI_GAP 向下排。绘制和命中共用。
/// Equal-width grid: cols columns from y0, row height h + UI_GAP. Shared by draw and hit-test.
EpdRect ui_grid_rect(int col, int cols, int row, int y0, int h);
EpdRect ui_row_rect(int col, int cols, int y0, int h);
int ui_grid_hit(
    uint16_t x, uint16_t y, int cols, int rows, int y0, int h, const int* ids
);
/// 底栏按钮：右端固定留给菜单按钮，剩下的宽度按 cols 等分。绘制和命中共用。
/// Bar buttons: right cell reserved for the menu button; the rest splits into cols. Shared by draw and hit-test.
EpdRect ui_bar_rect(int col, int cols);
int ui_bar_hit(uint16_t x, uint16_t y, int cols);

int ui_content_right(void);
int ui_content_width(void);
/// 页眉分隔线到底栏之间的整块内容区，局部 DU 刷新用。
/// Content band from the header rule to the bar, for a local DU.
EpdRect ui_content_refresh_area(void);

/// 细分隔线。整行 1px 用 epd_fill_rect 画，比 epd_draw_line 少一层逐点判断。
/// Hairline. A 1px row via epd_fill_rect, skipping epd_draw_line's per-pixel test.
void ui_hairline(uint8_t* framebuffer, int y, int x, int w, uint8_t color);
/// 页眉骨架：左栏标题/副标题，右栏可选方标（Data Matrix 等）。
/// mark_side 是方标边长（含静区）；0 表示没有右栏。
/// 方标按标题墨迹顶到副标题墨迹底垂直居中，右边对齐内容右缘。
/// Header skeleton: title/subtitle on the left, optional mark (Data Matrix, …) on the right.
/// mark_side is the mark side including quiet zone; 0 means no right column.
/// The mark is vertically centered from title ink-top to subtitle ink-bottom and right-aligned to the content edge.
typedef struct {
    EpdRect text;
    EpdRect accessory;
} ui_header_skel_t;

ui_header_skel_t ui_header_skel(
    const char* title, const char* subtitle, int mark_side
);
/// 右栏按宽高留位，不必是方标。字号条这类扁控件用这个。
/// Right column reserved by width and height; need not be square. Use this for a flat control such as a size bar.
ui_header_skel_t ui_header_skel_box(
    const char* title, const char* subtitle, int acc_w, int acc_h
);
/// 只画文字和通栏横线，右栏留白。方标由调用方画进 accessory。
/// Draws text and the full-width rule only; the right column stays blank. The caller paints the mark into accessory.
int ui_draw_header_skel(
    uint8_t* framebuffer, const ui_header_skel_t* skel,
    const char* title, const char* subtitle
);
/// 通栏页眉，等价于没有右栏的骨架。返回值恒为 UI_CONTENT_TOP。
/// Full-width header, same as a skeleton with no right column. Always returns UI_CONTENT_TOP.
int ui_draw_header(
    uint8_t* framebuffer, const char* title, const char* subtitle
);
/// 分组标题：小字标题 + 一条通栏细线。返回组内第一行的 y。
/// Section title: small title plus a full-width hairline. Returns the y of the first row in the group.
int ui_draw_section(uint8_t* framebuffer, int y, const char* title);
/// 键值行：标签左、数值右，行底一条浅色细线。返回下一行的 y。
/// Key-value row: label left, value right, light hairline at the bottom. Returns the next row y.
int ui_draw_row(
    uint8_t* framebuffer, int y, const char* label, const char* value
);
/// 紧凑双列键值行：字段特别多的页用，一行放两组。返回下一行的 y。
/// Compact two-column key-value row for dense pages. Returns the next row y.
int ui_draw_row2(
    uint8_t* framebuffer, int y, const char* l1, const char* v1,
    const char* l2, const char* v2
);
/// 主角数值：标签小字压在上面，数值用大字，用于要一眼看清的量。
/// Hero value: small label above a large number, for a quantity that should read at a glance.
int ui_draw_hero(
    uint8_t* framebuffer, int y, const char* label, const char* value
);
void ui_draw_button(
    uint8_t* framebuffer, EpdRect rect, const char* label, bool on
);
/// 状态位小方块：置位内圈加粗，未置位只留浅边。
/// Status chip: inset ring when set, light outline when clear.
void ui_draw_chip(
    uint8_t* framebuffer, EpdRect rect, const char* label, bool on
);
void ui_draw_barcode(uint8_t* framebuffer, EpdRect rect, const char* text);
int ui_barcode_rows(const char* text, int width);
void ui_draw_code128(uint8_t* framebuffer, EpdRect rect, const char* text);
int ui_code128_rows(const char* text, int width);
EpdRect ui_datamatrix_rect(EpdRect rect, const char* text);
int ui_datamatrix_side(const char* text);
void ui_draw_datamatrix(uint8_t* framebuffer, EpdRect rect, const char* text);

void ui_clear_page(uint8_t* framebuffer);
/// 把逻辑矩形换算成 framebuffer 里的物理矩形。
/// Map a logical rect to the physical rect in the framebuffer.
EpdRect ui_rotate_rect_to_fb(EpdRect rect);
/// 把一个逻辑矩形刷白，逐物理行连续 memset，比 epd_fill_rect 快得多。
/// Wipe a logical rect white with per-physical-row memset, much faster than epd_fill_rect.
void ui_clear_rect_fast(uint8_t* framebuffer, EpdRect rect);

/// 没有可用字体时的纯位图页面：插卡提示 + 重新读取 / 格式化两张位图卡片。
/// 启动路径和存储卡页共用，命中测试见 ui_no_font_hit_test()。
/// Bitmap-only page when no font is available: insert-card hint plus remount / format cards.
/// Shared by boot and the SD page; hit-test is ui_no_font_hit_test().
#define UI_NO_FONT_HIT_NONE (-1)
#define UI_NO_FONT_HIT_REMOUNT 0
#define UI_NO_FONT_HIT_FORMAT 1
/// 排版样本文案只在 ui_kit.c 里定义一处。字体页按剩余高度尽量多画。
/// Sample layout copy is defined once in ui_kit.c. The font page draws as many lines as height allows.
typedef struct {
    int px;
    const char* text;
} ui_sample_line_t;

extern const ui_sample_line_t ui_sample_lines[];
int ui_sample_line_count(void);

/// 开机图 / 锁屏图：整屏 4bpp 位图，尺寸必须和 framebuffer 一致。
/// Boot / lock image: full-screen 4bpp bitmap; size must match the framebuffer.
void ui_draw_full_image(uint8_t* framebuffer, const uint8_t* image);

void ui_draw_no_font_page(
    uint8_t* framebuffer, bool card_present, bool format_confirm
);
int ui_no_font_hit_test(uint16_t x, uint16_t y);

#ifdef __cplusplus
}
#endif
