/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "book_cover_auto.h"
#include "epdiy.h"

static int glyph_count(const char *text) {
    int count = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
        if ((*p & 0xc0) != 0x80) ++count;
    return count;
}
bool ui_font_has_text(const char *text) { (void)text; return true; }
int ui_font_ascender_px(int px) { return px; }
int ui_font_text_width_px(int px, const char *text) { return glyph_count(text) * px; }
bool ttf_font_ready(void) { return true; }
int ttf_ascender_px(int px) { return px; }
int ttf_text_width_px(int px, const char *text) { return ui_font_text_width_px(px, text); }
static void glyph_rect(uint8_t *fb, int x, int baseline, int px, const char *text,
                       enum EpdFontFlags align, uint8_t fg) {
    int width = ui_font_text_width_px(px, text);
    if (align == EPD_DRAW_ALIGN_CENTER) x -= width / 2;
    else if (align == EPD_DRAW_ALIGN_RIGHT) x -= width;
    for (int g = 0; g < glyph_count(text); ++g)
        for (int y = baseline - px; y < baseline - 2; ++y)
            for (int dx = 0; dx < px - 3; ++dx) {
                int xx = x + g * px + dx;
                if (xx < 0 || xx >= epd_rotated_display_width() ||
                    y < 0 || y >= epd_rotated_display_height()) continue;
                int physical_x = y, physical_y = epd_height() - xx - 1;
                uint8_t *at = fb + (size_t)physical_y * epd_width() / 2 + physical_x / 2;
                if (physical_x & 1) *at = (*at & 0x0f) | (fg << 4);
                else *at = (*at & 0xf0) | fg;
            }
}
void ui_font_draw_text_px(uint8_t *fb, int x, int baseline, int px, const char *text,
                          enum EpdFontFlags align, uint8_t fg, uint8_t bg, bool black_white) {
    (void)bg; (void)black_white;
    glyph_rect(fb, x, baseline, px, text, align, fg);
}
void ttf_draw_text_px(uint8_t *fb, int x, int baseline, int px, const char *text,
                      enum EpdFontFlags align, uint8_t fg, uint8_t bg) {
    (void)bg;
    glyph_rect(fb, x, baseline, px, text, align, fg);
}

enum { W = 176, H = 240, N = W * H };
static void render_checked(const char *identity, const char *title, const char *author,
                           uint8_t out[N]) {
    uint8_t *guard = malloc(N + 32);
    assert(guard);
    memset(guard, 0xa5, N + 32);
    assert(book_auto_cover_render(identity, title, author, W, H, guard + 16));
    for (int i = 0; i < 16; ++i) assert(guard[i] == 0xa5 && guard[N + 16 + i] == 0xa5);
    memcpy(out, guard + 16, N);
    free(guard);
}
int main(void) {
    uint8_t *a = malloc(N), *b = malloc(N);
    assert(a && b);
    bool found[3] = {0};
    char path[80];
    for (int i = 0; i < 100; ++i) {
        snprintf(path, sizeof(path), "/sdcard/books/book-%d.txt", i);
        found[book_auto_cover_template(path, "夏天、烟火和我的尸体", "作者")] = true;
    }
    assert(found[0] && found[1] && found[2]);
    bool checked_split_cover = false;
    for (int i = 0; i < 100 && !checked_split_cover; ++i) {
        snprintf(path, sizeof(path), "/sdcard/books/book-%d.txt", i);
        if (book_auto_cover_template(path, "时间的褶皱", "作者") != 2) continue;
        render_checked(path, "时间的褶皱", "作者", b);
        for (int y = 120; y < H; ++y)
            for (int x = 0; x < W; ++x)
                assert(b[y * W + x] >= 238); /* Lower panel is intentionally text-free. */
        assert(b[20 * W + 4] < 160);
        checked_split_cover = true;
    }
    assert(checked_split_cover);
    assert(book_auto_cover_template("/sdcard/books/long.epub", "书名", "作者") ==
           book_auto_cover_template("/sdcard/books/long.epub", "A renamed book", "Another author"));
    render_checked("/sdcard/books/long.epub", "夏天、烟火和我的尸体：一个很长很长的中文书名", "三天两觉", a);
    render_checked("/sdcard/books/long.epub", "夏天、烟火和我的尸体：一个很长很长的中文书名", "三天两觉", b);
    assert(!memcmp(a, b, N));
    render_checked("/sdcard/books/english.txt", "The Extremely Long English Title That Must Wrap Cleanly Without Escaping Its Frame", "An Author With a Very Long Name", b);
    assert(memcmp(a, b, N));
    render_checked("/sdcard/books/empty-author.txt", "没有作者的书", "", b);
    assert(!book_auto_cover_render("x", "", "", W, H, b));
    assert(!book_auto_cover_render("x", "title", "", 8, H, b));
    free(a); free(b);
    puts("automatic cover templates: OK");
    return 0;
}
