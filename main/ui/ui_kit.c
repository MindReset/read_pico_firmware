/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * ui_kit 绘制原语的实现。布局常量在头文件。
 *
 * Implementation of the ui_kit drawing primitives. Layout constants live
 * in the header.
 */

#include "ui_kit.h"

#include <string.h>

#include "datamatrix.h"
#include "ttf_font.h"

#define UI_SEL_INSET 4
#define UI_SEL_RING 3

void ui_text(
    uint8_t* framebuffer, int x, int y_top, int px, const char* text,
    enum EpdFontFlags align, bool inverted
) {
    ttf_draw_text_px(
        framebuffer, x, y_top + ttf_ascender_px(px), px, text, align,
        inverted ? UI_INK_WHITE : UI_INK_BLACK,
        inverted ? UI_INK_BLACK : UI_INK_WHITE
    );
}

void ui_text_bw(
    uint8_t* framebuffer, int x, int y_top, int px, const char* text,
    enum EpdFontFlags align, bool inverted
) {
    ttf_draw_text_px_bw(
        framebuffer, x, y_top + ttf_ascender_px(px), px, text, align,
        inverted ? UI_INK_WHITE : UI_INK_BLACK,
        inverted ? UI_INK_BLACK : UI_INK_WHITE
    );
}

void ui_text_vc(
    uint8_t* framebuffer, int x, int center_y, int px, const char* text,
    enum EpdFontFlags align, bool inverted
) {
    int above = 0;
    int below = 0;
    ttf_measure_line_px(px, text, &above, &below);
    ttf_draw_text_px(
        framebuffer, x, center_y + (above - below) / 2, px, text, align,
        inverted ? UI_INK_WHITE : UI_INK_BLACK,
        inverted ? UI_INK_BLACK : UI_INK_WHITE
    );
}

void ui_blit_bmp(
    uint8_t* framebuffer, int x, int y, const ui_fallback_bmp_t* bmp
) {
    const int stride = (bmp->width + 7) / 8;
    for (int row = 0; row < bmp->height; row++) {
        const uint8_t* src = bmp->bits + (size_t)row * stride;
        for (int col = 0; col < bmp->width; col++) {
            if ((src[col >> 3] & (uint8_t)(0x80 >> (col & 7))) != 0) {
                epd_draw_pixel(x + col, y + row, UI_GRAY_BLACK, framebuffer);
            }
        }
    }
}

static int clamp_radius(EpdRect rect, int radius) {
    if (radius > rect.width / 2) radius = rect.width / 2;
    if (radius > rect.height / 2) radius = rect.height / 2;
    return radius < 0 ? 0 : radius;
}

// 圆角描边用的四分之一圆弧。epdiy 只给了填充版的 epd_fill_circle_helper，没有
// 描边版，所以这里自己跑一遍中点画圆，按 corner 位掩码只画需要的那个象限：
// 1 左上、2 右上、4 右下、8 左下。
// Quarter-circle stroke. epdiy only has filled epd_fill_circle_helper, so this
// midpoint-circle pass draws the quadrant selected by the corner mask:
// 1 top-left, 2 top-right, 4 bottom-right, 8 bottom-left.
static void draw_arc(
    uint8_t* framebuffer, int cx, int cy, int r, int corner, uint8_t color
) {
    int f = 1 - r;
    int dx = 1;
    int dy = -2 * r;
    int x = 0;
    int y = r;

    while (x <= y) {
        if (corner & 1) {
            epd_draw_pixel(cx - x, cy - y, color, framebuffer);
            epd_draw_pixel(cx - y, cy - x, color, framebuffer);
        }
        if (corner & 2) {
            epd_draw_pixel(cx + x, cy - y, color, framebuffer);
            epd_draw_pixel(cx + y, cy - x, color, framebuffer);
        }
        if (corner & 4) {
            epd_draw_pixel(cx + x, cy + y, color, framebuffer);
            epd_draw_pixel(cx + y, cy + x, color, framebuffer);
        }
        if (corner & 8) {
            epd_draw_pixel(cx - x, cy + y, color, framebuffer);
            epd_draw_pixel(cx - y, cy + x, color, framebuffer);
        }
        if (f >= 0) {
            y--;
            dy += 2;
            f += dy;
        }
        x++;
        dx += 2;
        f += dx;
    }
}

void ui_draw_round_rect(
    uint8_t* framebuffer, EpdRect rect, int radius, uint8_t color
) {
    if (rect.width <= 0 || rect.height <= 0) return;
    const int r = clamp_radius(rect, radius);
    const int x1 = rect.x + rect.width - 1;
    const int y1 = rect.y + rect.height - 1;
    const int span_h = rect.width - 2 * r;
    const int span_v = rect.height - 2 * r;

    if (span_h > 0) {
        epd_fill_rect(
            (EpdRect){ .x = rect.x + r, .y = rect.y, .width = span_h, .height = 1 },
            color, framebuffer
        );
        epd_fill_rect(
            (EpdRect){ .x = rect.x + r, .y = y1, .width = span_h, .height = 1 },
            color, framebuffer
        );
    }
    if (span_v > 0) {
        epd_fill_rect(
            (EpdRect){ .x = rect.x, .y = rect.y + r, .width = 1, .height = span_v },
            color, framebuffer
        );
        epd_fill_rect(
            (EpdRect){ .x = x1, .y = rect.y + r, .width = 1, .height = span_v },
            color, framebuffer
        );
    }
    if (r > 0) {
        draw_arc(framebuffer, rect.x + r, rect.y + r, r, 1, color);
        draw_arc(framebuffer, x1 - r, rect.y + r, r, 2, color);
        draw_arc(framebuffer, x1 - r, y1 - r, r, 4, color);
        draw_arc(framebuffer, rect.x + r, y1 - r, r, 8, color);
    }
}

EpdRect ui_inset_rect(EpdRect r, int d) {
    return (EpdRect){
        .x = r.x + d,
        .y = r.y + d,
        .width = r.width - 2 * d,
        .height = r.height - 2 * d,
    };
}

void ui_draw_selected_round_rect(
    uint8_t* framebuffer, EpdRect rect, int radius
) {
    ui_draw_round_rect(framebuffer, rect, radius, UI_GRAY_BLACK);
    for (int i = 0; i < UI_SEL_RING; i++) {
        int d = UI_SEL_INSET + i;
        EpdRect inner = ui_inset_rect(rect, d);
        if (inner.width < 8 || inner.height < 8) break;
        int r = radius - d;
        if (r < 2) r = 2;
        ui_draw_round_rect(framebuffer, inner, r, UI_GRAY_BLACK);
    }
}

void ui_draw_choice_round_rect(
    uint8_t* framebuffer, EpdRect rect, int radius, bool on
) {
    if (on) {
        ui_draw_selected_round_rect(framebuffer, rect, radius);
    } else {
        ui_draw_round_rect(framebuffer, rect, radius, UI_GRAY_BLACK);
    }
}

void ui_fill_round_rect(
    uint8_t* framebuffer, EpdRect rect, int radius, uint8_t color
) {
    if (rect.width <= 0 || rect.height <= 0) return;
    radius = clamp_radius(rect, radius);

    epd_fill_rect(
        (EpdRect){
            .x = rect.x + radius,
            .y = rect.y,
            .width = rect.width - 2 * radius,
            .height = rect.height,
        },
        color,
        framebuffer
    );
    int delta = rect.height - 2 * radius - 1;
    epd_fill_circle_helper(
        rect.x + rect.width - radius - 1, rect.y + radius, radius, 1, delta,
        color, framebuffer
    );
    epd_fill_circle_helper(
        rect.x + radius, rect.y + radius, radius, 2, delta, color, framebuffer
    );
}

bool ui_rect_hit(EpdRect rect, uint16_t x, uint16_t y) {
    return (int)x >= rect.x && (int)x < rect.x + rect.width
        && (int)y >= rect.y && (int)y < rect.y + rect.height;
}

EpdRect ui_grid_rect(int col, int cols, int row, int y0, int h) {
    const int width = epd_rotated_display_width();
    const int gap = UI_GAP;
    const int w = (width - 2 * UI_MARGIN - (cols - 1) * gap) / cols;
    return (EpdRect){
        .x = UI_MARGIN + col * (w + gap),
        .y = y0 + row * (h + gap),
        .width = w,
        .height = h,
    };
}

EpdRect ui_row_rect(int col, int cols, int y0, int h) {
    return ui_grid_rect(col, cols, 0, y0, h);
}

int ui_grid_hit(
    uint16_t x, uint16_t y, int cols, int rows, int y0, int h, const int* ids
) {
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            if (ui_rect_hit(ui_grid_rect(c, cols, r, y0, h), x, y)) {
                return ids[r * cols + c];
            }
        }
    }
    return -1;
}

EpdRect ui_bar_rect(int col, int cols) {
    const int right = epd_rotated_display_width()
        - UI_BAR_MARGIN - UI_MENU_BTN_SIZE - UI_GAP;
    const int avail = right - UI_MARGIN;
    const int w = (avail - (cols - 1) * UI_GAP) / cols;
    return (EpdRect){
        .x = UI_MARGIN + col * (w + UI_GAP),
        .y = UI_BAR_TOP,
        .width = w,
        .height = UI_BAR_H,
    };
}

int ui_bar_hit(uint16_t x, uint16_t y, int cols) {
    for (int i = 0; i < cols; i++) {
        if (ui_rect_hit(ui_bar_rect(i, cols), x, y)) return i;
    }
    return -1;
}

int ui_content_right(void) {
    return epd_rotated_display_width() - UI_MARGIN;
}

int ui_content_width(void) {
    return epd_rotated_display_width() - 2 * UI_MARGIN;
}

EpdRect ui_content_refresh_area(void) {
    const int y0 = UI_HEADER_RULE_Y;
    return (EpdRect){
        .x = UI_MARGIN - 10,
        .y = y0,
        .width = ui_content_width() + 20,
        .height = UI_CONTENT_BOTTOM - y0,
    };
}

void ui_hairline(uint8_t* framebuffer, int y, int x, int w, uint8_t color) {
    epd_fill_rect(
        (EpdRect){ .x = x, .y = y, .width = w, .height = 1 }, color, framebuffer
    );
}

static void header_ink_span(int y_top, int px, const char* text, int* top, int* bot) {
    int above = 0;
    int below = 0;
    if (ttf_font_ready() && text != NULL && text[0] != '\0') {
        ttf_measure_line_px(px, text, &above, &below);
        const int base = y_top + ttf_ascender_px(px);
        *top = base - above;
        *bot = base + below;
        return;
    }
    *top = y_top;
    *bot = y_top + px;
}

ui_header_skel_t ui_header_skel_box(
    const char* title, const char* subtitle, int acc_w, int acc_h
) {
    ui_header_skel_t s = {
        .text = {
            .x = UI_MARGIN,
            .y = 0,
            .width = ui_content_width(),
            .height = UI_HEADER_RULE_Y,
        },
        .accessory = { 0 },
    };
    if (acc_w < 8 || acc_h < 8) return s;

    EpdRect acc = {
        .x = ui_content_right() - acc_w,
        .y = 0,
        .width = acc_w,
        .height = acc_h,
    };
    int top = UI_HEADER_TITLE_Y;
    int bottom = UI_HEADER_TITLE_Y + UI_PX_TITLE;
    header_ink_span(UI_HEADER_TITLE_Y, UI_PX_TITLE, title, &top, &bottom);
    if (subtitle != NULL && subtitle[0] != '\0') {
        int sub_top = UI_HEADER_SUB_Y;
        header_ink_span(UI_HEADER_SUB_Y, UI_PX_SUB, subtitle, &sub_top, &bottom);
    }
    const int band = bottom - top;
    if (band >= acc_h) {
        acc.y = top + (band - acc_h) / 2;
    } else {
        acc.y = top;
        if (acc.y + acc.height > UI_HEADER_RULE_Y) {
            acc.y = UI_HEADER_RULE_Y - acc.height;
        }
        if (acc.y < 0) acc.y = 0;
    }
    s.accessory = acc;
    int text_w = acc.x - UI_GAP - UI_MARGIN;
    if (text_w < 0) text_w = 0;
    s.text.width = text_w;
    return s;
}

ui_header_skel_t ui_header_skel(
    const char* title, const char* subtitle, int mark_side
) {
    return ui_header_skel_box(title, subtitle, mark_side, mark_side);
}

int ui_draw_header_skel(
    uint8_t* framebuffer, const ui_header_skel_t* skel,
    const char* title, const char* subtitle
) {
    if (framebuffer == NULL) return UI_CONTENT_TOP;
    ui_text(
        framebuffer, UI_MARGIN, UI_HEADER_TITLE_Y, UI_PX_TITLE,
        title != NULL ? title : "",
        EPD_DRAW_ALIGN_LEFT, false
    );
    if (subtitle != NULL && subtitle[0] != '\0') {
        ui_text(
            framebuffer, UI_MARGIN, UI_HEADER_SUB_Y, UI_PX_SUB, subtitle,
            EPD_DRAW_ALIGN_LEFT, false
        );
    }
    if (skel != NULL && skel->text.width < ui_content_width()) {
        ui_clear_rect_fast(framebuffer, (EpdRect){
            .x = skel->text.x + skel->text.width,
            .y = 0,
            .width = ui_content_right() - (skel->text.x + skel->text.width),
            .height = UI_HEADER_RULE_Y,
        });
    }
    ui_hairline(
        framebuffer, UI_HEADER_RULE_Y, UI_MARGIN, ui_content_width(), UI_GRAY_BLACK
    );
    return UI_CONTENT_TOP;
}

int ui_draw_header(
    uint8_t* framebuffer, const char* title, const char* subtitle
) {
    ui_header_skel_t s = ui_header_skel(title, subtitle, 0);
    return ui_draw_header_skel(framebuffer, &s, title, subtitle);
}

int ui_draw_section(uint8_t* framebuffer, int y, const char* title) {
    ui_text(
        framebuffer, UI_MARGIN, y, UI_PX_SECTION, title,
        EPD_DRAW_ALIGN_LEFT, false
    );
    int rule_y = y + UI_PX_SECTION + 8;
    ui_hairline(framebuffer, rule_y, UI_MARGIN, ui_content_width(), UI_GRAY_BLACK);
    return rule_y + 10;
}

int ui_draw_row(
    uint8_t* framebuffer, int y, const char* label, const char* value
) {
    const int center = y + UI_ROW_H / 2;
    ui_text_vc(
        framebuffer, UI_MARGIN, center, UI_PX_LABEL, label,
        EPD_DRAW_ALIGN_LEFT, false
    );
    if (value != NULL && value[0] != '\0') {
        ui_text_vc(
            framebuffer, ui_content_right(), center, UI_PX_VALUE, value,
            EPD_DRAW_ALIGN_RIGHT, false
        );
    }
    ui_hairline(
        framebuffer, y + UI_ROW_H - 1, UI_MARGIN, ui_content_width(), UI_GRAY_LIGHT
    );
    return y + UI_ROW_H;
}

int ui_draw_row2(
    uint8_t* framebuffer, int y, const char* l1, const char* v1,
    const char* l2, const char* v2
) {
    const int center = y + UI_ROW_H_SM / 2;
    const int half = ui_content_width() / 2;
    const int mid = UI_MARGIN + half;
    const bool two = l2 != NULL && l2[0] != '\0';
    ui_text_vc(
        framebuffer, UI_MARGIN, center, UI_PX_LABEL_SM, l1,
        EPD_DRAW_ALIGN_LEFT, false
    );
    if (v1 != NULL) {
        ui_text_vc(
            framebuffer, two ? mid - UI_PAD : ui_content_right(),
            center, UI_PX_VALUE_SM, v1, EPD_DRAW_ALIGN_RIGHT, false
        );
    }
    if (two) {
        ui_text_vc(
            framebuffer, mid + UI_PAD, center, UI_PX_LABEL_SM, l2,
            EPD_DRAW_ALIGN_LEFT, false
        );
        if (v2 != NULL) {
            ui_text_vc(
                framebuffer, ui_content_right(), center, UI_PX_VALUE_SM, v2,
                EPD_DRAW_ALIGN_RIGHT, false
            );
        }
    }
    ui_hairline(
        framebuffer, y + UI_ROW_H_SM - 1, UI_MARGIN, ui_content_width(),
        UI_GRAY_LIGHT
    );
    return y + UI_ROW_H_SM;
}

int ui_draw_hero(
    uint8_t* framebuffer, int y, const char* label, const char* value
) {
    ui_text(
        framebuffer, UI_MARGIN, y + 22, UI_PX_LABEL, label,
        EPD_DRAW_ALIGN_LEFT, false
    );
    ui_text(
        framebuffer, ui_content_right(), y - 8, UI_PX_HERO, value,
        EPD_DRAW_ALIGN_RIGHT, false
    );
    return y + UI_PX_HERO + 10;
}

void ui_draw_button(
    uint8_t* framebuffer, EpdRect rect, const char* label, bool on
) {
    ui_draw_choice_round_rect(framebuffer, rect, UI_BTN_RADIUS, on);
    ui_text_vc(
        framebuffer, rect.x + rect.width / 2, rect.y + rect.height / 2,
        UI_PX_BTN, label, EPD_DRAW_ALIGN_CENTER, false
    );
}

void ui_draw_chip(
    uint8_t* framebuffer, EpdRect rect, const char* label, bool on
) {
    if (on) {
        ui_draw_selected_round_rect(framebuffer, rect, UI_CHIP_RADIUS);
    } else {
        ui_draw_round_rect(framebuffer, rect, UI_CHIP_RADIUS, UI_GRAY_LIGHT);
    }
    ui_text_vc(
        framebuffer, rect.x + rect.width / 2, rect.y + rect.height / 2,
        UI_PX_LABEL_SM, label, EPD_DRAW_ALIGN_CENTER, false
    );
}

// Code 39 条码。/ Code 39 barcode.
#define C39_MAX 32
#define C39_MIN_NARROW 2
#define C39_WIDE_RATIO 2
#define C39_CHAR_UNITS (6 + 3 * C39_WIDE_RATIO)
#define C39_GAP_UNITS 1
#define C39_QUIET_UNITS 10
#define C39_ROW_GAP 12

static int c39_units(int n) {
    return (n + 2) * C39_CHAR_UNITS + (n + 1) * C39_GAP_UNITS
        + 2 * C39_QUIET_UNITS;
}

static int c39_max_chars(int width, int narrow) {
    int lo = 1;
    int hi = C39_MAX;
    int best = 0;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (c39_units(mid) * narrow <= width) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return best;
}

static bool c39_plan(const char* text, int width, int* out_n, int* out_max_n) {
    if (text == NULL || text[0] == '\0') return false;
    int n = (int)strlen(text);
    if (n > C39_MAX) n = C39_MAX;
    int max_n = c39_max_chars(width, C39_MIN_NARROW);
    if (max_n < 1) max_n = 1;
    *out_n = n;
    *out_max_n = max_n;
    return true;
}

static uint16_t c39_pattern(int c) {
    if (c >= 'a' && c <= 'z') c -= 32;
    switch (c) {
        case '0': return 0x034;
        case '1': return 0x121;
        case '2': return 0x061;
        case '3': return 0x160;
        case '4': return 0x031;
        case '5': return 0x130;
        case '6': return 0x070;
        case '7': return 0x025;
        case '8': return 0x124;
        case '9': return 0x064;
        case 'A': return 0x109;
        case 'B': return 0x049;
        case 'C': return 0x148;
        case 'D': return 0x019;
        case 'E': return 0x118;
        case 'F': return 0x058;
        case 'G': return 0x00D;
        case 'H': return 0x10C;
        case 'I': return 0x04C;
        case 'J': return 0x01C;
        case 'K': return 0x103;
        case 'L': return 0x043;
        case 'M': return 0x142;
        case 'N': return 0x013;
        case 'O': return 0x112;
        case 'P': return 0x052;
        case 'Q': return 0x007;
        case 'R': return 0x106;
        case 'S': return 0x046;
        case 'T': return 0x016;
        case 'U': return 0x181;
        case 'V': return 0x0C1;
        case 'W': return 0x1C0;
        case 'X': return 0x091;
        case 'Y': return 0x190;
        case 'Z': return 0x0D0;
        case '-': return 0x085;
        case '.': return 0x184;
        case ' ': return 0x0C4;
        case '*': return 0x094;
        case '$': return 0x0A8;
        case '/': return 0x0A2;
        case '+': return 0x08A;
        case '%': return 0x02A;
        default: return 0x085;
    }
}

static void c39_char(
    uint8_t* framebuffer, int* x, int y, int h, int narrow, int wide, int ch
) {
    const uint16_t pat = c39_pattern(ch);
    for (int i = 8; i >= 0; i--) {
        const int w = ((pat >> i) & 1) ? wide : narrow;
        if (((8 - i) & 1) == 0) {
            epd_fill_rect(
                (EpdRect){ .x = *x, .y = y, .width = w, .height = h },
                UI_GRAY_BLACK, framebuffer
            );
        }
        *x += w;
    }
}

static void c39_draw_one(
    uint8_t* framebuffer, EpdRect rect, const char* text, int n
) {
    const int units = c39_units(n);
    int narrow = rect.width / units;
    if (narrow < C39_MIN_NARROW) narrow = C39_MIN_NARROW;
    const int wide = narrow * C39_WIDE_RATIO;
    const int used = units * narrow;
    int x = rect.x + (rect.width - used) / 2 + C39_QUIET_UNITS * narrow;
    const int y = rect.y;
    const int h = rect.height;
    if (h <= 0) return;

    c39_char(framebuffer, &x, y, h, narrow, wide, '*');
    x += C39_GAP_UNITS * narrow;
    for (int i = 0; i < n; i++) {
        c39_char(framebuffer, &x, y, h, narrow, wide, (unsigned char)text[i]);
        x += C39_GAP_UNITS * narrow;
    }
    c39_char(framebuffer, &x, y, h, narrow, wide, '*');
}

int ui_barcode_rows(const char* text, int width) {
    int n = 0;
    int max_n = 0;
    if (!c39_plan(text, width, &n, &max_n)) return 1;
    return (n + max_n - 1) / max_n;
}

void ui_draw_barcode(uint8_t* framebuffer, EpdRect rect, const char* text) {
    if (framebuffer == NULL || text == NULL || text[0] == '\0') return;
    if (rect.width <= 0 || rect.height <= 0) return;

    int n = 0;
    int max_n = 0;
    if (!c39_plan(text, rect.width, &n, &max_n)) return;

    int rows = (n + max_n - 1) / max_n;
    int chunk = (n + rows - 1) / rows;
    int row_h = rect.height / rows;
    int inner = row_h - C39_ROW_GAP;
    if (inner < row_h / 2) inner = row_h;
    int y = rect.y;
    int off = 0;
    for (int r = 0; r < rows && off < n; r++) {
        int take = n - off;
        if (take > chunk) take = chunk;
        c39_draw_one(
            framebuffer,
            (EpdRect){
                .x = rect.x, .y = y, .width = rect.width, .height = inner
            },
            text + off, take
        );
        off += take;
        y += row_h;
    }
}

// Code 128 Auto 自动切换码集。/ Code 128 Auto.
static const uint16_t k_c128[] = {
    0x6CC, 0x66C, 0x666, 0x498, 0x48C, 0x44C, 0x4C8, 0x4C4, 0x464, 0x648,
    0x644, 0x624, 0x59C, 0x4DC, 0x4CE, 0x5CC, 0x4EC, 0x4E6, 0x672, 0x65C,
    0x64E, 0x6E4, 0x674, 0x76E, 0x74C, 0x72C, 0x726, 0x764, 0x734, 0x732,
    0x6D8, 0x6C6, 0x636, 0x518, 0x458, 0x446, 0x588, 0x468, 0x462, 0x688,
    0x628, 0x622, 0x5B8, 0x58E, 0x46E, 0x5D8, 0x5C6, 0x476, 0x776, 0x68E,
    0x62E, 0x6E8, 0x6E2, 0x6EE, 0x758, 0x746, 0x716, 0x768, 0x762, 0x71A,
    0x77A, 0x642, 0x78A, 0x530, 0x50C, 0x4B0, 0x486, 0x42C, 0x426, 0x590,
    0x584, 0x4D0, 0x4C2, 0x434, 0x432, 0x612, 0x650, 0x7BA, 0x614, 0x47A,
    0x53C, 0x4BC, 0x49E, 0x5E4, 0x4F4, 0x4F2, 0x7A4, 0x794, 0x792, 0x6DE,
    0x6F6, 0x7B6, 0x578, 0x51E, 0x45E, 0x5E8, 0x5E2, 0x7A8, 0x7A2, 0x5DE,
    0x5EE, 0x75E, 0x7AE, 0x684, 0x690, 0x69C,
};
#define C128_CODE_C 99
#define C128_CODE_B 100
#define C128_CODE_A 101
#define C128_START_A 103
#define C128_START_B 104
#define C128_START_C 105
#define C128_STOP 0x18EB
#define C128_STOP_BITS 13
#define C128_QUIET 10
#define C128_MAX 64
#define C128_MIN_MOD 2
#define C128_ROW_GAP 12
#define C128_SET_A 0
#define C128_SET_B 1
#define C128_SET_C 2

static int c128_modules(int n) {
    return 11 * (n + 3) + 2 + 2 * C128_QUIET;
}

static int c128_max_chars(int width, int min_mod) {
    int lo = 1;
    int hi = C128_MAX;
    int best = 0;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (c128_modules(mid) * min_mod <= width) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return best;
}

static bool c128_plan(const char* text, int width, int* out_n, int* out_max_n) {
    if (text == NULL || text[0] == '\0') return false;
    int n = (int)strlen(text);
    if (n > C128_MAX) n = C128_MAX;
    int max_n = c128_max_chars(width, C128_MIN_MOD);
    if (max_n < 1) max_n = 1;
    *out_n = n;
    *out_max_n = max_n;
    return true;
}

static void c128_bars(
    uint8_t* framebuffer, int x, int y, int h, int mod, uint16_t pat, int bits
) {
    int run = 0;
    int run_on = 0;
    int px = x;
    for (int i = bits - 1; i >= 0; i--) {
        const int on = (pat >> i) & 1;
        if (run > 0 && on != run_on) {
            if (run_on) {
                epd_fill_rect(
                    (EpdRect){ .x = px, .y = y, .width = run * mod, .height = h },
                    UI_GRAY_BLACK, framebuffer
                );
            }
            px += run * mod;
            run = 0;
        }
        run_on = on;
        run++;
    }
    if (run > 0 && run_on) {
        epd_fill_rect(
            (EpdRect){ .x = px, .y = y, .width = run * mod, .height = h },
            UI_GRAY_BLACK, framebuffer
        );
    }
}

static int c128_digits(const char* text, int i, int n) {
    int k = 0;
    while (i + k < n && text[i + k] >= '0' && text[i + k] <= '9') k++;
    return k;
}

static int c128_val_a(int c) {
    if (c < 32) return c + 64;
    if (c < 96) return c - 32;
    return '?' - 32;
}

static int c128_val_b(int c) {
    if (c >= 32 && c <= 127) return c - 32;
    return '?' - 32;
}

static int c128_encode(const char* text, int n, uint8_t* out, int max) {
    if (n < 1 || max < 2) return 0;
    const int d0 = c128_digits(text, 0, n);
    int set;
    int k = 0;
    if (d0 >= 4 || (d0 == n && n >= 2 && (n % 2) == 0)) {
        out[k++] = C128_START_C;
        set = C128_SET_C;
    } else if ((unsigned char)text[0] < 32) {
        out[k++] = C128_START_A;
        set = C128_SET_A;
    } else {
        out[k++] = C128_START_B;
        set = C128_SET_B;
    }

    int i = 0;
    while (i < n && k < max) {
        if (set == C128_SET_C) {
            if (i + 1 < n && text[i] >= '0' && text[i] <= '9'
                && text[i + 1] >= '0' && text[i + 1] <= '9') {
                out[k++] = (uint8_t)((text[i] - '0') * 10 + (text[i + 1] - '0'));
                i += 2;
            } else if ((unsigned char)text[i] < 32) {
                out[k++] = C128_CODE_A;
                set = C128_SET_A;
            } else {
                out[k++] = C128_CODE_B;
                set = C128_SET_B;
            }
            continue;
        }

        int d = c128_digits(text, i, n);
        if (d >= 4) {
            if (d % 2) {
                int c = (unsigned char)text[i];
                out[k++] = (uint8_t)(set == C128_SET_A ? c128_val_a(c) : c128_val_b(c));
                i++;
            }
            if (k < max) out[k++] = C128_CODE_C;
            set = C128_SET_C;
            continue;
        }

        int c = (unsigned char)text[i];
        if (set == C128_SET_B && c < 32) {
            out[k++] = C128_CODE_A;
            set = C128_SET_A;
            continue;
        }
        if (set == C128_SET_A && c >= 96) {
            out[k++] = C128_CODE_B;
            set = C128_SET_B;
            continue;
        }
        out[k++] = (uint8_t)(set == C128_SET_A ? c128_val_a(c) : c128_val_b(c));
        i++;
    }
    return k;
}

static void c128_draw_one(
    uint8_t* framebuffer, EpdRect rect, const char* text, int n
) {
    uint8_t codes[C128_MAX];
    int used = c128_encode(text, n, codes, C128_MAX);
    if (used < 1) return;

    int sum = codes[0];
    for (int i = 1; i < used; i++) sum += (int)codes[i] * i;
    const int check = sum % 103;
    const int modules = c128_modules(used - 1);
    int mod = rect.width / modules;
    if (mod < C128_MIN_MOD) mod = C128_MIN_MOD;
    const int bar_w = modules * mod;
    int x = rect.x + (rect.width - bar_w) / 2 + C128_QUIET * mod;
    const int y = rect.y;
    const int h = rect.height;
    if (h <= 0) return;

    for (int i = 0; i < used; i++) {
        c128_bars(framebuffer, x, y, h, mod, k_c128[codes[i]], 11);
        x += 11 * mod;
    }
    c128_bars(framebuffer, x, y, h, mod, k_c128[check], 11);
    x += 11 * mod;
    c128_bars(framebuffer, x, y, h, mod, C128_STOP, C128_STOP_BITS);
}

int ui_code128_rows(const char* text, int width) {
    int n = 0;
    int max_n = 0;
    if (!c128_plan(text, width, &n, &max_n)) return 1;
    return (n + max_n - 1) / max_n;
}

void ui_draw_code128(uint8_t* framebuffer, EpdRect rect, const char* text) {
    if (framebuffer == NULL || text == NULL || text[0] == '\0') return;
    if (rect.width <= 0 || rect.height <= 0) return;

    int n = 0;
    int max_n = 0;
    if (!c128_plan(text, rect.width, &n, &max_n)) return;

    int rows = (n + max_n - 1) / max_n;
    int chunk = (n + rows - 1) / rows;
    int row_h = rect.height / rows;
    int inner = row_h - C128_ROW_GAP;
    if (inner < row_h / 2) inner = row_h;
    int y = rect.y;
    int off = 0;
    for (int r = 0; r < rows && off < n; r++) {
        int take = n - off;
        if (take > chunk) take = chunk;
        c128_draw_one(
            framebuffer,
            (EpdRect){
                .x = rect.x, .y = y, .width = rect.width, .height = inner
            },
            text + off, take
        );
        off += take;
        y += row_h;
    }
}

#define DM_QUIET 2
#define DM_MIN_MOD 2
// 297 PPI 上 1 px ≈ 3.4 mil。4.7 mil 枪要 ≥2 px；1 px 低于解析能力。
// At 297 PPI, 1 px ≈ 3.4 mil. A 4.7 mil scanner needs ≥2 px; 1 px is below resolve.
#define DM_MOD 4

static int dm_fit(EpdRect rect, int dim, int* mod, EpdRect* used) {
    if (dim < 1 || rect.width <= 0 || rect.height <= 0) return -1;
    const int cells = dim + 2 * DM_QUIET;
    const int side = rect.width < rect.height ? rect.width : rect.height;
    int m = DM_MOD;
    if (m * cells > side) m = side / cells;
    if (m < DM_MIN_MOD) m = DM_MIN_MOD;
    const int s = cells * m;
    *mod = m;
    used->x = rect.x + (rect.width - s) / 2;
    used->y = rect.y;
    used->width = s;
    used->height = s;
    return 0;
}

int ui_datamatrix_side(const char* text) {
    uint8_t mods[DM_MAX_DIM * DM_MAX_DIM];
    int dim = 0;
    if (text == NULL || text[0] == '\0') return 0;
    if (dm_encode(text, mods, &dim) != 0 || dim < 1) return 0;
    return (dim + 2 * DM_QUIET) * DM_MOD;
}

EpdRect ui_datamatrix_rect(EpdRect rect, const char* text) {
    EpdRect used = { 0 };
    if (text == NULL || text[0] == '\0') return used;
    uint8_t mods[DM_MAX_DIM * DM_MAX_DIM];
    int dim = 0;
    int mod = 0;
    if (dm_encode(text, mods, &dim) != 0) return used;
    dm_fit(rect, dim, &mod, &used);
    return used;
}

void ui_draw_datamatrix(uint8_t* framebuffer, EpdRect rect, const char* text) {
    if (framebuffer == NULL || text == NULL || text[0] == '\0') return;
    if (rect.width <= 0 || rect.height <= 0) return;

    uint8_t mods[DM_MAX_DIM * DM_MAX_DIM];
    int dim = 0;
    if (dm_encode(text, mods, &dim) != 0 || dim < 1) return;

    int mod = 0;
    EpdRect used = { 0 };
    if (dm_fit(rect, dim, &mod, &used) != 0) return;
    const int x0 = used.x + DM_QUIET * mod;
    const int y0 = used.y + DM_QUIET * mod;

    for (int r = 0; r < dim; r++) {
        int run0 = -1;
        for (int c = 0; c <= dim; c++) {
            const int on = (c < dim && mods[r * dim + c]);
            if (on && run0 < 0) run0 = c;
            if (!on && run0 >= 0) {
                epd_fill_rect(
                    (EpdRect){
                        .x = x0 + run0 * mod,
                        .y = y0 + r * mod,
                        .width = (c - run0) * mod,
                        .height = mod,
                    },
                    UI_GRAY_BLACK, framebuffer
                );
                run0 = -1;
            }
        }
    }
}

void ui_clear_page(uint8_t* framebuffer) {
    memset(framebuffer, 0xFF, (size_t)epd_width() * epd_height() / 2);
}

// 旋转都是 90° 的整数倍，逻辑矩形映射过去仍是矩形，所以取两个对角点的包围盒就
// 精确等价。四个分支跟 epdiy 内部的 _rotate() 一一对应：横屏 px=lx，竖屏 px=ly。
// Rotations are multiples of 90°, so the mapped logical rect stays a rect; the
// bounding box of two opposite corners is exact. The four arms match epdiy's
// _rotate(): landscape px=lx, portrait px=ly.
EpdRect ui_rotate_rect_to_fb(EpdRect rect) {
    const int pw = epd_width();
    const int ph = epd_height();
    const int lx0 = rect.x;
    const int ly0 = rect.y;
    const int lx1 = rect.x + rect.width - 1;
    const int ly1 = rect.y + rect.height - 1;
    int ax, ay, bx, by;

    switch (epd_get_rotation()) {
        case EPD_ROT_LANDSCAPE:
            ax = lx0; ay = ly0; bx = lx1; by = ly1;
            break;
        case EPD_ROT_PORTRAIT:
            ax = pw - ly0 - 1; ay = lx0; bx = pw - ly1 - 1; by = lx1;
            break;
        case EPD_ROT_INVERTED_LANDSCAPE:
            ax = pw - lx0 - 1; ay = ph - ly0 - 1;
            bx = pw - lx1 - 1; by = ph - ly1 - 1;
            break;
        default:  // 倒置竖屏。/ EPD_ROT_INVERTED_PORTRAIT
            ax = ly0; ay = ph - lx0 - 1; bx = ly1; by = ph - lx1 - 1;
            break;
    }

    const int x0 = ax < bx ? ax : bx;
    const int x1 = ax < bx ? bx : ax;
    const int y0 = ay < by ? ay : by;
    const int y1 = ay < by ? by : ay;
    return (EpdRect){
        .x = x0, .y = y0,
        .width = x1 - x0 + 1, .height = y1 - y0 + 1,
    };
}

// 不能图省事用 epd_fill_rect：它会退化成逐像素 read-modify-write，而竖屏下逻辑
// 横线在内存里每像素要跨一个物理行（epd_width()/2 字节），几乎必然 cache miss，
// 清一小块反而比整屏 memset 还慢。
// Do not use epd_fill_rect here: it falls back to per-pixel RMW. In portrait a
// logical horizontal line strides a physical row (epd_width()/2 bytes) per
// pixel and misses cache; a small clear can be slower than a full-screen memset.
void ui_clear_rect_fast(uint8_t* framebuffer, EpdRect rect) {
    EpdRect fb = ui_rotate_rect_to_fb(rect);
    if (fb.x < 0) { fb.width += fb.x; fb.x = 0; }
    if (fb.y < 0) { fb.height += fb.y; fb.y = 0; }
    if (fb.x + fb.width > epd_width()) fb.width = epd_width() - fb.x;
    if (fb.y + fb.height > epd_height()) fb.height = epd_height() - fb.y;
    if (fb.width <= 0 || fb.height <= 0) return;

    // 一个字节两个像素：起点向下取偶、终点向上取偶，多清一列不影响观感
    // Two pixels per byte: start even, end even; an extra column does not show.
    const int stride = epd_width() / 2;
    const int byte_x = fb.x / 2;
    const size_t span = (size_t)((fb.x + fb.width + 1) / 2 - byte_x);
    for (int row = fb.y; row < fb.y + fb.height; row++) {
        memset(framebuffer + (size_t)row * stride + byte_x, 0xFF, span);
    }
}

#define UI_NO_FONT_CARD_H 140
#define UI_NO_FONT_CARD_GAP 16

static EpdRect s_no_font_remount_rect;
static EpdRect s_no_font_format_rect;

static EpdRect no_font_card_rect(int y) {
    return (EpdRect){
        .x = UI_MARGIN,
        .y = y,
        .width = ui_content_width(),
        .height = UI_NO_FONT_CARD_H,
    };
}

static void draw_bmp_card(
    uint8_t* framebuffer, EpdRect card, const ui_fallback_bmp_t* bmp
) {
    ui_draw_round_rect(framebuffer, card, UI_BTN_RADIUS, UI_GRAY_BLACK);
    int x = card.x + (card.width - bmp->width) / 2;
    int y = card.y + (card.height - bmp->height) / 2;
    ui_blit_bmp(framebuffer, x, y, bmp);
}

// 一个字都画不出来的时候只能用内嵌位图说话：提示插卡，再给重挂载和格式化两张卡片。
// When no glyph can be drawn, speak with embedded bitmaps: insert-card hint plus remount and format cards.
void ui_draw_no_font_page(
    uint8_t* framebuffer, bool card_present, bool format_confirm
) {
    ui_clear_page(framebuffer);

    int mx = (epd_rotated_display_width() - ui_fb_insert.width) / 2;
    if (mx < UI_MARGIN) mx = UI_MARGIN;
    ui_blit_bmp(framebuffer, mx, UI_CONTENT_TOP + 48, &ui_fb_insert);

    int y = UI_CONTENT_TOP + 48 + ui_fb_insert.height + 56;
    s_no_font_remount_rect = no_font_card_rect(y);
    draw_bmp_card(framebuffer, s_no_font_remount_rect, &ui_fb_remount);
    y += UI_NO_FONT_CARD_H + UI_NO_FONT_CARD_GAP;

    if (card_present) {
        s_no_font_format_rect = no_font_card_rect(y);
        draw_bmp_card(
            framebuffer, s_no_font_format_rect,
            format_confirm ? &ui_fb_confirm : &ui_fb_format
        );
    } else {
        s_no_font_format_rect = (EpdRect){ 0 };
    }
}

int ui_no_font_hit_test(uint16_t x, uint16_t y) {
    if (ui_rect_hit(s_no_font_remount_rect, x, y)) return UI_NO_FONT_HIT_REMOUNT;
    if (ui_rect_hit(s_no_font_format_rect, x, y)) return UI_NO_FONT_HIT_FORMAT;
    return UI_NO_FONT_HIT_NONE;
}

// 开机图 / 锁屏图：整屏 4bpp 位图，尺寸必须和 framebuffer 一致。
// Boot / lock image: full-screen 4bpp bitmap; size must match the framebuffer.
void ui_draw_full_image(uint8_t* framebuffer, const uint8_t* image) {
    ui_clear_page(framebuffer);
    if (image == NULL) return;
    EpdRect area = {
        .x = 0,
        .y = 0,
        .width = epd_rotated_display_width(),
        .height = epd_rotated_display_height(),
    };
    epd_draw_rotated_image(area, image, framebuffer);
}

#define UI_SAMPLE_TEXT "滚滚长江东逝水"

const ui_sample_line_t ui_sample_lines[] = {
    { 72, UI_SAMPLE_TEXT },
    { 48, UI_SAMPLE_TEXT },
    { 36, UI_SAMPLE_TEXT },
    { 28, UI_SAMPLE_TEXT },
    { 24, UI_SAMPLE_TEXT },
    { 20, UI_SAMPLE_TEXT },
    { 18, UI_SAMPLE_TEXT },
    { 16, UI_SAMPLE_TEXT },
    { 14, UI_SAMPLE_TEXT },
    { 12, UI_SAMPLE_TEXT },
    { 10, UI_SAMPLE_TEXT },
};

int ui_sample_line_count(void) {
    return (int)(sizeof(ui_sample_lines) / sizeof(ui_sample_lines[0]));
}
