/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：验证真实 UTF-8 编辑及输入框光标、滚动、密码遮蔽与字号边界。
 * English: Exercise actual UTF-8 editing, field carets, scrolling, password masking and font bounds.
 */
#include "ui_text_input.h"
#include "ui_kit.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int scale = 100, caret_count;
static EpdRect caret;
static char painted[129];
static int painted_x, painted_px;
int ui_text_effective_px(int px) { return px * scale / 100; }
int ui_text_fixed_width_px(int px, const char *text) {
    int width = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
        if ((*p & 0xc0) != 0x80) width += *p >= 128 ? px : *p == 'i' ? px / 4 : px / 2;
    return width;
}
int ui_text_fixed_context_width_px(int px, const char *text, const char *sample) {
    assert(sample);
    return ui_text_fixed_width_px(px, text);
}
void ui_text_fixed_vc(uint8_t *fb, int x, int y, int px, const char *text, enum EpdFontFlags align, bool inverted) {
    (void)fb; (void)y; (void)align; (void)inverted;
    snprintf(painted, sizeof(painted), "%s", text); painted_x = x; painted_px = px;
}
void ui_text_fixed_context_vc(uint8_t *fb, int x, int y, int px, const char *text,
                              const char *sample, enum EpdFontFlags align, bool inverted) {
    assert(sample);
    ui_text_fixed_vc(fb, x, y, px, text, align, inverted);
}
void ui_draw_round_rect(uint8_t *fb, EpdRect rect, int radius, uint8_t gray) {
    (void)fb; (void)rect; (void)radius; (void)gray;
}
bool ui_rect_hit(EpdRect r, uint16_t x, uint16_t y) { return x >= r.x && y >= r.y && x < r.x + r.width && y < r.y + r.height; }
void epd_fill_rect(EpdRect r, uint8_t gray, uint8_t *fb) {
    (void)fb;
    if (r.width == 3 && gray == UI_GRAY_BLACK) { caret = r; ++caret_count; }
}
void epd_draw_line(int x0, int y0, int x1, int y1, uint8_t gray, uint8_t *fb) {
    (void)x0; (void)y0; (void)x1; (void)y1; (void)gray; (void)fb;
}

static void editing(void) {
    char text[20] = "";
    ui_text_edit_t edit;
    ui_text_edit_init(&edit, text, sizeof(text));
    assert(edit.cursor == 0 && !ui_text_edit_backspace(&edit));
    assert(ui_text_edit_insert(&edit, "你好😀ab"));
    assert(edit.cursor == strlen(text));
    ui_text_edit_place(&edit, 4); assert(edit.cursor == 3);
    assert(ui_text_edit_insert(&edit, "中")); assert(!strcmp(text, "你中好😀ab"));
    assert(ui_text_edit_backspace(&edit)); assert(!strcmp(text, "你好😀ab") && edit.cursor == 3);
    ui_text_edit_move(&edit, 1); assert(edit.cursor == 6);
    ui_text_edit_move(&edit, 1); assert(edit.cursor == 10);
    assert(ui_text_edit_backspace(&edit)); assert(!strcmp(text, "你好ab") && edit.cursor == 6);
    ui_text_edit_place(&edit, 1000); assert(edit.cursor == strlen(text));
    ui_text_edit_move(&edit, 1); assert(edit.cursor == strlen(text));
    ui_text_edit_place(&edit, 0); ui_text_edit_move(&edit, -1); assert(!edit.cursor);
    char before[20]; strcpy(before, text);
    assert(!ui_text_edit_insert(&edit, "十二三四五六七八九")); assert(!strcmp(text, before) && !edit.cursor);
    char full[8] = "你好a"; ui_text_edit_init(&edit, full, sizeof(full));
    assert(!ui_text_edit_insert(&edit, "b") && edit.cursor == 7);
    assert(ui_text_edit_backspace(&edit) && ui_text_edit_insert(&edit, "b") && !strcmp(full, "你好b"));
    for (int i = 0; i < 1000; ++i) {
        ui_text_edit_place(&edit, (size_t)i % sizeof(full));
        ui_text_edit_insert(&edit, i % 2 ? "😀" : "中");
        ui_text_edit_move(&edit, i % 3 ? 1 : -1);
        ui_text_edit_backspace(&edit);
        assert(edit.cursor <= strlen(full) && strlen(full) < sizeof(full));
        assert(!full[edit.cursor] || ((unsigned char)full[edit.cursor] & 0xc0) != 0x80);
    }
}

static void fields(void) {
    uint8_t fb = 0;
    EpdRect field = {36, 242, 612, 82};
    char text[121] = "";
    ui_text_edit_t edit;
    ui_text_edit_init(&edit, text, sizeof(text));
    ui_text_input_draw(&fb, &edit, field, 29, false, NULL);
    assert(caret_count == 1 && caret.x == 53 && caret.width == 3 && caret.height >= 29);
    strcpy(text, "你好世界"); ui_text_edit_init(&edit, text, sizeof(text));
    assert(ui_text_input_tap(&edit, field, 29, false, NULL, 53 + 29, 283) && edit.cursor == 3);
    assert(ui_text_edit_insert(&edit, "A") && !strcmp(text, "你A好世界"));
    assert(ui_text_input_tap(&edit, field, 29, false, NULL, 550, 283) && edit.cursor == 3);
    assert(ui_text_input_tap(&edit, field, 29, false, NULL, 610, 283) && edit.cursor == 4);
    assert(!ui_text_input_tap(&edit, field, 29, false, NULL, 53, 240));
    memset(text, 'W', 120); text[120] = 0; ui_text_edit_init(&edit, text, sizeof(text));
    ui_text_edit_t saved = edit;
    for (scale = 100; scale <= 200; scale += 25) {
        ui_text_input_draw(&fb, &edit, field, 29, false, NULL);
        assert(caret.x >= 53 && caret.x + 3 <= 519 && caret.y >= 252 && caret.y + caret.height <= 314);
        assert(painted_x + ui_text_fixed_width_px(painted_px, painted) <= 519);
        assert(!memcmp(&saved, &edit, sizeof(edit)));
        assert(ui_text_input_tap(&edit, field, 29, false, NULL, 53, 283));
        assert(edit.cursor < 120 && edit.cursor > 0);
        ui_text_edit_place(&edit, 120);
    }
    scale = 100;
    strcpy(text, "secretWiFi123"); ui_text_edit_init(&edit, text, sizeof(text));
    ui_text_input_draw(&fb, &edit, field, 29, true, NULL);
    assert(!strcmp(painted, "*************"));
    ui_text_input_tap(&edit, field, 29, true, NULL, 53 + 14 * 3, 283); assert(edit.cursor == 3);
    ui_text_input_draw(&fb, &edit, field, 29, false, NULL); assert(edit.cursor == 3);
    ui_text_input_draw(&fb, &edit, field, 29, true, NULL); assert(edit.cursor == 3);
    ui_text_edit_backspace(&edit); assert(!strcmp(text, "seretWiFi123"));
    int old_count = caret_count;
    ui_text_input_composition(&fb, "nihao", (EpdRect){36, 375, 612, 40}, 25);
    assert(caret_count == old_count + 1 && !strcmp(painted, "nihao"));
    strcpy(text, "很长很长的中文名称很长很长的中文名称很长很长的中文名称");
    ui_text_edit_init(&edit, text, sizeof(text)); scale = 200;
    ui_text_input_draw(&fb, &edit, field, 29, false, ".epub");
    assert(caret.x + 3 <= 397 && painted_x + ui_text_fixed_width_px(painted_px, painted) < 536);
    assert(ui_text_input_tap(&edit, field, 29, false, ".epub", 500, 283));
    assert(edit.cursor == strlen(text));
}

int main(void) { editing(); fields(); puts("text_input_host_test: PASS"); }
