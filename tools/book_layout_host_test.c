/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：用伪字体验证布局完整性、偏移和测宽复杂度。
 * English: Verify layout completeness, offsets and measurement complexity using a fake font.
 * 冻结：只用于宿主测试。/ Frozen: Host testing only.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "book_layout.h"
#include "ttf_font.h"
int test_heap_fail_after = -1;
size_t test_heap_largest;
static bool test_zero_width;
static char drawn[20000];
static bool capture_lines;
static char captured[128][512];
static unsigned captured_count;
static size_t measured_codepoints;
static size_t measure_calls;
static int first_draw_px, last_draw_px, first_draw_x, last_draw_x;
static int first_draw_y, last_draw_y, captured_y[128];
static int last_tracking_px;
static int fitted_target;
static int test_cjk_advance;
static int test_opener_bearing;
static int image_probe_count;
static int rotated_draws;
static int last_rotated_x, last_rotated_y;
static int vertical_draws, first_vertical_x, last_vertical_x, first_vertical_y, last_vertical_y;
// PR9 尺寸由调用方提供；测试用图片表模拟真实解码回调。/ PR9 asks its caller for dimensions; simulate the decoded image table.
static bool fixture_image(void *ctx, int image, int *width, int *height) {
    const html_text_t *chapter = ctx;
    for (size_t i=0; i<chapter->count; ++i) if (chapter->blocks[i].image == image) {
        *width=chapter->blocks[i].image_width; *height=chapter->blocks[i].image_height;
        return *width>0 && *height>0;
    }
    return false;
}
static bool probe_image(void *ctx, int image, int *width, int *height) {
    assert(ctx == &image_probe_count && image >= 0);
    ++image_probe_count;
    *width = 80; *height = 20;
    return true;
}
int test_guide_segments;
int test_guide_first_x;
int test_guide_first_y;
int test_guide_second_y;
int test_guide_last_y;
int test_guide_height;
uint8_t test_guide_gray;
// 宿主假字体：一个可配置就绪位的槽表 + 一个可配置的当前槽，用来验证按 run 换字体。
// Host stand-in font: a slot table with configurable readiness plus a current slot, enough to
// check that measuring follows the runs.
static unsigned test_slot_ready_mask = 1u;
static int test_slot;
// 每个槽每字额外加宽，用来把“量宽到底走了哪个槽”变成看得见的折行差异。
// Extra per-glyph width per slot, so which slot measured a glyph shows up as a wrap change.
static int test_slot_advance[TTF_FONT_SLOTS];
void ttf_draw_set_runs(const ttf_run_t* runs, size_t count) {
    if (runs != NULL && count > 0) ttf_font_select(runs[0].slot);
}
int ttf_font_select(int slot) {
    int previous = test_slot;
    if (slot >= 0 && slot < TTF_FONT_SLOTS) test_slot = slot;
    return previous;
}
int ttf_font_selected(void) { return test_slot; }
bool ttf_font_slot_ready(int slot) {
    return slot >= 0 && slot < TTF_FONT_SLOTS && (test_slot_ready_mask & (1u << slot)) != 0;
}
int ttf_text_width_px(int px, const char* text) {
    int n = 0, width = 0;
    for (; *text; text++) if (((unsigned char)*text & 0xc0) != 0x80) {
        n++;
        width += test_zero_width&&*text=='~'?0:(unsigned char)*text >= 0x80 && test_cjk_advance ? test_cjk_advance :
                 *text == 'i' ? px / 2 : *text == 'W' ? px + px / 2 : px;
    }
    measured_codepoints += (size_t)n;
    measure_calls++;
    return width + n * test_slot_advance[test_slot];
}
int ttf_ascender_px(int px) { return px; }
int ttf_text_left_bearing_px(int px, const char* text) {
    (void)px; (void)text; return test_opener_bearing;
}
void ttf_draw_text_px(uint8_t* fb, int x, int y, int px, const char* text,
                      enum EpdFontFlags align, uint8_t fg, uint8_t bg) {
    (void)fb; (void)align;
    assert(fg <= 15 && bg <= 15);
    if (!drawn[0]) { first_draw_px = px; first_draw_x = x; first_draw_y = y; }
    last_draw_px = px;
    last_draw_x = x;
    last_draw_y = y;
    assert(strlen(drawn) + strlen(text) < sizeof(drawn));
    strcat(drawn, text);
    if(capture_lines){assert(captured_count<128&&strlen(text)<512);captured_y[captured_count]=y;strcpy(captured[captured_count++],text);}
}
void ttf_draw_text_px_spaced(uint8_t* fb, int x, int y, int px, const char* text,
                             int tracking_px, uint8_t fg, uint8_t bg) {
    last_tracking_px = tracking_px;
    ttf_draw_text_px(fb, x, y, px, text, EPD_DRAW_ALIGN_LEFT, fg, bg);
}
void ttf_draw_text_px_fitted(uint8_t* fb, int x, int y, int px, const char* text,
                             int tracking_px, int target_width, uint8_t fg, uint8_t bg) {
    fitted_target = target_width;
    ttf_draw_text_px_spaced(fb, x, y, px, text, tracking_px, fg, bg);
}
// 竖排的旋转绘制：宿主替身只记录调用与落点，用来验证拉丁文走了旋转路径。
// Rotated drawing for vertical setting: the host stand-in only records the call and where it
// landed, enough to check that Latin takes the rotated path.
int ttf_draw_text_px_rotated(uint8_t* fb, int x, int y, int px, const char* text,
                             uint8_t fg, uint8_t bg) {
    (void)fb; (void)fg; (void)bg;
    ++rotated_draws;
    last_rotated_x = x;
    last_rotated_y = y;
    if (!drawn[0]) { first_draw_px = px; first_draw_x = x; first_draw_y = y; }
    last_draw_px = px;
    last_draw_x = x;
    last_draw_y = y;
    assert(strlen(drawn) + strlen(text) < sizeof(drawn));
    strcat(drawn, text);
    return ttf_text_width_px(px, text);
}
// 竖排整列绘制：排版把一整列交给字体引擎，替身只记录起点与内容。
// Whole-column vertical drawing: layout hands a column to the font engine, so the stand-in only
// records where it started and what it carried.
int ttf_draw_text_px_vertical(uint8_t* fb, int x, int y, int px, const char* text,
                              int tracking_px, uint8_t fg, uint8_t bg) {
    (void)fb; (void)fg; (void)bg;
    ++vertical_draws;
    if (vertical_draws == 1) { first_vertical_x = x; first_vertical_y = y; }
    last_vertical_x = x;
    last_vertical_y = y;
    last_tracking_px = tracking_px;
    if (!drawn[0]) { first_draw_px = px; first_draw_x = x; first_draw_y = y; }
    last_draw_px = px;
    last_draw_x = x;
    last_draw_y = y;
    assert(strlen(drawn) + strlen(text) < sizeof(drawn));
    strcat(drawn, text);
    return ttf_text_width_px(px, text);
}
int ttf_vertical_text_height_px(int px, const char* text, int tracking_px) {
    int total = ttf_text_width_px(px, text);
    if (tracking_px > 0) {
        int glyphs = 0;
        for (const char* c = text; *c; ++c) if (((unsigned char)*c & 0xc0) != 0x80) ++glyphs;
        if (glyphs > 1) total += tracking_px * (glyphs - 1);
    }
    return total;
}
int main(void) {
    EpdRect r = {0, 0, 20, 30};
    const char text[] = "甲乙丙丁戊己庚辛壬癸";
    assert(book_layout_build(text, strlen(text), r, 10));
    assert(book_layout_page_count() == 3);
    assert(book_layout_page_start_offset(1) == 12);
    assert(book_layout_page_for_offset(11) == 0);
    assert(book_layout_page_for_offset(12) == 1);
    assert(book_layout_page_for_offset(999) == 2);
    uint8_t fb = 0;
    for (size_t i = 0; i < book_layout_page_count(); i++) {
        assert(book_layout_page_start_offset(i) % 3 == 0);
        book_layout_draw_page(&fb, i, r, 10);
    }
    assert(strcmp(drawn, text) == 0);
    const char mixed[] = "Wi甲iiW";
    assert(book_layout_build(mixed, strlen(mixed), r, 10));
    assert(book_layout_page_count() == 2);
    assert(book_layout_page_start_offset(1) == 7);
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    book_layout_draw_page(&fb, 1, r, 10);
    assert(strcmp(drawn, mixed) == 0);
    assert(book_layout_build("", 0, r, 10));
    assert(book_layout_page_count() == 1);
    assert(book_layout_build(NULL, 0, r, 10));
    assert(!book_layout_build(NULL, 1, r, 10));
    assert(book_layout_page_count() == 0);
    assert(!book_layout_build("\xe7\x94", 2, r, 10));
    assert(!book_layout_build("x\0y", 3, r, 10));
    assert(!book_layout_build("x", 1, (EpdRect){0,0,5,30}, 10));
    assert(!book_layout_build("x", 1, (EpdRect){0,0,20,5}, 10));
    char* many = malloc(4097);
    memset(many, 'x', 4097);
    r = (EpdRect){0,0,10,15};
    assert(book_layout_build(many, 4096, r, 10));
    assert(book_layout_page_count() == 4096);
    assert(!book_layout_build(many, 4097, r, 10));
    assert(book_layout_page_count() == 0);
    free(many);
    assert(book_layout_build("a\r\n\r\nb", 6, (EpdRect){0,0,20,100}, 10));
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, (EpdRect){0,0,20,100}, 10);
    assert(strcmp(drawn, "ab") == 0);
    char long_line[1024];
    memset(long_line, 'z', sizeof(long_line));
    r = (EpdRect){0,0,10000,30};
    measured_codepoints = measure_calls = 0;
    assert(book_layout_build(long_line, sizeof(long_line), r, 10));
    printf("long-line build: %zu calls, %zu measured codepoints\n", measure_calls, measured_codepoints);
    fflush(stdout);
    assert(measured_codepoints <= sizeof(long_line) * 2);
    assert(book_layout_page_count() == 1);
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 11);
    assert(drawn[0] == 0);
    book_layout_draw_page(&fb, 99, r, 10);
    assert(drawn[0] == 0);
    measured_codepoints = measure_calls = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    assert(measured_codepoints <= sizeof(long_line) * 2);
    assert(strlen(drawn) == sizeof(long_line));
    assert(memcmp(drawn, long_line, sizeof(long_line)) == 0);
    assert(book_layout_page_start_offset(99) == sizeof(long_line));
    assert(!book_layout_build("\xed\xa0\x80", 3, r, 10));
    assert(!book_layout_build("\xf4\x90\x80\x80", 4, r, 10));
    assert(!book_layout_build("\xc0\xaf", 2, r, 10));
    book_layout_free();
    assert(book_layout_page_count() == 0);
    const char styled[] = "Title\nbody";
    blk_t blocks[] = {{.offset = 0, .len = 5, .heading = true, .image = -1},
                      {.offset = 6, .len = 4, .heading = false, .image = -1}};
    r = (EpdRect){0, 0, 100, 45};
    assert(book_layout_build_blocks(styled, strlen(styled), blocks, 2, r, 10));
    assert(book_layout_page_count() == 2);
    assert(book_layout_page_start_offset(1) == 6);
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    book_layout_draw_page(&fb, 1, r, 10);
    assert(!strcmp(drawn, "Titlebody") && first_draw_px == 18 && last_draw_px == 10);
    const char chapters[] = "One\nalpha\nTwo\nbeta";
    blk_t chapter_blocks[] = {
        {.offset = 0, .len = 3, .heading = true, .chapter_start = true, .image = -1},
        {.offset = 4, .len = 5, .image = -1},
        {.offset = 10, .len = 3, .heading = true, .chapter_start = true, .image = -1},
        {.offset = 14, .len = 4, .image = -1},
    };
    EpdRect chapter_rect = {0, 0, 300, 120};
    assert(book_layout_build_blocks(chapters, strlen(chapters), chapter_blocks, 4, chapter_rect, 10));
    // PR9 不包含后来追加的块级 chapter_start 强制分页；这里验证其原始流式行为。
    // PR9 excludes the later block-level forced chapter break; verify its original flow.
    assert(book_layout_page_count() == 1 && book_layout_page_start_offset(0) == 0);
    assert(book_layout_page_for_offset(10) == 0 && book_layout_page_for_offset(14) == 0);
    drawn[0] = 0; book_layout_draw_page(&fb, 0, chapter_rect, 10);
    assert(!strcmp(drawn, "OnealphaTwobeta"));
    book_layout_set_chapter_lead(4, 30);
    assert(book_layout_build_blocks(chapters, strlen(chapters), chapter_blocks, 4, chapter_rect, 10));
    assert(book_layout_page_count() == 1 && book_layout_page_start_offset(0) == 4);
    book_layout_set_chapter_lead(0, 0);
    const char illustrated[] = "IMG\nAA\nIMG\nBB";
    blk_t illustrated_blocks[] = {
        {.offset = 0, .len = 3, .image = 0},
        {.offset = 4, .len = 2, .image = -1},
        {.offset = 7, .len = 3, .image = 1},
        {.offset = 11, .len = 2, .image = -1},
    };
    EpdRect illustrated_rect = {0, 0, 100, 60};
    assert(book_layout_build_blocks(illustrated, strlen(illustrated), illustrated_blocks, 4,
                                    illustrated_rect, 10));
    assert(book_layout_page_count() == 4);
    assert(book_layout_page_image(0) == 0 && book_layout_page_image(2) == 1);
    book_layout_set_images_visible(false);
    assert(book_layout_build_blocks(illustrated, strlen(illustrated), illustrated_blocks, 4,
                                    illustrated_rect, 10));
    assert(book_layout_page_count() == 1 && book_layout_page_start_offset(0) == 4);
    assert(book_layout_page_image(0) == -1);
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, illustrated_rect, 10);
    assert(!strcmp(drawn, "AABB"));
    assert(book_layout_build_blocks("IMG", 3, illustrated_blocks, 1, illustrated_rect, 10));
    assert(book_layout_page_count() == 1 && book_layout_page_image(0) == 0);
    book_layout_set_images_visible(true);
    // 真实 HTML 的换行、分页空白、全角及零宽占位不应在插图前后产生额外页面。
    // Real HTML line/page-break whitespace and fullwidth/zero-width spacers must not create image-adjacent pages.
    const char *image_html = "<p>甲</p><p>&#10;&#12;&nbsp;</p><p>　</p>"
        "<br/><img src='image.png'/><br/><p>　&#x200b;&#xfeff;</p><p id='after'>乙<br/>丙</p>";
    html_text_t parsed = {0};size_t image_anchor = 0;
    assert(html_to_blocks_with_css_anchor(image_html, strlen(image_html), NULL, 0,
                                          "after", &image_anchor, &parsed) == ESP_OK);
    assert(parsed.count == 6 && parsed.image_count == 1);
    size_t after_image = parsed.blocks[4].offset;
    assert(image_anchor == after_image);
    assert(book_layout_build_blocks(parsed.utf8, parsed.len, parsed.blocks, parsed.count,
                                    illustrated_rect, 10));
    assert(book_layout_page_count() == 3 && book_layout_page_image(1) == -1);
    assert(book_layout_page_start_offset(2) == after_image && book_layout_page_for_offset(image_anchor) == 2);
    drawn[0] = 0;
    for(size_t p=0;p<3;p++)book_layout_draw_page(&fb,p,illustrated_rect,10);
    assert(!strcmp(drawn,"甲乙丙"));
    book_layout_set_images_visible(false);
    assert(book_layout_build_blocks(parsed.utf8, parsed.len, parsed.blocks, parsed.count,
                                    illustrated_rect, 10));
    assert(book_layout_page_count() == 1 && book_layout_page_image(0) < 0);
    drawn[0]=0;book_layout_draw_page(&fb,0,illustrated_rect,10);assert(!strcmp(drawn,"甲乙丙"));
    book_layout_free();html_text_free(&parsed);
    const char *only_image = "<p>　&#x200b;</p><img src='image.png'/><p>　</p>";
    assert(html_to_blocks(only_image,strlen(only_image),&parsed)==ESP_OK);
    assert(book_layout_build_blocks(parsed.utf8,parsed.len,parsed.blocks,parsed.count,illustrated_rect,10));
    assert(book_layout_page_count()==1&&book_layout_page_image(0)==-1);
    book_layout_set_images_visible(true);
    assert(book_layout_build_blocks(parsed.utf8,parsed.len,parsed.blocks,parsed.count,illustrated_rect,10));
    assert(book_layout_page_count()==1&&book_layout_page_image(0)==-1);
    book_layout_free();html_text_free(&parsed);
    // 通栏：屏宽大于正文栏时插图按整屏缩放且不放大；关闭后仍退回正文栏。
    // Full bleed: on a panel wider than the column an illustration scales to the panel and is
    // never upscaled; turning it off falls back to the body column.
    html_text_t wide = {0};
    blk_t wide_block = {.offset = 0, .len = 3, .image = 0, .image_width = 240, .image_height = 100};
    wide.blocks = &wide_block; wide.count = 1;
    book_layout_set_image_dims(fixture_image, &wide);
    EpdRect column = {50, 0, 100, 200};
    int wide_image = -1, wide_y = -1, wide_w = 0, wide_h = 0;
    book_layout_set_image_bleed_width(0);
    assert(book_layout_build_blocks("IMG", 3, &wide_block, 1, column, 10));
    assert(book_layout_image_rect().x == 50 && book_layout_image_rect().width == 100);
    assert(book_layout_page_image_at(0, 0, &wide_image, &wide_y, &wide_w, &wide_h) && wide_image == 0);
    assert(wide_w == 100 && wide_h == 41);
    book_layout_set_image_bleed_width(200);
    assert(book_layout_build_blocks("IMG", 3, &wide_block, 1, column, 10));
    assert(book_layout_page_count() == 1);
    assert(book_layout_image_rect().x == 0 && book_layout_image_rect().width == 200);
    assert(book_layout_page_image_at(0, 0, &wide_image, &wide_y, &wide_w, &wide_h));
    assert(wide_w == 200 && wide_h == 83);
    book_layout_set_image_dims(probe_image, &image_probe_count);
    assert(book_layout_build_blocks("IMG", 3, &wide_block, 1, column, 10));
    assert(book_layout_page_image_at(0, 0, &wide_image, &wide_y, &wide_w, &wide_h));
    assert(wide_w == 80 && wide_h == 20);
    image_probe_count = 0;
    book_layout_set_image_bleed_width(0);
    book_layout_free();
    const char *text_spacer="<p>甲</p><p>　</p><p>乙</p>";
    assert(html_to_blocks(text_spacer,strlen(text_spacer),&parsed)==ESP_OK);
    assert(book_layout_build_blocks(parsed.utf8,parsed.len,parsed.blocks,parsed.count,(EpdRect){0,0,100,20},10));
    assert(book_layout_page_count()==3);
    book_layout_free();html_text_free(&parsed);
    EpdRect balanced = book_layout_balanced_rect((EpdRect){36, 0, 612, 100}, 48, 0);
    assert(balanced.x == 54 && balanced.width == 576);
    assert(balanced.x == 684 - balanced.x - balanced.width);
    balanced = book_layout_balanced_rect((EpdRect){36, 0, 612, 100}, 48, 2);
    assert(balanced.x == 43 && balanced.width == 598);
    r = (EpdRect){0, 0, 100, 100};
    for (unsigned em = 0; em <= 3; ++em) {
        book_layout_set_first_line_indent(em);
        assert(book_layout_build("甲乙\n丙丁", strlen("甲乙\n丙丁"), r, 10));
        drawn[0] = 0;
        book_layout_draw_page(&fb, 0, r, 10);
        assert(first_draw_x == (int)em * 10);
        // 原文的段首全角/半角空格不得额外叠加一个缩进档位。
        // Source leading spaces must not add another visual indent stop.
        const char *preindented = "　甲乙\n  丙丁";
        assert(book_layout_build(preindented, strlen(preindented), r, 10));
        drawn[0] = 0;
        book_layout_draw_page(&fb, 0, r, 10);
        assert(first_draw_x == (int)em * 10);
        assert(!strcmp(drawn, "甲乙丙丁"));
    }
    // 模拟字号 48、真实字宽 33 的字体；开引号留白不得额外增加半格。
    // Simulate a 48-high font with 33-wide glyphs; an opener's bearing must not add half an indent cell.
    r = (EpdRect){36, 0, 612, 180};
    test_cjk_advance = 33;
    for (unsigned em = 0; em <= 3; ++em) {
        for (int tracking = -4; tracking <= 4; tracking += 2) {
            book_layout_set_first_line_indent(em);
            book_layout_set_typography(tracking);
            const char *cases[] = {"甲乙丙丁", "　甲乙丙丁", "\xef\xbb\xbf\xe2\x80\x8b甲乙丙丁",
                                    "　“甲乙丙丁”", "（甲乙丙丁）", "「甲乙丙丁」"};
            for (unsigned i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
                test_opener_bearing = i >= 3 ? 20 : 0;
                assert(book_layout_build(cases[i], strlen(cases[i]), r, 48));
                drawn[0] = 0;
                book_layout_draw_page(&fb, 0, r, 48);
                int shift = em ? test_opener_bearing : 0;
                assert(first_draw_x + shift == r.x + (int)em * (33 + tracking));
                assert(book_layout_page_start_offset(0) == 0);
            }
        }
    }
    book_layout_set_first_line_indent(2);book_layout_set_typography(0);
    test_cjk_advance=33;test_opener_bearing=12;
    for(int delta=-20;delta<=20;++delta){
        book_layout_set_first_line_indent_adjust(delta);
        const char *text="　“甲乙丙丁”";
        assert(book_layout_build(text,strlen(text),r,48));drawn[0]=0;book_layout_draw_page(&fb,0,r,48);
        assert(first_draw_x+12==r.x+66+delta&&book_layout_page_start_offset(0)==0);
    }
    book_layout_set_first_line_indent(0);book_layout_set_first_line_indent_adjust(20);
    assert(book_layout_build("甲乙",strlen("甲乙"),r,48));drawn[0]=0;book_layout_draw_page(&fb,0,r,48);assert(first_draw_x==r.x);
    book_layout_set_first_line_indent(1);test_cjk_advance=10;test_opener_bearing=0;book_layout_set_first_line_indent_adjust(-20);
    assert(book_layout_build("甲乙",strlen("甲乙"),r,10));drawn[0]=0;book_layout_draw_page(&fb,0,r,10);assert(first_draw_x==r.x);
    book_layout_set_first_line_indent_adjust(21); // Invalid values reset safely to zero.
    assert(book_layout_build("甲乙",strlen("甲乙"),r,10));drawn[0]=0;book_layout_draw_page(&fb,0,r,10);assert(first_draw_x==r.x+10);
    for(int align=0;align<=2;++align){
        blk_t isolated={.offset=0,.len=strlen("甲乙"),.heading=align==0,.align=(uint8_t)align,.image=-1};
        book_layout_set_first_line_indent(2);book_layout_set_first_line_indent_adjust(0);
        assert(book_layout_build_blocks("甲乙",isolated.len,&isolated,1,r,10));drawn[0]=0;book_layout_draw_page(&fb,0,r,10);int original_x=first_draw_x;
        book_layout_set_first_line_indent_adjust(20);
        assert(book_layout_build_blocks("甲乙",isolated.len,&isolated,1,r,10));drawn[0]=0;book_layout_draw_page(&fb,0,r,10);assert(first_draw_x==original_x);
    }
    book_layout_set_first_line_indent_adjust(0);
    test_cjk_advance = test_opener_bearing = 0;
    book_layout_set_typography(0);
    book_layout_set_first_line_indent(2);
    const char punct[] = "甲乙，丙";
    r = (EpdRect){0, 0, 20, 15};
    assert(book_layout_build(punct, strlen(punct), r, 10));
    assert(book_layout_page_count() == 3 && book_layout_page_start_offset(1) == 3);
    book_layout_set_first_line_indent(0);
    r = (EpdRect){0, 0, 40, 15};
    assert(book_layout_build("甲乙丙丁，戊", strlen("甲乙丙丁，戊"), r, 10));
    assert(book_layout_page_start_offset(1) == 9);
    book_layout_set_first_line_indent(2);
    const char opener[] = "甲（乙丙";
    r.width = 20;
    assert(book_layout_build(opener, strlen(opener), r, 10));
    assert(book_layout_page_count() == 3 && book_layout_page_start_offset(1) == 3);
    const char aligned[] = "甲乙";
    blk_t aligned_block = {.offset = 0, .len = strlen(aligned), .image = -1,
                           .align = 1, .indent_percent = 100};
    r = (EpdRect){10, 0, 100, 30};
    assert(book_layout_build_blocks(aligned, strlen(aligned), &aligned_block, 1, r, 10));
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    assert(first_draw_x == 50 && last_draw_x == 50);
    // 使用真实解析块验证两行题头的绘制坐标，而非仅检查 CSS 标记。
    // Verify both heading lines' drawing coordinates using parsed blocks, not just CSS metadata.
    const char split_html[] = "<h2 style='text-align:center;margin-bottom:1.8em'>"
                              "<span>第一章</span><br/>山边小村</h2>";
    html_text_t split_title = {0};
    assert(html_to_blocks(split_html, strlen(split_html), &split_title) == ESP_OK);
    r = (EpdRect){10, 0, 300, 84};
    assert(book_layout_build_blocks(split_title.utf8, split_title.len, split_title.blocks,
                                    split_title.count, r, 20));
    assert(book_layout_page_count() == 1);
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 20);
    assert(!strcmp(drawn, "第一章山边小村"));
    assert(first_draw_px == 28 && last_draw_px == 28);
    assert(first_draw_x == 118 && last_draw_x == 104);
    assert(first_draw_y == 28 && last_draw_y == 70);
    book_layout_free();
    html_text_free(&split_title);
    // 真正分段仍保留段后距；段内 br 仅占一行，两者不能混为一谈。
    // Paragraph boundaries retain their gaps; an in-paragraph br advances just one line.
    const char paragraph_html[] = "<p style='margin-bottom:1em'>甲<br/>乙</p><p>丙</p>";
    assert(html_to_blocks(paragraph_html, strlen(paragraph_html), &split_title) == ESP_OK);
    r = (EpdRect){10, 0, 300, 200};
    assert(book_layout_build_blocks(split_title.utf8, split_title.len, split_title.blocks,
                                    split_title.count, r, 20));
    drawn[0] = 0; capture_lines = true; captured_count = 0;
    book_layout_draw_page(&fb, 0, r, 20);
    capture_lines = false;
    assert(captured_count == 3 && captured_y[0] == 20 && captured_y[1] == 50 && captured_y[2] == 115);
    book_layout_free(); html_text_free(&split_title);
    const char separate_html[] = "<h2 style='margin-bottom:1.8em'>第一章</h2><h2>山边小村</h2>";
    assert(html_to_blocks(separate_html, strlen(separate_html), &split_title) == ESP_OK);
    assert(book_layout_build_blocks(split_title.utf8, split_title.len, split_title.blocks,
                                    split_title.count, r, 20));
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 20);
    assert(last_draw_y - first_draw_y == 106);
    book_layout_free(); html_text_free(&split_title);
    r = (EpdRect){0, 0, 100, 45};
    book_layout_set_chapter_lead(6, 20);
    assert(book_layout_build_blocks(styled, strlen(styled), blocks, 2, r, 10));
    assert(book_layout_page_count() == 1);
    assert(book_layout_page_start_offset(0) == 6);
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    assert(!strcmp(drawn, "body"));
    book_layout_set_chapter_lead(0, 0);
    r.width = 20;
    assert(book_layout_build_blocks(styled, strlen(styled), blocks, 2, r, 10));
    assert(book_layout_page_count() == 6);
    assert(book_layout_page_start_offset(5) == 6);
    drawn[0] = 0;
    for (size_t i = 0; i < book_layout_page_count(); ++i) book_layout_draw_page(&fb, i, r, 10);
    assert(!strcmp(drawn, "Titlebody"));
    assert(!book_layout_build_blocks(styled, strlen(styled), blocks, 2, (EpdRect){0,0,100,20}, 10));
    blocks[1].offset = 5;
    assert(!book_layout_build_blocks(styled, strlen(styled), blocks, 2, r, 10));
    assert(book_layout_page_count() == 0);
    blocks[1].offset = 6;
    blocks[1].len = SIZE_MAX;
    assert(!book_layout_build_blocks(styled, strlen(styled), blocks, 2, r, 10));
    assert(!book_layout_build_blocks(styled, strlen(styled), NULL, 2, r, 10));
    blk_t split_utf8[] = {{.offset = 0, .len = 1, .heading = true, .image = -1},
                          {.offset = 2, .len = 2, .heading = false, .image = -1}};
    assert(!book_layout_build_blocks("甲\nx", 5, split_utf8, 2, r, 10));
    assert(book_layout_build("body", 4, r, 10));
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    assert(first_draw_px == 10);
    book_layout_set_typography(0);
    r = (EpdRect){0, 0, 35, 100};
    assert(book_layout_build("abcd", 4, r, 10));
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    assert(!strcmp(drawn, "abcd") && first_draw_x == 20 && last_draw_x == 0);
    book_layout_set_first_line_indent(0);
    book_layout_set_typography(2);
    r = (EpdRect){0, 0, 25, 15};
    assert(book_layout_build("abcd", 4, r, 10));
    assert(book_layout_page_count() == 2 && book_layout_page_start_offset(1) == 2);
    drawn[0] = 0; last_tracking_px = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    assert(!strcmp(drawn, "ab") && last_tracking_px == 2);
    book_layout_set_typography(-2);
    r.width = 28;
    assert(book_layout_build("abcd", 4, r, 10));
    assert(book_layout_page_count() == 2 && book_layout_page_start_offset(1) == 3);
    drawn[0] = 0; last_tracking_px = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    assert(!strcmp(drawn, "abc") && last_tracking_px == -2);
    book_layout_set_typography(0);
    book_layout_set_first_line_indent(2);
    r = (EpdRect){10, 20, 400, 120};
    assert(book_layout_build("甲乙丙丁", strlen("甲乙丙丁"), r, 40));
    book_layout_set_reading_line(0);
    test_guide_segments = 0;
    book_layout_draw_page(&fb, 0, r, 40);
    assert(test_guide_segments == 0);
    book_layout_set_reading_line(1);
    book_layout_draw_page(&fb, 0, r, 40);
    int dashed_segments = test_guide_segments;
    assert(dashed_segments > 0);
    assert(test_guide_first_y == 70 && test_guide_height == 2 && test_guide_gray == 0x50);
    book_layout_set_reading_line_offset(-8);
    test_guide_segments = 0;
    book_layout_draw_page(&fb, 0, r, 40);
    assert(test_guide_first_y == 62);
    book_layout_set_reading_line_offset(8);
    test_guide_segments = 0;
    book_layout_draw_page(&fb, 0, r, 40);
    assert(test_guide_first_y == 78);
    book_layout_set_reading_line_offset(0);
    book_layout_set_reading_line(2);
    test_guide_segments = 0;
    book_layout_draw_page(&fb, 0, r, 40);
    assert(test_guide_segments > dashed_segments);
    assert(test_guide_first_y == 70 && test_guide_height == 2 && test_guide_gray == 0x50);
    book_layout_set_spacing(110, 50);
    assert(book_layout_build("甲乙丙丁", strlen("甲乙丙丁"), r, 40));
    test_guide_segments = 0;
    book_layout_draw_page(&fb, 0, r, 40);
    assert(test_guide_first_y == 62);
    book_layout_set_spacing(200, 50);
    assert(book_layout_build("甲乙丙丁", strlen("甲乙丙丁"), r, 40));
    test_guide_segments = 0;
    book_layout_draw_page(&fb, 0, r, 40);
    assert(test_guide_first_y == 80);
    book_layout_set_spacing(150, 50);
    book_layout_set_reading_line(0);
    // 竖排的阅读线走在列的右侧，不再压着字：把整页逆时针转 90° 来读这一列，横排的「字下」正是这里。
    // 原先画在列中线上（字格左缘 + 半个字宽），等于穿字而过。
    // In vertical setting the guide runs down the column's right side instead of through the glyphs:
    // turning the page 90° counter-clockwise puts the horizontal "under the glyphs" here. It used to
    // sit on the column's centre line, half a glyph width into the cell.
    book_layout_set_vertical(true);
    book_layout_set_reading_line(1);
    book_layout_set_first_line_indent(2);
    r = (EpdRect){10, 20, 400, 120};
    assert(book_layout_build("甲乙丙丁", strlen("甲乙丙丁"), r, 40));
    test_guide_segments = 0; test_guide_first_x = -1; vertical_draws = 0;
    book_layout_draw_page(&fb, 0, r, 40);
    assert(test_guide_segments > 1 && vertical_draws > 0);
    assert(test_guide_first_x >= first_vertical_x + 40 && test_guide_height > 2);
    // 与横排对称：虚线从整页上边起算、走整页高度，所以第一段落在页顶，段距正好一个周期，
    // 与列首缩进无关——原先每列从自己的字首起算，缩进一变相位就变，看着疏密不一。
    // Symmetric with the horizontal rule: the dashes count from the page's top edge and span its
    // height, so the first segment sits at the page top and the next is exactly one period later,
    // whatever the column's indent. Counting from each column's own start shifted the phase with it.
    assert(test_guide_first_y == r.y && test_guide_second_y - test_guide_first_y == 31);
    assert(test_guide_last_y < r.y + r.height);
    book_layout_set_first_line_indent(0);
    // 上下位置滑杆在竖排里横着移动阅读线；调到最里也必须留在字格右边，不许压字。
    // The offset slider moves the guide sideways in vertical setting; even at its innermost it must
    // stay right of the glyph cell instead of biting into the text.
    book_layout_set_reading_line_offset(-8);
    test_guide_segments = 0; test_guide_first_x = -1;
    book_layout_draw_page(&fb, 0, r, 40);
    assert(test_guide_segments > 0 && test_guide_first_x >= first_vertical_x + 41);
    book_layout_set_reading_line_offset(0);
    book_layout_set_reading_line(0);
    book_layout_set_vertical(false);
    // 右标点可在安全留白内悬挂，下一页必须仍从正文开始。/ Hang a closer safely without starting the next page with punctuation.
    book_layout_set_first_line_indent(0);
    r = (EpdRect){40, 0, 40, 15};
    const char hanging[] = "甲乙丙丁，戊己庚辛";
    assert(book_layout_build(hanging, strlen(hanging), r, 10));
    assert(book_layout_page_start_offset(1) == strlen("甲乙丙丁，"));
    drawn[0] = 0; fitted_target = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    assert(!strcmp(drawn, "甲乙丙丁，") && fitted_target == 43);
    // 居中块的首行不应带入普通段落缩进。/ A centered block must not inherit first-line body indent.
    blk_t centered = {.offset = 0, .len = strlen("甲乙丙丁戊己"), .align = 1, .indent_percent = 200, .image = -1};
    r = (EpdRect){40, 0, 30, 15};
    assert(book_layout_build_blocks("甲乙丙丁戊己", centered.len, &centered, 1, r, 10));
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, r, 10);
    int first_center_x = first_draw_x;
    drawn[0] = 0;
    book_layout_draw_page(&fb, 1, r, 10);
    assert(first_center_x == first_draw_x);
    const char *punct_cases[]={"甲乙丙丁：“戊己”庚辛", "甲乙丙丁……戊己庚辛", "甲乙丙丁——戊己庚辛", "甲乙丙丁”，戊己庚辛", "甲乙丙丁：‘戊己’庚辛"};
    book_layout_set_first_line_indent(0);
    for(unsigned c=0;c<5;++c)for(int w=20;w<=80;w+=10)for(int tracking=-4;tracking<=4;tracking+=2){
        EpdRect box={40,0,w,30};
        book_layout_set_typography(tracking);capture_lines=true;captured_count=0;drawn[0]=0;vertical_draws=0;
        assert(book_layout_build(punct_cases[c],strlen(punct_cases[c]),box,10));
        for(size_t page=0;page<book_layout_page_count();++page)book_layout_draw_page(&fb,page,box,10);
        assert(!strcmp(drawn,punct_cases[c]));
        capture_lines=false;
    }
    html_text_t composed={0};
    const char *html="<p>甲乙丙丁</p><img src='small.png'/><p>戊己庚辛</p>";
    assert(html_to_blocks(html,strlen(html),&composed)==ESP_OK);
    for(size_t i=0;i<composed.count;++i)if(composed.blocks[i].image>=0){composed.blocks[i].image_width=60;composed.blocks[i].image_height=15;}
    EpdRect mixed_box={40,20,100,100};
    book_layout_set_image_dims(fixture_image,&composed);
    book_layout_set_typography(0);book_layout_set_spacing(150,25);book_layout_set_chapter_lead(0,0);
    assert(book_layout_build_blocks(composed.utf8,composed.len,composed.blocks,composed.count,mixed_box,10));
    assert(book_layout_page_count()==1&&book_layout_page_image(0)==-1&&book_layout_page_image_count(0)==1);
    int image_y,image_w,image_h;
    assert(book_layout_page_image_at(0,0,NULL,&image_y,&image_w,&image_h));
    assert(image_w==60&&image_h==15&&image_y>0&&image_y+image_h<mixed_box.height);
    drawn[0]=0;book_layout_draw_page(&fb,0,mixed_box,10);assert(!strcmp(drawn,"甲乙丙丁戊己庚辛"));
    book_layout_set_images_visible(false);
    assert(book_layout_build_blocks(composed.utf8,composed.len,composed.blocks,composed.count,mixed_box,10));
    assert(book_layout_page_count()==1&&book_layout_page_image(0)==-1&&!book_layout_page_image_count(0));
    book_layout_set_images_visible(true);html_text_free(&composed);

    // 按 run 换字体：槽 2 的每个字更宽。量宽没跟着 run 走的话，三个字仍然挤在一行里。
    // Per-run faces: slot 2 is wider per glyph. If measuring ignored the runs, all three
    // glyphs would still fit on one line.
    {
        const char* runs_text = "甲甲乙";
        blk_t run_block = {.offset = 0, .len = 9, .image = -1, .run_first = 0, .run_count = 2};
        html_run_t run_spans[] = {{0, 3, 1}, {3, 6, 2}};
        EpdRect run_box = {40, 0, 80, 60};
        const int old_cjk = test_cjk_advance;
        test_cjk_advance = 10;
        test_slot_ready_mask = 0x7u;
        // 槽 2 单字 75px：一行 80px 只放得下一个，槽 1 的 10px 三个都放得下。
        // Slot 2 is 75 px per glyph: only one fits an 80 px line, whereas all three 10 px
        // glyphs of slot 1 fit easily.
        test_slot_advance[2] = 65;
        book_layout_set_spacing(150, 0);
        book_layout_set_chapter_lead(0, 0);
        book_layout_set_first_line_indent(0);
        book_layout_set_typography(0);
        book_layout_set_runs(NULL, 0);
        capture_lines = true; captured_count = 0;
        assert(book_layout_build_blocks(runs_text, 9, &run_block, 1, run_box, 10));
        for (size_t page = 0; page < book_layout_page_count(); ++page)
            book_layout_draw_page(&fb, page, run_box, 10);
        assert(captured_count == 1 && !strcmp(captured[0], "甲甲乙"));
        captured_count = 0;
        book_layout_set_runs(run_spans, 2);
        assert(book_layout_build_blocks(runs_text, 9, &run_block, 1, run_box, 10));
        for (size_t page = 0; page < book_layout_page_count(); ++page)
            book_layout_draw_page(&fb, page, run_box, 10);
        assert(captured_count == 3 && !strcmp(captured[0], "甲") &&
               !strcmp(captured[1], "甲") && !strcmp(captured[2], "乙"));
        capture_lines = false;
        book_layout_set_runs(NULL, 0);
        test_slot_ready_mask = 1u;
        test_slot_advance[2] = 0;
        test_cjk_advance = old_cjk;
    }

    // 多图同页、比例缩小与未知尺寸回退，正文前后不遗漏。
    // Cover multiple images, aspect-fit and unknown-size fallback without losing adjacent prose.
    const char *multi = "<p>甲</p><img src='a.png'/><img src='b.png'/><p>乙</p>";
    assert(html_to_blocks(multi, strlen(multi), &composed) == ESP_OK);
    for (size_t i=0;i<composed.count;++i) if (composed.blocks[i].image>=0) {
        composed.blocks[i].image_width=60; composed.blocks[i].image_height=15;
    }
    uint32_t before_generation=book_layout_generation();
    assert(book_layout_build_blocks(composed.utf8,composed.len,composed.blocks,composed.count,mixed_box,10));
    assert(book_layout_generation()!=before_generation);
    assert(book_layout_page_count()==1 && book_layout_page_image_count(0)==2 && book_layout_page_image(0)==-1);
    int index,y,w,h,first_y;
    assert(book_layout_page_image_at(0,0,&index,&first_y,&w,&h)&&index==0&&w==60&&h==15);
    assert(book_layout_page_image_at(0,1,&index,&y,&w,&h)&&index==1&&y==first_y+20);
    assert(!book_layout_page_image_at(0,-1,NULL,NULL,NULL,NULL));
    assert(!book_layout_page_image_at(0,2,NULL,NULL,NULL,NULL)&&book_layout_page_image_count(99)==0);
    drawn[0]=0;book_layout_draw_page(&fb,0,mixed_box,10);assert(!strcmp(drawn,"甲乙"));
    // 剩余高度不足时整张图移到下一页。/ Move a whole image when remaining height is insufficient.
    EpdRect short_box={40,20,100,45};
    assert(book_layout_build_blocks(composed.utf8,composed.len,composed.blocks,composed.count,short_box,10));
    assert(book_layout_page_count()==2&&book_layout_page_image_count(0)==1&&book_layout_page_image_count(1)==1);
    assert(book_layout_page_start_offset(1)==composed.blocks[2].offset);
    assert(book_layout_page_image_at(1,0,&index,&y,&w,&h)&&index==1&&y==0);
    drawn[0]=0;for(size_t i=0;i<2;++i)book_layout_draw_page(&fb,i,short_box,10);assert(!strcmp(drawn,"甲乙"));
    book_layout_set_images_visible(false);
    assert(book_layout_build_blocks(composed.utf8,composed.len,composed.blocks,composed.count,mixed_box,10));
    assert(book_layout_page_count()==1&&book_layout_page_image_count(0)==0);
    drawn[0]=0;book_layout_draw_page(&fb,0,mixed_box,10);assert(!strcmp(drawn,"甲乙"));
    book_layout_set_images_visible(true);
    composed.blocks[3].chapter_start=true;
    assert(book_layout_build_blocks(composed.utf8,composed.len,composed.blocks,composed.count,mixed_box,10));
    assert(book_layout_page_count()==1);
    html_text_free(&composed);
    const char *large="<img src='big.png'/>";
    assert(html_to_blocks(large,strlen(large),&composed)==ESP_OK);
    composed.blocks[0].image_width=1000;composed.blocks[0].image_height=2000;
    assert(book_layout_build_blocks(composed.utf8,composed.len,composed.blocks,composed.count,mixed_box,10));
    assert(book_layout_page_image_at(0,0,&index,&y,&w,&h)&&w==50&&h==100&&y==0);
    assert(book_layout_page_image(0)==0);
    composed.blocks[0].image_width=composed.blocks[0].image_height=0;
    assert(book_layout_build_blocks(composed.utf8,composed.len,composed.blocks,composed.count,mixed_box,10));
    assert(book_layout_page_image_count(0)==0&&book_layout_page_image(0)==0);
    book_layout_set_image_dims(probe_image,&image_probe_count);
    assert(book_layout_build_blocks(composed.utf8,composed.len,composed.blocks,composed.count,mixed_box,10));
    assert(image_probe_count==1&&book_layout_page_image_at(0,0,&index,&y,&w,&h)&&w==80&&h==20);
    book_layout_set_image_dims(fixture_image,&composed);html_text_free(&composed);
    char many_images[2000]="<p>甲</p>";
    for(int i=0;i<40;++i)strcat(many_images,"<img src='small.png'/>");
    strcat(many_images,"<p>乙</p>");
    assert(html_to_blocks(many_images,strlen(many_images),&composed)==ESP_OK);
    for(size_t i=0;i<composed.count;++i)if(composed.blocks[i].image>=0){composed.blocks[i].image_width=10;composed.blocks[i].image_height=1;}
    EpdRect many_box={0,0,100,400};
    assert(book_layout_build_blocks(composed.utf8,composed.len,composed.blocks,composed.count,many_box,10));
    assert(book_layout_page_count()==1&&book_layout_page_image_count(0)==40);
    assert(book_layout_page_image_at(0,39,&index,&y,&w,&h)&&index==39);
    // 每一处分配失败后布局清空，可以安全重建。/ Allocation failures clear ownership and permit a clean rebuild.
    for(int fail=0;fail<6;++fail){
        test_heap_fail_after=fail;
        assert(!book_layout_build_blocks(composed.utf8,composed.len,composed.blocks,composed.count,many_box,10));
        assert(!book_layout_page_count()&&!book_layout_page_image_count(0));
    }
    test_heap_fail_after=-1;
    assert(book_layout_build_blocks(composed.utf8,composed.len,composed.blocks,composed.count,many_box,10));
    drawn[0]=0;book_layout_draw_page(&fb,0,many_box,10);assert(!strcmp(drawn,"甲乙"));
    html_text_free(&composed);
    // 竖排：列自上而下、列序自右向左；句号抬到字格右上角、拉丁转 90°；插图独占一页。
    // Vertical setting: columns run downward ordered right to left; the period lifts to the
    // top-right of its cell, Latin turns 90°, and an illustration owns its page.
    book_layout_set_first_line_indent(0);
    book_layout_set_first_line_indent_adjust(0);
    book_layout_set_spacing(150, 50);
    book_layout_set_typography(0);
    book_layout_set_reading_line(0);
    book_layout_set_images_visible(true);
    book_layout_set_chapter_lead(0, 0);
    const char *vertical_text = "甲乙丙丁戊己庚辛";
    EpdRect column_box = {0, 0, 100, 40};
    book_layout_set_vertical(true);
    assert(book_layout_build(vertical_text, strlen(vertical_text), column_box, 10));
    assert(book_layout_page_count() == 1);
    drawn[0] = 0;
    vertical_draws = 0;
    book_layout_draw_page(&fb, 0, column_box, 10);
    // 列高 40、字高 10 → 每列 4 字；列距 15 → 右起第一列字身框 x=87，第二列退到 72。
    // 一整列交给字体引擎一次，正立/抬角/旋转由引擎按竖排规则决定。
    // A 40 px column holds four 10 px glyphs; at a 15 px pitch the first em box sits at x=87 and
    // the second column steps back to 72. Each column goes to the font engine in one call, which
    // decides per glyph whether it stands upright, lifts into the corner or turns 90°.
    assert(!strcmp(drawn, "甲乙丙丁戊己庚辛"));
    assert(vertical_draws == 2);
    assert(first_vertical_x == 87 && last_vertical_x == 72);
    assert(first_vertical_y == 0 && last_vertical_y == 0);
    const char *mark = "甲。";
    assert(book_layout_build(mark, strlen(mark), column_box, 10));
    drawn[0] = 0;
    vertical_draws = 0;
    book_layout_draw_page(&fb, 0, column_box, 10);
    assert(!strcmp(drawn, "甲。") && vertical_draws == 1);
    const char *latin = "AB";
    assert(book_layout_build(latin, strlen(latin), column_box, 10));
    drawn[0] = 0;
    vertical_draws = 0;
    rotated_draws = 0;
    book_layout_draw_page(&fb, 0, column_box, 10);
    // 拉丁整串走同一列调用，引擎内部逐字转 90°；排版这一层不再逐字分派。
    // The Latin run takes the same single column call and the engine turns each glyph 90°
    // inside it; layout no longer dispatches glyph by glyph.
    assert(!strcmp(drawn, "AB") && vertical_draws == 1 && !rotated_draws);
    assert(first_vertical_x == 87);
    // 段首缩进在竖排里是列顶留白。
    // The paragraph indent is blank space at the top of the column.
    book_layout_set_first_line_indent(1);
    assert(book_layout_build("甲乙", strlen("甲乙"), column_box, 10));
    drawn[0] = 0;
    vertical_draws = 0;
    book_layout_draw_page(&fb, 0, column_box, 10);
    assert(first_vertical_x == 87 && first_vertical_y == 10);
    book_layout_set_first_line_indent(0);
    // 竖排题头在首页右起预留列宽，正文列跟着让开同样的宽度。
    // A vertical heading reserves column width at the right of the first page, and body columns
    // step aside by the same width.
    book_layout_set_chapter_lead_width(30);
    assert(book_layout_build(vertical_text, strlen(vertical_text), column_box, 10));
    drawn[0] = 0;
    vertical_draws = 0;
    book_layout_draw_page(&fb, 0, column_box, 10);
    assert(first_vertical_x == 57);
    assert(!strcmp(drawn, "甲乙丙丁戊己庚辛"));
    book_layout_set_chapter_lead_width(0);
    // 一列一页：8 字、每列 2 字 → 4 页，页首偏移按列推进。
    // One column per page: eight glyphs at two per column make four pages, advancing by column.
    EpdRect one_column = {0, 0, 15, 20};
    assert(book_layout_build(vertical_text, strlen(vertical_text), one_column, 10));
    assert(book_layout_page_count() == 4);
    assert(book_layout_page_start_offset(1) == 6 && book_layout_page_start_offset(3) == 18);
    // 插图独占一页，正文从下一页的右边缘重新开始。
    // An illustration owns its page and the body restarts at the right edge of the next one.
    const char *vertical_image = "IMG\n甲乙";
    blk_t vertical_blocks[] = {
        {.offset = 0, .len = 3, .image = 0},
        {.offset = 4, .len = 6, .image = -1},
    };
    book_layout_set_image_dims(probe_image, &image_probe_count);
    image_probe_count = 0;
    assert(book_layout_build_blocks(vertical_image, strlen(vertical_image), vertical_blocks, 2,
                                    column_box, 10));
    assert(book_layout_page_count() == 2 && book_layout_page_image(0) == 0);
    assert(book_layout_page_image_count(0) == 1 && book_layout_page_start_offset(1) == 4);
    book_layout_set_image_dims(fixture_image, &composed);
    // 禁则：右标点不得停在列底，也不得独自落到下一列首，整列回退一个字带它走。
    // Kinsoku: a closing mark may not sit at the bottom of a column or start the next one alone,
    // so the column rolls back a glyph and takes it along.
    const char *kinsoku = "甲乙。丙";
    EpdRect narrow_column = {0, 0, 15, 25};
    assert(book_layout_build(kinsoku, strlen(kinsoku), narrow_column, 10));
    assert(book_layout_page_count() == 3);
    assert(book_layout_page_start_offset(1) == 3 && book_layout_page_start_offset(2) == 9);
    drawn[0] = 0;
    for (size_t p = 0; p < book_layout_page_count(); ++p)
        book_layout_draw_page(&fb, p, narrow_column, 10);
    assert(!strcmp(drawn, "甲乙。丙"));
    // 拉丁串整串走同一列调用，逐字沿列推进。
    // A Latin run takes one column call and advances glyph by glyph down it.
    const char *latin_run = "ABC";
    assert(book_layout_build(latin_run, strlen(latin_run), column_box, 10));
    drawn[0] = 0;
    vertical_draws = 0;
    book_layout_draw_page(&fb, 0, column_box, 10);
    assert(vertical_draws == 1 && !strcmp(drawn, "ABC"));
    // 章节首页题头在竖排里是列顶留白，正文从它下面开始。
    // The chapter heading is blank space at the top of the first column; the body starts below.
    book_layout_set_chapter_lead(0, 20);
    assert(book_layout_build("甲乙", strlen("甲乙"), column_box, 10));
    drawn[0] = 0;
    vertical_draws = 0;
    book_layout_draw_page(&fb, 0, column_box, 10);
    assert(first_vertical_y == 20);
    // 首页有题头时插图让到第二页，不压在题头上。
    // With a heading on the first page the illustration moves to the second one, clear of it.
    book_layout_set_image_dims(probe_image, &image_probe_count);
    image_probe_count = 0;
    assert(book_layout_build_blocks(vertical_image, strlen(vertical_image), vertical_blocks, 2,
                                    column_box, 10));
    assert(book_layout_page_count() == 3 && book_layout_page_image(0) == -1);
    assert(book_layout_page_image(1) == 0 && book_layout_page_start_offset(2) == 4);
    book_layout_set_chapter_lead(0, 0);
    book_layout_set_image_dims(fixture_image, &composed);
    // 关掉竖排回到横排：同一段文字从左边缘排起。
    // Turning it off returns to horizontal setting, where the same text starts at the left edge.
    book_layout_set_vertical(false);
    assert(book_layout_build(vertical_text, strlen(vertical_text), column_box, 10));
    drawn[0] = 0;
    book_layout_draw_page(&fb, 0, column_box, 10);
    assert(first_draw_x == 0);
    // 分行/分列预绘与一次绘制保持文字和位置一致；重排使旧游标失效。
    // Idle slices match complete drawing text/coordinates; reflow invalidates an older cursor.
    book_layout_set_chapter_lead(0,0);book_layout_set_runs(NULL,0);
    const char *idle_text="甲乙丙丁天地玄黄宇宙洪荒。春夏秋冬日月盈昃。\n天地人和甲乙丙丁。";
    for(int vertical=0;vertical<2;++vertical){
        book_layout_set_vertical(vertical);
        EpdRect box={3,7,100,100};
        assert(book_layout_build(idle_text,strlen(idle_text),box,10));
        for(size_t page=0;page<book_layout_page_count();++page){
            capture_lines=true;captured_count=0;drawn[0]=0;
            book_layout_draw_page(&fb,page,box,10);
            char expected[sizeof(drawn)];strcpy(expected,drawn);
            int vx0=first_vertical_x,vy0=first_vertical_y,vx1=last_vertical_x,vy1=last_vertical_y;
            int expected_y[128];memcpy(expected_y,captured_y,sizeof(expected_y));unsigned lines=captured_count;
            captured_count=0;drawn[0]=0;vertical_draws=0;book_layout_draw_cursor_t cursor={0};int result,steps=0;
            do{unsigned before=captured_count;result=book_layout_draw_slice(&fb,page,box,10,&cursor);
               assert(result>=0&&captured_count<=before+1&&++steps<100);}while(!result);
            assert(!strcmp(expected,drawn)&&captured_count==lines);
            if(vertical&&vertical_draws)assert(first_vertical_x==vx0&&first_vertical_y==vy0&&last_vertical_x==vx1&&last_vertical_y==vy1);
            assert(!memcmp(expected_y,captured_y,lines*sizeof(int)));
            assert(book_layout_build(idle_text,strlen(idle_text),box,10));
            assert(book_layout_draw_slice(&fb,page,box,10,&cursor)==-1);
        }
    }
    capture_lines=false;book_layout_set_vertical(false);
    // 大章节的行缓冲不应跟整章等大；沿用同样的分页结果与限制。
    // A large chapter's scratch line must not reserve a whole chapter; pagination rules stay intact.
    char *large_chapter=malloc(240001);assert(large_chapter);memset(large_chapter,'a',240000);large_chapter[240000]=0;
    test_heap_largest=0;
    assert(book_layout_build(large_chapter,240000,(EpdRect){0,0,600,1000},10));
    assert(book_layout_page_count()>1&&test_heap_largest<16384);book_layout_free();free(large_chapter);
    char zero_width[6001];memset(zero_width,'~',6000);zero_width[6000]=0;test_zero_width=true;test_heap_largest=0;
    assert(book_layout_build(zero_width,6000,(EpdRect){0,0,600,1000},10));
    assert(test_heap_largest<=6001);drawn[0]=0;book_layout_draw_page(&fb,0,(EpdRect){0,0,600,1000},10);
    assert(!strcmp(drawn,zero_width));book_layout_free();test_heap_fail_after=3;
    assert(!book_layout_build(zero_width,6000,(EpdRect){0,0,600,1000},10));
    test_heap_fail_after=-1;test_zero_width=false;book_layout_free();
    puts("book_layout_host_test: PASS; idle rows/columns equal full rendering, stale cursor rejected and large chapter supported");
}
