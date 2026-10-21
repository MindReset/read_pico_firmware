/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：目录只呈现章节导航，和书架列表、旧示例菜单完全分离。
 * English: The directory only navigates chapters and shares no shelf or demo-menu rendering.
 */
#include "book_toc.h"

#include <stdio.h>
#include <string.h>
#include "book_source.h"
#include "ui_kit.h"
#include "ui_nav.h"

enum { TOC_LEFT = 36, TOC_RIGHT = 648, TOC_ROW_TOP = 252,
       TOC_ROW_STEP = 108, TOC_ROW_HEIGHT = 94 };

int book_toc_pages(size_t chapters) {
    return chapters ? (int)(1 + (chapters - 1) / BOOK_TOC_ROWS) : 1;
}

EpdRect book_toc_row_rect(int row) {
    return (EpdRect){TOC_LEFT, TOC_ROW_TOP + row * TOC_ROW_STEP,
                     TOC_RIGHT - TOC_LEFT, TOC_ROW_HEIGHT};
}

// 把 EPUB/TXT 中的换行、制表符和控制符收敛为一个空格，防止绘制越过本行。
// Collapse line breaks, tabs and controls from EPUB/TXT to one space so glyphs stay in one row.
static void one_line(char *dst, size_t cap, const char *src) {
    if (!cap) return;
    size_t out = 0;
    bool gap = false;
    for (const unsigned char *p = (const unsigned char *)src; *p && out + 1 < cap;) {
        unsigned char c = *p;
        if (c <= 0x20 || c == 0x7f) { gap = out != 0; ++p; continue; }
        if (gap && out + 2 < cap) dst[out++] = ' ';
        gap = false;
        int bytes = c < 0x80 ? 1 : c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
        if (out + (size_t)bytes >= cap) break;
        for (int i = 0; i < bytes && *p; ++i) dst[out++] = (char)*p++;
    }
    while (out && dst[out - 1] == ' ') --out;
    dst[out] = 0;
}

static void fit_line(char *text, int px, int width) {
    size_t len = strlen(text);
    bool trimmed = false;
    while (len && (ui_text_fixed_width_px(px, text) > width || len > 96)) {
        do { --len; } while (len && ((unsigned char)text[len] & 0xc0) == 0x80);
        text[len] = 0;
        trimmed = true;
    }
    if (trimmed && len > 3) {
        // Cut a little more before adding the ellipsis so its glyph cannot cross the status area.
        // 添加省略号前预留字形宽度，避免跨过右侧当前章节标记。
        while (len && ui_text_fixed_width_px(px, text) + ui_text_fixed_width_px(px, "…") > width) {
            do { --len; } while (len && ((unsigned char)text[len] & 0xc0) == 0x80);
            text[len] = 0;
        }
        if (len + sizeof("…") <= 160) strcat(text, "…");
    }
}

int book_toc_hit(uint16_t x, uint16_t y, int page, size_t chapters) {
    if (x < 100 && y >= 76 && y < 151) return BOOK_TOC_BACK;
    if (y >= 1018 && y < 1086) {
        if (x >= 36 && x < 136) return BOOK_TOC_SKIP_PREV;
        if (x >= 146 && x < 258) return BOOK_TOC_PREV;
        if (x >= 426 && x < 538) return BOOK_TOC_NEXT;
        if (x >= 548 && x < 648) return BOOK_TOC_SKIP_NEXT;
    }
    if (x >= 36 && x < 648 && y >= 1100 && y < 1174) return BOOK_TOC_JUMP;
    for (int row = 0; row < BOOK_TOC_ROWS; ++row) {
        size_t index = (size_t)page * BOOK_TOC_ROWS + row;
        if (index >= chapters) break;
        if (ui_rect_hit(book_toc_row_rect(row), x, y)) return (int)index;
    }
    return -1;
}

int book_toc_jump_hit(uint16_t x, uint16_t y) {
    if (x >= 75 && x < 330 && y >= 850 && y < 907) return BOOK_TOC_JUMP_CANCEL;
    if (x >= 354 && x < 609 && y >= 850 && y < 907) return BOOK_TOC_JUMP_CONFIRM;
    if (x >= 75 && x < 609 && y >= 600 && y < 711) return BOOK_TOC_JUMP_TRACK;
    return -1;
}

int book_toc_jump_percent(uint16_t x) {
    if (x <= 92) return 0;
    if (x >= 592) return 100;
    return ((int)x - 92) * 100 / 500;
}

int book_toc_page_from_percent(int percent, int pages) {
    if (pages <= 1 || percent <= 0) return 0;
    if (percent >= 100) return pages - 1;
    int page = (percent * pages + 99) / 100 - 1;
    return page < pages ? page : pages - 1;
}

// 双像素深色轮廓让白色卡片在墨水屏上仍有清晰边界。/ Dark two-pixel outlines keep white cards legible on e-paper.
static void toc_border(uint8_t *fb, EpdRect rect, int radius, bool selected) {
    uint8_t gray = selected ? 0x38 : 0x60;
    ui_draw_round_rect(fb, rect, radius, gray);
    ui_draw_round_rect(fb, (EpdRect){rect.x + 1, rect.y + 1, rect.width - 2, rect.height - 2},
                       radius - 1, gray);
}

static void toc_pill(uint8_t *fb, EpdRect rect, const char *text, int px, bool selected) {
    ui_fill_round_rect(fb, rect, 20, selected ? 0xc0 : UI_GRAY_WHITE);
    toc_border(fb, rect, 20, selected);
    char label[160];
    one_line(label, sizeof(label), text);
    fit_line(label, px, rect.width - 16);
    ui_text_box(fb,ui_inset_rect(rect,8),px,label,EPD_DRAW_ALIGN_CENTER,false);
}

void book_toc_render(uint8_t *fb, const char *book_title, size_t chapters,
                     size_t current, int page, const char *message) {
    ui_clear_page(fb);
    ui_fill_rect_fast((EpdRect){0, 0, UI_LOCK_WIDTH, UI_LOCK_HEIGHT}, 0xe0, fb);
    ui_nav_status(fb);
    ui_nav_back(fb, TOC_LEFT, 79);
    ui_text_box(fb,(EpdRect){120,79,444,72},34,"目录",EPD_DRAW_ALIGN_CENTER,false);

    char title[160];
    one_line(title, sizeof(title), book_title ? book_title : "");
    if (!title[0]) strcpy(title, "当前书籍");
    fit_line(title,ui_text_effective_px(23),420);
    ui_text_box(fb,(EpdRect){TOC_LEFT,160,420,48},23,title,EPD_DRAW_ALIGN_LEFT,false);
    char total[48];
    snprintf(total, sizeof(total), "%u 节", (unsigned)chapters);
    ui_text_box(fb,(EpdRect){482,160,166,48},23,total,EPD_DRAW_ALIGN_RIGHT,false);
    ui_fill_rect_fast((EpdRect){TOC_LEFT, 213, TOC_RIGHT - TOC_LEFT, 2}, 0x68, fb);

    if (message && *message) {
        char info[160];one_line(info,sizeof(info),message);fit_line(info,ui_text_effective_px(23),540);
        ui_text_box(fb,(EpdRect){72,408,540,72},23,info,EPD_DRAW_ALIGN_CENTER,false);
    } else if (!chapters) {
        ui_text_box(fb,(EpdRect){72,408,540,72},25,"这本书没有可用目录",EPD_DRAW_ALIGN_CENTER,false);
    } else {
        for (int row = 0; row < BOOK_TOC_ROWS; ++row) {
            size_t index = (size_t)page * BOOK_TOC_ROWS + row;
            if (index >= chapters) break;
            size_t source_index = book_navigation_chapter(index);
            if (source_index == SIZE_MAX) break;
            EpdRect r = book_toc_row_rect(row);
            bool selected = index == current;
            ui_fill_round_rect(fb, r, 20, selected ? 0xc0 : UI_GRAY_WHITE);
            toc_border(fb, r, 20, selected);
            char number[16]; snprintf(number, sizeof(number), "%02u", (unsigned)index + 1);
            ui_text_box(fb,(EpdRect){r.x+14,r.y+11,58,72},21,number,EPD_DRAW_ALIGN_LEFT,false);
            char raw[160] = "", label[160];
            if (book_navigation_title(index, raw, sizeof(raw)) != ESP_OK || !raw[0])
                snprintf(raw, sizeof(raw), "第 %u 节", (unsigned)index + 1);
            char spine_default[32];
            snprintf(spine_default, sizeof(spine_default), "第 %u 节", (unsigned)source_index + 1);
            if (strcmp(raw, spine_default) == 0)
                snprintf(raw, sizeof(raw), "第 %u 节", (unsigned)index + 1);
            one_line(label, sizeof(label), raw);
            if (!label[0]) snprintf(label, sizeof(label), "第 %u 节", (unsigned)index + 1);
            fit_line(label,ui_text_effective_px(30),selected?420:495);
            ui_text_box(fb,(EpdRect){r.x+79,r.y+9,selected?420:495,76},30,label,EPD_DRAW_ALIGN_LEFT,false);
            if (selected)
                ui_text_box(fb,(EpdRect){r.x+r.width-93,r.y+11,71,72},24,"当前",EPD_DRAW_ALIGN_RIGHT,false);
        }
    }
    const int pages = book_toc_pages(chapters);
    toc_pill(fb, (EpdRect){36, 1018, 100, 68}, "« 10页", 24, false);
    toc_pill(fb, (EpdRect){146, 1018, 112, 68}, "‹ 上页", 26, false);
    char position[32]; snprintf(position, sizeof(position), "%02d / %02d", page + 1, pages);
    toc_pill(fb, (EpdRect){268, 1018, 148, 68}, position, 25, true);
    toc_pill(fb, (EpdRect){426, 1018, 112, 68}, "下页 ›", 26, false);
    toc_pill(fb, (EpdRect){548, 1018, 100, 68}, "10页 »", 24, false);
    EpdRect jump = {36, 1100, 612, 74};
    ui_fill_round_rect(fb, jump, 21, UI_GRAY_WHITE);
    toc_border(fb, jump, 21, false);
    ui_text_box(fb,(EpdRect){58,1109,388,56},28,"按百分比跳转",EPD_DRAW_ALIGN_LEFT,false);
    char percent[24];
    snprintf(percent, sizeof(percent), "%d%%  ›", (page + 1) * 100 / pages);
    ui_text_box(fb,(EpdRect){468,1109,158,56},28,percent,EPD_DRAW_ALIGN_RIGHT,false);
}

void book_toc_render_jump(uint8_t *fb, size_t chapters, int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    EpdRect card = {44, 356, 596, 572};
    ui_fill_round_rect(fb, card, 30, UI_GRAY_WHITE);
    toc_border(fb, card, 30, true);
    ui_text_box(fb,(EpdRect){(342)-250,393,500,44},32,"跳到目录位置",EPD_DRAW_ALIGN_CENTER,false);
    ui_text_box(fb,(EpdRect){(342)-250,450,500,44},24,"选择百分比，先预览目标位置",
                  EPD_DRAW_ALIGN_CENTER, false);
    char value[24]; snprintf(value, sizeof(value), "%d%%", percent);
    ui_text_fixed(fb, 342, 515, 55, value,EPD_DRAW_ALIGN_CENTER,false);
    ui_fill_round_rect(fb, (EpdRect){92, 637, 500, 8}, 4, 0x80);
    int width = 500 * percent / 100;
    if (width) ui_fill_round_rect(fb, (EpdRect){92, 637, width, 8}, 4, 0x40);
    int knob = 92 + width;
    epd_fill_circle(knob, 641, 25, UI_GRAY_WHITE, fb);
    epd_draw_circle(knob, 641, 25, 0x40, fb);
    static const char *ticks[] = {"0", "25", "50", "75", "100%"};
    for (int i = 0; i < 5; ++i)
        ui_text_box(fb,(EpdRect){(92 + i * 125)-250,678,500,44},24,ticks[i],EPD_DRAW_ALIGN_CENTER,false);
    EpdRect target_card = {75, 740, 534, 84};
    ui_fill_round_rect(fb, target_card, 19, 0xe8);
    toc_border(fb, target_card, 19, false);
    ui_text_box(fb,(EpdRect){99,747,486,30},23,"目标位置",EPD_DRAW_ALIGN_LEFT,false);
    int pages = book_toc_pages(chapters);
    int target = book_toc_page_from_percent(percent, pages);
    size_t first = (size_t)target * BOOK_TOC_ROWS + 1;
    size_t last = first + BOOK_TOC_ROWS - 1;
    if (last > chapters) last = chapters;
    char summary[160];
    if (chapters)
        snprintf(summary, sizeof(summary), "约第 %d / %d 页 · 第 %u～%u 节",
                 target + 1, pages, (unsigned)first, (unsigned)last);
    else snprintf(summary, sizeof(summary), "这本书没有可用目录");
    fit_line(summary, 25, 486);
    ui_text_box(fb,(EpdRect){99,783,486,34},25,summary,EPD_DRAW_ALIGN_LEFT,false);
    toc_pill(fb, (EpdRect){75, 850, 255, 57}, "取消", 26, false);
    ui_fill_round_rect(fb, (EpdRect){354, 850, 255, 57}, 20, 0x40);
    ui_text_box(fb,(EpdRect){362,858,239,41},26,"跳转",EPD_DRAW_ALIGN_CENTER,true);
}
