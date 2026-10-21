/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：输入框共用绘制与命中区域；按实际系统字体测量，光标只随编辑刷新。
 * English: Shared field painting and hit geometry; measure the actual system font and refresh carets only on edits.
 */
#include "ui_text_input.h"
#include <ui_kit.h>
#include <string.h>

#define INPUT_TEXT_BYTES 128
#define INPUT_ARROW_WIDTH 56
#define INPUT_PAD 17
#define INPUT_CARET_WIDTH 3

typedef struct { size_t start, end, cursor; int caret; } input_view_t;

static int input_px(int px, EpdRect line) {
    px = ui_text_effective_px(px);
    if (px > line.height - 12) px = line.height - 12;
    return px > 0 ? px : 1;
}

static void input_slice(const char *text, size_t start, size_t end, bool masked,
                        char out[INPUT_TEXT_BYTES + 1]) {
    size_t used = 0;
    for (size_t at = start; at < end;) {
        size_t next = ui_text_edit_next(text, at);
        size_t bytes = masked ? 1 : next - at;
        if (used + bytes > INPUT_TEXT_BYTES) break;
        if (masked) out[used] = '*';
        else memcpy(out + used, text + at, bytes);
        used += bytes;
        at = next;
    }
    out[used] = 0;
}

static int input_width(const char *text, size_t start, size_t end, int px, bool masked) {
    char slice[INPUT_TEXT_BYTES + 1];
    input_slice(text, start, end, masked, slice);
    return ui_text_fixed_context_width_px(px, slice, masked ? "*" : text);
}

static input_view_t input_view(const char *text, size_t cursor, EpdRect line, int px, bool masked) {
    input_view_t view = {.cursor = cursor};
    size_t length = strlen(text);
    if (view.cursor > length) view.cursor = length;
    while (view.cursor && ((unsigned char)text[view.cursor] & 0xc0) == 0x80) --view.cursor;
    int available = line.width - INPUT_CARET_WIDTH;
    if (available < 0) available = 0;
    // 常见短输入直接测整串与光标前缀，省去逐个前缀反复测量。
    // Short input needs only whole-text and caret-prefix measurements, avoiding repeated prefix scans.
    if (length <= INPUT_TEXT_BYTES && input_width(text, 0, length, px, masked) <= available) {
        view.end = length;
        view.caret = input_width(text, 0, view.cursor, px, masked);
        return view;
    }
    while (view.start < view.cursor && input_width(text, view.start, view.cursor, px, masked) > available)
        view.start = ui_text_edit_next(text, view.start);
    view.end = view.start;
    while (text[view.end]) {
        size_t next = ui_text_edit_next(text, view.end);
        if (next - view.start > INPUT_TEXT_BYTES || input_width(text, view.start, next, px, masked) > available) break;
        view.end = next;
    }
    view.caret = input_width(text, view.start, view.cursor, px, masked);
    return view;
}

static EpdRect input_line(EpdRect field, const char *suffix, int *suffix_px, int *suffix_width) {
    *suffix_px = input_px(22, field);
    *suffix_width = suffix ? ui_text_fixed_width_px(*suffix_px, suffix) : 0;
    if (*suffix_width > field.width / 3) *suffix_width = field.width / 3;
    return (EpdRect){field.x + INPUT_PAD, field.y + 10,
        field.width - 2 * INPUT_PAD - 2 * INPUT_ARROW_WIDTH - *suffix_width - (*suffix_width ? 12 : 0),
        field.height - 20};
}

static void paint_value(uint8_t *fb, const char *text, size_t cursor, EpdRect line, int px, bool masked) {
    if (line.width < INPUT_CARET_WIDTH || line.height <= 0) return;
    input_view_t view = input_view(text, cursor, line, px, masked);
    char shown[INPUT_TEXT_BYTES + 1];
    input_slice(text, view.start, view.end, masked, shown);
    ui_text_fixed_context_vc(fb, line.x, line.y + line.height / 2, px, shown,
                             masked ? "*" : text, EPD_DRAW_ALIGN_LEFT, false);
    int height = px + 10;
    if (height > line.height) height = line.height;
    epd_fill_rect((EpdRect){line.x + view.caret, line.y + (line.height - height) / 2,
                           INPUT_CARET_WIDTH, height}, UI_GRAY_BLACK, fb);
}

void ui_text_input_draw(uint8_t *fb, const ui_text_edit_t *edit, EpdRect field,
                        int px, bool masked, const char *suffix) {
    if (!edit || !edit->text) return;
    ui_draw_round_rect(fb, field, 10, UI_GRAY_BLACK);
    int suffix_px, suffix_width;
    EpdRect line = input_line(field, suffix, &suffix_px, &suffix_width);
    paint_value(fb, edit->text, edit->cursor, line, input_px(px, line), masked);
    if (suffix_width) {
        char shown[INPUT_TEXT_BYTES + 1];
        EpdRect suffix_line = {line.x + line.width + 12, line.y, suffix_width + INPUT_CARET_WIDTH, line.height};
        input_view_t view = input_view(suffix, 0, suffix_line, suffix_px, false);
        input_slice(suffix, 0, view.end, false, shown);
        ui_text_fixed_context_vc(fb, suffix_line.x, field.y + field.height / 2, suffix_px, shown,
                                 suffix, EPD_DRAW_ALIGN_LEFT, false);
    }
    int divider = field.x + field.width - 2 * INPUT_ARROW_WIDTH;
    epd_fill_rect((EpdRect){divider, field.y + 12, 1, field.height - 24}, 0x70, fb);
    for (int i = 0; i < 2; ++i) {
        int center = divider + INPUT_ARROW_WIDTH * i + INPUT_ARROW_WIDTH / 2;
        int tip = center + (i ? 5 : -5), tail = center + (i ? -5 : 5);
        for (int stroke = 0; stroke < 2; ++stroke) {
            epd_draw_line(tail + stroke, field.y + field.height / 2 - 10, tip + stroke,
                          field.y + field.height / 2, UI_GRAY_BLACK, fb);
            epd_draw_line(tip + stroke, field.y + field.height / 2, tail + stroke,
                          field.y + field.height / 2 + 10, UI_GRAY_BLACK, fb);
        }
    }
}

bool ui_text_input_tap(ui_text_edit_t *edit, EpdRect field, int px, bool masked,
                       const char *suffix, int x, int y) {
    if (!edit || !edit->text || !ui_rect_hit(field, x, y)) return false;
    int divider = field.x + field.width - 2 * INPUT_ARROW_WIDTH;
    if (x >= divider) { ui_text_edit_move(edit, x < divider + INPUT_ARROW_WIDTH ? -1 : 1); return true; }
    int suffix_px, suffix_width;
    EpdRect line = input_line(field, suffix, &suffix_px, &suffix_width);
    px = input_px(px, line);
    input_view_t view = input_view(edit->text, edit->cursor, line, px, masked);
    size_t best = view.start;
    int nearest = field.width + (x > line.x ? x - line.x : line.x - x);
    for (size_t at = view.start;; at = ui_text_edit_next(edit->text, at)) {
        int distance = x - line.x - input_width(edit->text, view.start, at, px, masked);
        if (distance < 0) distance = -distance;
        if (distance < nearest) { nearest = distance; best = at; }
        if (at >= view.end) break;
    }
    ui_text_edit_place(edit, best);
    return true;
}

void ui_text_input_composition(uint8_t *fb, const char *text, EpdRect line, int px) {
    if (!text) return;
    paint_value(fb, text, strlen(text), line, input_px(px, line), false);
}
