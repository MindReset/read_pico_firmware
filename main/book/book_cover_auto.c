/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：从书籍标识确定三种封面版式，并用内建字形在小画布绘制灰阶。
 * English: Choose one of three book-cover layouts from a stable identity and draw grayscale with built-in glyphs.
 */
#include "book_cover_auto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "epdiy.h"
#include "esp_heap_caps.h"
#include "ttf_font.h"
#include "ui_font.h"

#define COVER_LINE_MAX 4
#define COVER_TEXT_CAP 256

static uint64_t hash_text(uint64_t hash, const char *text) {
    if (!text) text = "";
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
        hash = (hash ^ *p) * UINT64_C(1099511628211);
    return (hash ^ 0xffu) * UINT64_C(1099511628211);
}

static uint64_t cover_seed(const char *identity) {
    uint64_t hash = UINT64_C(14695981039346656037);
    hash = hash_text(hash, identity);
    // 版式更新只触发缓存重建，书籍原先分配到的版式保持不变。
    // Template revisions invalidate caches without reassigning a book's layout.
    hash ^= 1u;
    hash ^= hash >> 30; hash *= UINT64_C(0xbf58476d1ce4e5b9);
    hash ^= hash >> 27; hash *= UINT64_C(0x94d049bb133111eb);
    return hash ^ (hash >> 31);
}

unsigned book_auto_cover_template(const char *identity, const char *title, const char *author) {
    (void)title; (void)author;
    return (unsigned)(cover_seed(identity) % 3u);
}

typedef struct {
    uint8_t *fb;
    unsigned width, height, stride;
} cover_canvas_t;

static bool physical_pixel(int x, int y, int *physical_x, int *physical_y) {
    int pw = epd_width(), ph = epd_height();
    switch (epd_get_rotation()) {
        case EPD_ROT_LANDSCAPE: *physical_x = x; *physical_y = y; break;
        case EPD_ROT_PORTRAIT: *physical_x = pw - y - 1; *physical_y = x; break;
        case EPD_ROT_INVERTED_LANDSCAPE:
            *physical_x = pw - x - 1; *physical_y = ph - y - 1; break;
        default: *physical_x = y; *physical_y = ph - x - 1; break;
    }
    return *physical_x >= 0 && *physical_y >= 0 && *physical_x < pw && *physical_y < ph;
}

static int xy(unsigned coordinate, unsigned size, unsigned reference) {
    return (int)((uint64_t)coordinate * size / reference);
}

static int font_px(const cover_canvas_t *c, unsigned base) {
    unsigned by_width = base * c->width / 176u;
    unsigned by_height = base * c->height / 240u;
    unsigned px = by_width < by_height ? by_width : by_height;
    return px < 10 ? 10 : px > 72 ? 72 : (int)px;
}

static void put(cover_canvas_t *c, int x, int y, uint8_t gray4) {
    if (x < 0 || y < 0 || x >= (int)c->width || y >= (int)c->height) return;
    int px, py;
    if (!physical_pixel(x, y, &px, &py)) return;
    uint8_t *at = c->fb + (size_t)py * c->stride + (unsigned)px / 2;
    gray4 &= 15;
    if (px & 1) *at = (uint8_t)((*at & 0x0f) | (gray4 << 4));
    else *at = (uint8_t)((*at & 0xf0) | gray4);
}

static uint8_t get(const cover_canvas_t *c, int x, int y) {
    int px, py;
    if (!physical_pixel(x, y, &px, &py)) return 15;
    uint8_t value = c->fb[(size_t)py * c->stride + (unsigned)px / 2];
    return px & 1 ? value >> 4 : value & 15;
}

static void fill(cover_canvas_t *c, int x0, int y0, int x1, int y1, uint8_t gray4) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)c->width) x1 = (int)c->width;
    if (y1 > (int)c->height) y1 = (int)c->height;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) put(c, x, y, gray4);
}

static void outline(cover_canvas_t *c, int inset, uint8_t gray4) {
    fill(c, inset, inset, (int)c->width - inset, inset + 1, gray4);
    fill(c, inset, (int)c->height - inset - 1, (int)c->width - inset, (int)c->height - inset, gray4);
    fill(c, inset, inset, inset + 1, (int)c->height - inset, gray4);
    fill(c, (int)c->width - inset - 1, inset, (int)c->width - inset, (int)c->height - inset, gray4);
}

static size_t cp_bytes(const char *p) {
    unsigned char c = (unsigned char)*p;
    if (!c) return 0;
    if (c < 0x80) return 1;
    if (c >= 0xc2 && c <= 0xdf) return 2;
    if (c >= 0xe0 && c <= 0xef) return 3;
    if (c >= 0xf0 && c <= 0xf4) return 4;
    return 1;
}

static size_t prior_cp(const char *text, size_t length) {
    if (!length) return 0;
    --length;
    while (length && ((unsigned char)text[length] & 0xc0) == 0x80) --length;
    return length;
}

static int text_width(int px, const char *text) {
    if (!text || !*text) return 0;
    return ui_font_has_text(text) ? ui_font_text_width_px(px, text) : ttf_text_width_px(px, text);
}

static void draw_text(cover_canvas_t *c, int x, int top, int px, const char *text,
                      enum EpdFontFlags align, uint8_t fg, uint8_t bg) {
    if (!text || !*text) return;
    if (ui_font_has_text(text))
        ui_font_draw_text_px(c->fb, x, top + ui_font_ascender_px(px), px,
                             text, align, fg, bg, false);
    else if (ttf_font_ready())
        ttf_draw_text_px(c->fb, x, top + ttf_ascender_px(px), px,
                         text, align, fg, bg);
}

static void trim_right(char *text) {
    size_t n = strlen(text);
    while (n && (text[n - 1] == ' ' || text[n - 1] == '\t')) text[--n] = 0;
}

static int wrap_title(const char *title, int px, int max_width, int max_lines,
                      char lines[COVER_LINE_MAX][COVER_TEXT_CAP]) {
    const char *cursor = title;
    int count = 0;
    while (*cursor && count < max_lines) {
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n') ++cursor;
        if (!*cursor) break;
        char *line = lines[count];
        size_t used = 0, break_at = 0;
        const char *break_src = NULL;
        while (*cursor && *cursor != '\n') {
            size_t bytes = cp_bytes(cursor);
            if (used + bytes >= COVER_TEXT_CAP) break;
            if (bytes > 1) for (size_t i = 1; i < bytes; ++i)
                if (!cursor[i] || ((unsigned char)cursor[i] & 0xc0) != 0x80) { bytes = 1; break; }
            bool whitespace = bytes == 1 && (*cursor == ' ' || *cursor == '\t');
            memcpy(line + used, cursor, bytes);
            line[used + bytes] = 0;
            if (text_width(px, line) > max_width && used) {
                line[break_at ? break_at : used] = 0;
                if (break_src) cursor = break_src;
                break;
            }
            used += bytes;
            cursor += bytes;
            if (whitespace) { break_at = used - 1; break_src = cursor; }
        }
        if (*cursor == '\n') ++cursor;
        trim_right(line);
        if (line[0]) ++count;
        else if (*cursor) cursor += cp_bytes(cursor);
    }
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n') ++cursor;
    if (*cursor && count) {
        char *last = lines[count - 1];
        size_t length = strlen(last);
        while (length && text_width(px, last) + text_width(px, "…") > max_width) {
            length = prior_cp(last, length);
            last[length] = 0;
        }
        if (length + strlen("…") < COVER_TEXT_CAP) strcat(last, "…");
    }
    return count;
}

static int title_lines(const char *title, int max_px, int min_px, int max_width,
                       int max_height, int gap, int max_lines,
                       char lines[COVER_LINE_MAX][COVER_TEXT_CAP]) {
    for (int px = max_px; px >= min_px; --px) {
        memset(lines, 0, sizeof(char) * COVER_LINE_MAX * COVER_TEXT_CAP);
        int count = wrap_title(title, px, max_width, max_lines, lines);
        if (!count) continue;
        const char *last = lines[count - 1];
        size_t len = strlen(last);
        bool shortened = len >= strlen("…") && !strcmp(last + len - strlen("…"), "…");
        if ((!shortened && count * px + (count - 1) * gap <= max_height) || px == min_px) return px;
    }
    return min_px;
}

static void draw_author(cover_canvas_t *c, const char *author, int x, int y,
                        int max_width, int px, enum EpdFontFlags align,
                        uint8_t fg, uint8_t bg) {
    if (!author || !*author) return;
    char shown[COVER_TEXT_CAP];
    snprintf(shown, sizeof(shown), "%s", author);
    while (px > font_px(c, 12) && text_width(px, shown) > max_width) --px;
    size_t len = strlen(shown);
    bool clipped = false;
    while (len && text_width(px, shown) > max_width) {
        len = prior_cp(shown, len);
        shown[len] = 0;
        clipped = true;
    }
    if (clipped) {
        while (len && text_width(px, shown) + text_width(px, "…") > max_width) {
            len = prior_cp(shown, len); shown[len] = 0;
        }
        if (len + strlen("…") < sizeof(shown)) strcat(shown, "…");
    }
    draw_text(c, x, y, px, shown, align, fg, bg);
}

static void paper_grain(cover_canvas_t *c, uint64_t seed, uint8_t base) {
    for (unsigned y = 0; y < c->height; ++y)
        for (unsigned x = 0; x < c->width; ++x) {
            uint64_t noise = seed ^ ((uint64_t)x * UINT64_C(0x9e3779b97f4a7c15)) ^
                                   ((uint64_t)y * UINT64_C(0xc2b2ae3d27d4eb4f));
            noise ^= noise >> 29;
            if ((noise & 63u) == 0) put(c, (int)x, (int)y, base ? base - 1 : base);
        }
}

static void dark_library(cover_canvas_t *c, const char *title, const char *author, uint64_t seed) {
    uint8_t bg = (uint8_t)(3 + ((seed >> 8) & 1u));
    fill(c, 0, 0, (int)c->width, (int)c->height, bg);
    paper_grain(c, seed, bg);
    outline(c, xy(7, c->width, 176), 14);
    outline(c, xy(10, c->width, 176), 12);
    char lines[COVER_LINE_MAX][COVER_TEXT_CAP];
    int px = title_lines(title, font_px(c, 30), font_px(c, 17),
                         (int)c->width - xy(34, c->width, 176),
                         xy(96, c->height, 240), xy(6, c->height, 240), 3, lines);
    int count = 0;
    while (count < 3 && lines[count][0]) ++count;
    int step = px + xy(6, c->height, 240);
    int top = xy(author && *author ? 102 : 111, c->height, 240) - count * step / 2;
    for (int i = 0; i < count; ++i)
        draw_text(c, (int)c->width / 2, top + i * step, px, lines[i], EPD_DRAW_ALIGN_CENTER, 15, bg);
    int rule_y = xy(author && *author ? 186 : 190, c->height, 240);
    fill(c, xy(65, c->width, 176), rule_y, xy(111, c->width, 176), rule_y + 1, 13);
    if (author && *author)
        draw_author(c, author, (int)c->width / 2, xy(198, c->height, 240),
                    (int)c->width - xy(46, c->width, 176), font_px(c, 15),
                    EPD_DRAW_ALIGN_CENTER, 14, bg);
}

static void modern_whitespace(cover_canvas_t *c, const char *title, const char *author, uint64_t seed) {
    uint8_t bg = (uint8_t)(14 + ((seed >> 9) & 1u));
    fill(c, 0, 0, (int)c->width, (int)c->height, bg);
    paper_grain(c, seed, bg);
    outline(c, xy(6, c->width, 176), 4);
    outline(c, xy(8, c->width, 176), 9);
    char lines[COVER_LINE_MAX][COVER_TEXT_CAP];
    int px = title_lines(title, font_px(c, 37), font_px(c, 18),
                         (int)c->width - xy(43, c->width, 176),
                         xy(86, c->height, 240), xy(3, c->height, 240), 3, lines);
    int step = px + xy(3, c->height, 240);
    for (int i = 0; i < 3 && lines[i][0]; ++i)
        draw_text(c, xy(23, c->width, 176), xy(46, c->height, 240) + i * step,
                  px, lines[i], EPD_DRAW_ALIGN_LEFT, 2, bg);
    int left = xy(24, c->width, 176);
    fill(c, left, xy(139, c->height, 240), left + 1,
         xy(author && *author ? 185 : 203, c->height, 240), 5);
    if (author && *author)
        draw_author(c, author, left, xy(190, c->height, 240),
                    xy(100, c->width, 176), font_px(c, 15), EPD_DRAW_ALIGN_LEFT, 3, bg);
    int accent_x = xy(135 + (unsigned)((seed >> 15) & 3u), c->width, 176);
    fill(c, accent_x, xy(161, c->height, 240),
         accent_x + xy(18, c->width, 176), xy(204, c->height, 240), 12);
    fill(c, accent_x + xy(13, c->width, 176), xy(182, c->height, 240),
         accent_x + xy(24, c->width, 176), xy(209, c->height, 240), 8);
}

static void split_dark_cover(cover_canvas_t *c, const char *title, const char *author, uint64_t seed) {
    int panel_bottom = xy(108, c->height, 240);
    fill(c, 0, 0, (int)c->width, panel_bottom, 4);
    fill(c, 0, panel_bottom, (int)c->width, (int)c->height, 15);
    fill(c, 0, panel_bottom, (int)c->width, panel_bottom + 3, 8);
    for (int y = panel_bottom + 7; y < (int)c->height; y += 8)
        fill(c, 0, y, (int)c->width, y + 1, 14);
    char lines[COVER_LINE_MAX][COVER_TEXT_CAP];
    int px = title_lines(title, font_px(c, 34), font_px(c, 14),
                         (int)c->width - xy(28, c->width, 176),
                         xy(58, c->height, 240), xy(2, c->height, 240), 3, lines);
    int step = px + xy(2, c->height, 240);
    for (int i = 0; i < 3 && lines[i][0]; ++i)
        draw_text(c, (int)c->width / 2, xy(13, c->height, 240) + i * step,
                  px, lines[i], EPD_DRAW_ALIGN_CENTER, 15, 4);
    int rule_y = xy(77, c->height, 240);
    fill(c, xy(14, c->width, 176), rule_y, xy(162, c->width, 176), rule_y + 1, 13);
    if (author && *author)
        draw_author(c, author, xy(15, c->width, 176), xy(84, c->height, 240),
                    (int)c->width - xy(30, c->width, 176), font_px(c, 13),
                    EPD_DRAW_ALIGN_LEFT, 14, 4);
    char number[8];
    snprintf(number, sizeof(number), "%04u", (unsigned)((seed >> 16) % 10000u));
    draw_text(c, xy(161, c->width, 176), xy(96, c->height, 240),
              font_px(c, 10), number, EPD_DRAW_ALIGN_RIGHT, 11, 4);
}

// gen22：push 虚拟书专属封面（第 4 版式）。原先 push.txt 无内嵌封面、按路径哈希
// 落到深色版式（黑底白字占位观感），与书架里带内嵌封面的 epub 放一起很突兀。
// 这里给推送书一张极简专属封面：纸纹浅底 + 「手机 → 电波 → 书」图形 + 底部书名，
// 复用本文件的灰阶原语（fill/outline/paper_grain），不引入图片资源、不占固件体积。
// gen22: dedicated cover for the push pseudo-book (4th template). push.txt has no
// embedded art, so its path hash used to land on a dark template (black plate,
// white type) that looked out of place beside epubs with real covers. This draws a
// minimal "phone -> waves -> book" motif on grained paper using existing grayscale
// primitives - no image asset, no firmware bloat.
static void push_delivery(cover_canvas_t *c, const char *title, const char *author, uint64_t seed) {
    uint8_t bg = 14;
    fill(c, 0, 0, (int)c->width, (int)c->height, bg);
    paper_grain(c, seed, bg);
    outline(c, xy(6, c->width, 176), 4);
    outline(c, xy(8, c->width, 176), 10);
    // 手机（左）：机身 + 屏幕。/ handset: body plus screen.
    int ph_x0 = xy(34, c->width, 176), ph_x1 = xy(64, c->width, 176);
    int ph_y0 = xy(54, c->height, 240), ph_y1 = xy(118, c->height, 240);
    fill(c, ph_x0, ph_y0, ph_x1, ph_y1, 5);
    fill(c, ph_x0 + xy(3, c->width, 176), ph_y0 + xy(6, c->height, 240),
         ph_x1 - xy(3, c->width, 176), ph_y1 - xy(10, c->height, 240), 12);
    // 推送电波（中）：三条由密到疏的短横，读起来像向外发送。
    // waves: three shortening bars read as an outgoing transmission.
    for (int i = 0; i < 3; ++i) {
        int y = xy(74, c->height, 240) + i * xy(12, c->height, 240);
        int x0 = xy(70, c->width, 176) + i * xy(2, c->width, 176);
        fill(c, x0, y, x0 + xy(10 - i * 2, c->width, 176), y + xy(2, c->height, 240),
             (uint8_t)(9 - i * 2));
    }
    // 书（右）：封面 + 书脊 + 页面线。/ book: cover, spine, page rules.
    int bk_x0 = xy(96, c->width, 176), bk_x1 = xy(140, c->width, 176);
    int bk_y0 = xy(66, c->height, 240), bk_y1 = xy(112, c->height, 240);
    fill(c, bk_x0, bk_y0, bk_x1, bk_y1, 7);
    fill(c, bk_x0, bk_y0, bk_x0 + xy(4, c->width, 176), bk_y1, 3);
    for (int i = 0; i < 3; ++i) {
        int y = bk_y0 + xy(10, c->height, 240) + i * xy(12, c->height, 240);
        fill(c, bk_x0 + xy(9, c->width, 176), y, bk_x1 - xy(6, c->width, 176),
             y + xy(2, c->height, 240), 11);
    }
    // 书名。/ title block.
    char lines[COVER_LINE_MAX][COVER_TEXT_CAP];
    int px = title_lines(title, font_px(c, 34), font_px(c, 17),
                         (int)c->width - xy(40, c->width, 176),
                         xy(70, c->height, 240), xy(3, c->height, 240), 2, lines);
    int step = px + xy(3, c->height, 240);
    for (int i = 0; i < 2 && lines[i][0]; ++i)
        draw_text(c, xy(20, c->width, 176), xy(150, c->height, 240) + i * step,
                  px, lines[i], EPD_DRAW_ALIGN_LEFT, 2, bg);
    if (author && *author)
        draw_author(c, author, xy(20, c->width, 176), xy(196, c->height, 240),
                    xy(120, c->width, 176), font_px(c, 14), EPD_DRAW_ALIGN_LEFT, 5, bg);
    // 底部推送色条。/ bottom accent bar.
    fill(c, xy(20, c->width, 176), xy(216, c->height, 240),
         xy(76, c->width, 176), xy(219, c->height, 240), 6);
}

// push.txt 判定：只认路径末段，避免与 book/ 侧的模块常量耦合。
// push.txt test: match the path tail only, keeping this module free of book/ constants.
static bool is_push_identity(const char *identity) {
    if (!identity || !*identity) return false;
    const char *slash = strrchr(identity, '/');
    const char *name = slash ? slash + 1 : identity;
    return strcmp(name, "push.txt") == 0;
}

bool book_auto_cover_render(const char *identity, const char *title, const char *author,
                            unsigned width, unsigned height, uint8_t *out) {
    if (!out || !title || !*title || width < 96 || height < 128 ||
        width > 320 || height > 440 || width + 104 >= (unsigned)epd_rotated_display_width() ||
        height > (unsigned)epd_rotated_display_height()) return false;
    unsigned stride = (unsigned)epd_width() / 2;
    uint8_t *scratch = heap_caps_malloc((size_t)stride * epd_height(),
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!scratch) return false;
    memset(scratch, 0xff, (size_t)stride * epd_height());
    cover_canvas_t canvas = {.fb = scratch, .width = width, .height = height, .stride = stride};
    uint64_t seed = cover_seed(identity);
    if (is_push_identity(identity)) push_delivery(&canvas, title, author, seed);
    else switch (seed % 3u) {
        case 0: dark_library(&canvas, title, author, seed); break;
        case 1: modern_whitespace(&canvas, title, author, seed); break;
        default: split_dark_cover(&canvas, title, author, seed); break;
    }
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
            out[(size_t)y * width + x] = (uint8_t)(get(&canvas, (int)x, (int)y) * 17u);
    free(scratch);
    return true;
}
