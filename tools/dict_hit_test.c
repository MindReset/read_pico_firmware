/*
 * SPDX-License-Identifier: Apache-2.0
 * Round-trip check for the tap-to-lookup hit test: draw a page, remember where
 * each line was actually painted, tap the middle of a word and require the hit
 * test to return exactly that word.
 *
 * Host build (needs only a C compiler, no ESP-IDF):
 *   gcc -std=gnu11 -I main/book -I tools/book_layout_stubs -I tools/dict_hit_stubs \
 *       -o dict_hit_test tools/dict_hit_test.c main/book/book_layout.c
 *   ./dict_hit_test          # prints ROUNDTRIP_PASS on success
 */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "book_layout.h"
#include "ttf_font.h"

int test_heap_fail_after = -1;
int test_guide_segments;
int test_guide_first_x;   /* r95 rebase 补：竖排辅助线经 epd_fill_rect 桩写入，链接期需定义 */
int test_guide_first_y;
int test_guide_second_y;  /* r95 rebase 补：同上 */
int test_guide_last_y;    /* r95 rebase 补：同上 */
int test_guide_height;
uint8_t test_guide_gray;

// r93 起布局在分片绘制里按 run 切换字体槽；命中测试不关心槽，只要能记账。
// Since r93 the slice renderer switches font slots per run; hit testing ignores slots but must link.
static int test_slot;
size_t test_heap_largest;
void ttf_draw_set_runs(const ttf_run_t* runs, size_t count) {
    if (runs != NULL && count > 0) ttf_font_select(runs[0].slot);
}
int ttf_font_select(int slot) {
    int previous = test_slot;
    if (slot >= 0 && slot < TTF_FONT_SLOTS) test_slot = slot;
    return previous;
}
int ttf_font_selected(void) { return test_slot; }
bool ttf_font_slot_ready(int slot) { return slot >= 0 && slot < TTF_FONT_SLOTS; }
// 竖排分片路径 r93 才进链接图；命中测试只走横排，桩必须存在但不必记账。
// The vertical slice path only enters the link graph on r93; hit testing draws horizontally.
int ttf_draw_text_px_vertical(uint8_t* fb, int x, int y, int px, const char* text,
                              int tracking_px, uint8_t fg, uint8_t bg) {
    (void)fb; (void)x; (void)y; (void)px; (void)text; (void)tracking_px; (void)fg; (void)bg;
    return 0;
}

int ttf_text_left_bearing_px(int px, const char* text) { (void)px; (void)text; return 0; }
int ttf_ascender_px(int px) { return px; }
int ttf_text_width_px(int px, const char* text) {
    int width = 0;
    for (; *text; text++) if (((unsigned char)*text & 0xc0) != 0x80)
        width += *text == 'i' ? px / 2 : *text == 'W' ? px + px / 2 : px;
    return width;
}

/* ---- 记录每一次绘制：起点 x、基线 y、字号、字间距、是否绘制了的具体文本 ---- */
typedef struct { int x, y, px, tracking; const char* text; } draw_rec_t;
static draw_rec_t rec[256];
static size_t rec_count;

static void rec_reset(void) {
    for (size_t i = 0; i < rec_count; ++i) free((void*)rec[i].text);
    rec_count = 0;
}

static void record(int x, int y, int px, const char* text, int tracking) {
    assert(rec_count < sizeof(rec) / sizeof(rec[0]));
    rec[rec_count++] = (draw_rec_t){x, y, px, tracking, strdup(text)};
}
void ttf_draw_text_px(uint8_t* fb, int x, int y, int px, const char* text,
                      enum EpdFontFlags align, uint8_t fg, uint8_t bg) {
    (void)fb; (void)align; (void)fg; (void)bg;
    record(x, y, px, text, 0);
}
void ttf_draw_text_px_spaced(uint8_t* fb, int x, int y, int px, const char* text,
                             int tracking_px, uint8_t fg, uint8_t bg) {
    (void)fb; (void)fg; (void)bg;
    record(x, y, px, text, tracking_px);
}
void ttf_draw_text_px_fitted(uint8_t* fb, int x, int y, int px, const char* text,
                             int tracking_px, int target_width, uint8_t fg, uint8_t bg) {
    (void)target_width; // 桩字体不做齐行分布；命中测试在纯拉丁行上也应得到相同结果
    ttf_draw_text_px_spaced(fb, x, y, px, text, tracking_px, fg, bg);
}

/* ---- 在一次绘制里取得某个单词的中心点击坐标（第 nth 个拉丁词） ---- */
static int word_extent(const char* line, int nth, int px, int* x0, int* width, const char** out) {
    int seen = -1, cx = 0;
    const char* p = line;
    while (*p) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')) {
            if (seen != nth) { ++seen; }
            if (seen == nth) {
                /* 排版按字宽换行，单词会被拆到两行；只取完整落在本行内的词作为样本。
                   Lines wrap by glyph width and may split words; only sample whole ones. */
                const char* start = p;
                int w = 0;
                while (*p && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))) {
                    char one[2] = {*p, 0};
                    w += ttf_text_width_px(px, one);
                    ++p;
                }
                /* 贴着行首/行尾的词可能被拆到邻行，跳过。
                   Words touching either edge may continue on the neighbouring line. */
                if (start == line || *p == 0) return 0;
                char prev = start[-1];
                if ((prev >= 'a' && prev <= 'z') || (prev >= 'A' && prev <= 'Z')) return 0;
                *x0 = cx; *width = w; *out = start;
                return (int)(p - start);
            }
            while (*p && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))) {
                char one[2] = {*p, 0};
                cx += ttf_text_width_px(px, one);
                if (*p) cx += 0;
                ++p;
            }
            continue;
        }
        char one[2] = {*p, 0};
        cx += ttf_text_width_px(px, one);
        ++p;
    }
    return 0;
}

static int failures, checks;

static void expect_word(size_t page, size_t rec_index, int nth, const char* source) {
    const draw_rec_t* r = &rec[rec_index];
    int x0 = 0, w = 0; const char* start = NULL;
    int len = word_extent(r->text, nth, r->px, &x0, &w, &start);
    if (len <= 0 || !start) return; // 该行不足 nth+1 个词，跳过
    assert(start);
    char expected[64];
    memcpy(expected, start, (size_t)len); expected[len] = 0;
    /* 行顶 = 基线 - ascender；往下取一点点，确保落在本行内 */
    int tap_x = r->x + x0 + w / 2;
    int tap_y = r->y - r->px + 1;
    size_t a = 0, b = 0;
    ++checks;
    if (!book_layout_hit_test(page, tap_x, tap_y, &a, &b)) {
        printf("  FAIL p%zu r%zu w%d: hit test returned nothing (wanted \"%s\")\n",
               page, rec_index, nth, expected);
        ++failures;
        return;
    }
    size_t n = b - a;
    if (n >= sizeof(expected)) n = sizeof(expected) - 1;
    char got[64];
    memcpy(got, source + a, n); got[n] = 0;
    if (strcmp(got, expected) != 0) {
        printf("  FAIL p%zu r%zu w%d: wanted \"%s\", got \"%s\"\n", page, rec_index, nth, expected, got);
        ++failures;
    }
}

/* 插图原始像素由用例改写：矮图走"图下留半行距"，高图走"整页只剩图"。
   Cases rewrite these: a short image exercises the half-line gap below it, a tall one the
   page-filling image that leaves no room for text. */
static int probe_w = 120, probe_h = 40;
/* 图下第一行必须恰好落在「图底 + 半个行高」上。
   rc85 的写法是 (height < rect.height ? px/2 : 0)，rc89 改成无条件 px/2；两种算法只在
   「图高恰好等于页高」时取值不同，所以本用例把两个分支各跑一遍：
     expect_line=true  矮图：间距必须真的加上，差一个像素都算回归；
     expect_line=false 高图：图高必须整好等于页高（即旧式为 0 的那一支），且该页不再有正文。
   The first line under an image must sit exactly at image-bottom + half a line. rc85 used
   (height < page ? px/2 : 0), rc89 uses px/2 unconditionally; the two differ only when the
   image height equals the page height, so this helper covers both branches:
     expect_line=true   short image: the gap must really be added, one pixel off is a regression;
     expect_line=false  tall image: image height must equal the page height (the old zero-gap
                        branch) and no text may follow on that page. */
static void expect_image_gap(size_t page, int px, int page_h, bool expect_line) {
    int index = -1, iy = 0, iw = 0, ih = 0;
    if (!book_layout_page_image_at(page, 0, &index, &iy, &iw, &ih)) return;
    ++checks;
    int bottom = iy + ih;
    int got = -1;
    for (size_t i = 0; i < rec_count; ++i) {
        int top = rec[i].y - ttf_ascender_px(rec[i].px);
        if (top >= bottom) { got = top; break; }
    }
    if (expect_line) {
        int want = bottom + px / 2;
        if (got != want) {
            printf("  FAIL p%zu: 图下第一行行顶应为 %d（图底 %d + 半行距 %d），实际 %d\n",
                   page, want, bottom, px / 2, got);
            ++failures;
        }
    } else {
        if (ih != page_h) {
            printf("  FAIL p%zu: 该用例要求图高恰好等于页高 %d，实际 %d\n", page, page_h, ih);
            ++failures;
        }
        if (got >= 0) {
            printf("  FAIL p%zu: 撑满整页的图后面不应再有正文行（行顶 %d）\n", page, got);
            ++failures;
        }
    }
}

static bool probe(void* ctx, int image, int* width, int* height) {
    (void)ctx; (void)image;
    *width = probe_w; *height = probe_h;
    return true;
}

static size_t build_doc(const char* const* parts, int* image_at, size_t n,
                        char* out, size_t out_n, blk_t* blocks) {
    size_t at = 0;
    for (size_t i = 0; i < n; ++i) {
        blocks[i] = (blk_t){0};
        blocks[i].offset = at;
        blocks[i].len = strlen(parts[i]);
        blocks[i].heading = false;
        blocks[i].image = (image_at && (int)i == *image_at) ? 0 : -1;
        int written = snprintf(out + at, out_n - at, (i + 1 < n) ? "%s\n" : "%s", parts[i]);
        at += (size_t)written;
    }
    return at;
}

int main(void) {
    EpdRect r = {0, 0, 240, 300};
    uint8_t fb[1] = {0};

    /* 场景一：插图排在正文流中间（rc83 之后常见），图片下方的词必须点得准 */
    const char* parts[] = {
        "Alpha beta gamma delta",
        "#",
        "epsilon zeta eta theta iota kappa lambda",
        "mu nu xi omicron pi rho sigma tau upsilon",
        "phi chi psi omega finish here now",
    };
    static char text[512];
    static blk_t blocks[8];
    int image_at = 1;
    size_t len = build_doc(parts, &image_at, 5, text, sizeof(text), blocks);
    book_layout_set_images_visible(true);
    book_layout_set_image_dims(probe, NULL);
    assert(book_layout_build_blocks(text, len, blocks, 5, r, 12));
    size_t pages = book_layout_page_count();
    printf("场景一：插图混排，共 %zu 页\n", pages);
    for (size_t p = 0; p < pages; ++p) {
        rec_reset();
        book_layout_draw_page(fb, p, r, 12);
        printf("  第 %zu 页绘制了 %zu 行\n", p, rec_count);
        for (size_t i = 0; i < rec_count; ++i)
            for (int nth = 0; nth < 6; ++nth) expect_word(p, i, nth, text);
    }

    /* 场景二：段首带缩进空格（原文字节与行缓冲起点不一致） */
    const char indented[] = "   Leading words after three indent spaces continue here for a while\n"
                            "\xe3\x80\x80\xe3\x80\x80" "Full width indent then Orange Banana Cherry words follow\n"
                            "Tail words here with Charlie Delta Echo Foxtrot Golf Hotel end";
    book_layout_set_image_dims(NULL, NULL);
    assert(book_layout_build(indented, strlen(indented), r, 12));
    pages = book_layout_page_count();
    printf("场景二：段首缩进，共 %zu 页\n", pages);
    for (size_t p = 0; p < pages; ++p) {
        rec_reset();
        book_layout_draw_page(fb, p, r, 12);
        for (size_t i = 0; i < rec_count; ++i)
            for (int nth = 0; nth < 6; ++nth) expect_word(p, i, nth, indented);
    }

    /* 场景三：矮图（缩放后高度远小于页高）必须真的留出半个行高，且图下的词点得准。
       这是两种间距算法取值相同的那一支；这里把行顶钉死成「图底 + px/2」，
       将来上游若改动 draw_page 的图后间距，会被这一像素断言抓住。 */
    {
        const char* parts3[] = {
            "Alpha beta gamma delta",
            "#",
            "epsilon zeta eta theta iota kappa lambda mu nu xi omicron",
        };
        static char text3[512];
        static blk_t blocks3[8];
        int image_at3 = 1;
        size_t len3 = build_doc(parts3, &image_at3, 3, text3, sizeof(text3), blocks3);
        probe_w = 120; probe_h = 40;   // 缩放后 120x40，远低于页高 300
        book_layout_set_images_visible(true);
        book_layout_set_image_dims(probe, NULL);
        assert(book_layout_build_blocks(text3, len3, blocks3, 3, r, 12));
        size_t pages3 = book_layout_page_count();
        printf("场景三：矮图混排，共 %zu 页\n", pages3);
        for (size_t p = 0; p < pages3; ++p) {
            rec_reset();
            book_layout_draw_page(fb, p, r, 12);
            expect_image_gap(p, 12, r.height, true);
            for (size_t i = 0; i < rec_count; ++i)
                for (int nth = 0; nth < 6; ++nth) expect_word(p, i, nth, text3);
        }
    }

    /* 场景四：窄高图被等比缩到整页高（图高 == 页高），正是 rc85 条件式为 0、
       rc89 无条件为 px/2 的唯一分歧点。断言：图确实整页、该页不再排正文，
       后续文字落到下一页且照样点得准——即两种算法在这里不会分道扬镳。 */
    {
        const char* parts4[] = {
            "Alpha beta gamma delta",
            "#",
            "epsilon zeta eta theta iota kappa lambda mu nu xi omicron",
        };
        static char text4[512];
        static blk_t blocks4[8];
        int image_at4 = 1;
        size_t len4 = build_doc(parts4, &image_at4, 3, text4, sizeof(text4), blocks4);
        probe_w = 100; probe_h = 500;  // 宽度不超栏，高度被页高截到正好 300
        book_layout_set_images_visible(true);
        book_layout_set_image_dims(probe, NULL);
        assert(book_layout_build_blocks(text4, len4, blocks4, 3, r, 12));
        size_t pages4 = book_layout_page_count();
        printf("场景四：整页高图，共 %zu 页\n", pages4);
        for (size_t p = 0; p < pages4; ++p) {
            rec_reset();
            book_layout_draw_page(fb, p, r, 12);
            expect_image_gap(p, 12, r.height, false);
            for (size_t i = 0; i < rec_count; ++i)
                for (int nth = 0; nth < 6; ++nth) expect_word(p, i, nth, text4);
        }
    }
    book_layout_free();
    rec_reset();

    printf("检查 %d 处点击，失败 %d 处\n", checks, failures);
    printf(failures ? "ROUNDTRIP_FAIL\n" : "ROUNDTRIP_PASS\n");
    return failures != 0;
}
