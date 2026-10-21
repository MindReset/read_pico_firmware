#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""中文：book_layout 单栏排版黄金基线（P0 前置冻结）。
English: Frozen golden baseline for book_layout single-column pagination (P0 prerequisite).

背景 / Why: 横屏双栏（P0）不改 book_layout.c，只在 app 层把单栏版面切成两栏。互证测试①
要求「功能关 = 旧版逐位一致」——本测试就是那个「旧版」的参照：在 P0 动手前的树上，
把单栏排版的可观测行为全部冻结：

  - 分页：页数、每页起始字节偏移；
  - 插图：每页插图张数、序号/页内 y/显示宽高（含流进正文的图与整页回退图）；
  - 绘制：每页 book_layout_draw_page() 的全部原语调用（ttf_draw_text_px* 的参数与文本）
    + 阅读线（epd_fill_rect 段数/位置/灰度，经 epdiy 桩的 test_guide_* 全局量）；
  - 命中：每页多个探测点的 book_layout_hit_test() 结果（词区间）；
  - 寻址：若干字节偏移的 book_layout_page_for_offset() 结果。

字体测量/绘制用确定性模型桩（ASCII 半宽、其余全宽；ascender=px），摘排由真实
book_layout.c 整文件编译执行——不抽取、不改写，黄金值锁的就是生产代码本身。
FNV-1a 摘要逐字段喂入；不匹配时打印全量流并 exit 1。

用法 / Usage:
    python3 tools/book_layout_single_column_host_test.py           # 比对黄金值
    python3 tools/book_layout_single_column_host_test.py --regen   # 重签黄金值（仅在有意变更单栏排版时）
"""
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HERE = Path(__file__).resolve().parent
GOLDEN = HERE / 'book_layout_single_column_golden.txt'
STREAM = HERE / 'book_layout_single_column_stream.txt'

UNIT = r'''
/* 自包含测试单元：确定性字体模型桩 + fixture + 场景表；book_layout.c 以真实源码整文件
   链接（见 Python 侧的编译命令），不抽取不改写。/ Self-contained unit: deterministic font
   model stubs, fixtures and scenarios; the real book_layout.c is linked whole. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* r93 行缓冲压力钩子：stub 记录最大单次分配，测试场景断言内存上限。
   r93 line-buffer pressure hook: the stub records the largest single allocation
   so scenarios can assert memory bounds. */
size_t test_heap_largest;
#include "book_layout.h"
/* 归因：r91 的绘制路径直接用字体槽/run 类型，测试单元要能看到同一份桩声明。
   Attribution: r91's draw path uses the font slot/run types directly, so the unit needs
   the same stub declarations in scope. */
#include "ttf_font.h"

/* ---- 确定性字体模型：ASCII 半宽、其余全宽；ascender=px；左bearing=px/8。----
   Deterministic font model: ASCII half-width, everything else full-width. */
int ttf_text_width_px(int px, const char* text) {
    int total = 0;
    for (const unsigned char* p = (const unsigned char*)text; *p;) {
        size_t n = *p < 0x80 ? 1 : (*p >= 0xf0 ? 4 : (*p >= 0xe0 ? 3 : 2));
        total += n == 1 ? px / 2 : px;
        p += n;
    }
    return total;
}
int ttf_text_left_bearing_px(int px, const char* glyph) { (void)glyph; return px / 8; }
int ttf_ascender_px(int px) { return px; }
/* 归因：r91 的 draw 路径按 run 选字体并在画完后把手写游标还给系统 face，字体槽体系
   在宿主里无字形可查，桩只需记住当前槽（单栏场景恒为系统槽，不改变绘制调用流）。
   Attribution: r91's draw path selects a font per run and hands the cursor back to the
   system face afterwards; the host has no glyphs to look up, so the stub only remembers
   the slot (always the system slot in single-column scenes, leaving the call stream as is). */
static int s_ttf_slot = TTF_FONT_SLOT_SYSTEM;
static size_t s_ttf_run_count;
void ttf_draw_set_runs(const ttf_run_t* runs, size_t count) {
    (void)runs; s_ttf_run_count = count;
}
int ttf_font_select(int slot) { s_ttf_slot = slot; return 0; }
bool ttf_font_slot_ready(int slot) { return slot == TTF_FONT_SLOT_SYSTEM; }
int ttf_font_selected(void) { return s_ttf_slot; }

/* ---- 绘制调用流：一次原语调用一条记录。/ Draw-call stream: one record per call. ---- */
enum { OP_TEXT, OP_TEXT_SPACED, OP_TEXT_FITTED, OP_TEXT_VERTICAL };
typedef struct {
    int op, x, y, px, align, fg, bg, tracking, target;
    char text[512]; // 横屏一行可达 40+ 汉字 / a landscape line can exceed 40 CJK glyphs
} draw_call_t;
static draw_call_t s_calls[8192];
static int s_call_count;
static void record(int op, int x, int y, int px, const char* text, int align,
                   int tracking, int target) {
    assert(s_call_count < (int)(sizeof(s_calls) / sizeof(s_calls[0])));
    draw_call_t* c = &s_calls[s_call_count++];
    memset(c, 0, sizeof(*c));
    c->op = op; c->x = x; c->y = y; c->px = px; c->align = align;
    c->fg = 0; c->bg = 15; c->tracking = tracking; c->target = target;
    size_t n = strlen(text);
    assert(n < sizeof(c->text));
    memcpy(c->text, text, n + 1);
}
void ttf_draw_text_px(uint8_t* fb, int x, int y, int px, const char* text,
                      enum EpdFontFlags align, uint8_t fg, uint8_t bg) {
    (void)fb; (void)fg; (void)bg;
    record(OP_TEXT, x, y, px, text, (int)align, 0, 0);
}
void ttf_draw_text_px_spaced(uint8_t* fb, int x, int y, int px, const char* text,
                             int tracking_px, uint8_t fg, uint8_t bg) {
    (void)fb; (void)fg; (void)bg;
    record(OP_TEXT_SPACED, x, y, px, text, 0, tracking_px, 0);
}
void ttf_draw_text_px_fitted(uint8_t* fb, int x, int y, int px, const char* text,
                             int tracking_px, int target_width, uint8_t fg, uint8_t bg) {
    (void)fb; (void)fg; (void)bg;
    record(OP_TEXT_FITTED, x, y, px, text, 0, tracking_px, target_width);
}
/* 归因：r92 给 book_layout.c 带来竖排绘制路径（draw_vertical_column），真实整文件链接
   需要这个符号。单栏黄金场景从不调用 book_layout_set_vertical(true)，竖排路径不该被执行；
   这里仍记入独立 op——一旦竖排绘制混进单栏场景，黄金流就会变、摘要就会失败，守卫不松。
   Attribution: r92 brought the vertical draw path (draw_vertical_column) into book_layout.c,
   so the whole-file link needs this symbol. Single-column golden scenes never call
   book_layout_set_vertical(true), so the vertical path must not run; it still records under
   its own op — if vertical drawing ever leaks into single-column scenes the golden stream
   changes, the digest fails, and the guard holds. */
int ttf_draw_text_px_vertical(uint8_t* fb, int x, int y, int pixel_height, const char* text,
                              int tracking_px, uint8_t fg, uint8_t bg) {
    (void)fb; (void)fg; (void)bg; (void)tracking_px;
    record(OP_TEXT_VERTICAL, x, y, pixel_height, text, 0, tracking_px, 0);
    return pixel_height; // 确定性正返回值：调用方按消耗高度推进列位置。
}
/* 阅读线段由 epdiy 桩记入这些全局量。/ The epdiy stub records guide segments here. */
int test_guide_segments, test_guide_first_y, test_guide_height;
uint8_t test_guide_gray;
/* esp_heap_caps 桩的分配失败开关：负值 = 从不失败（0 表示第 0 次分配后即失败）。
   Heap-fail switch from the stub: negative never fails (0 fails after zero allocations). */
int test_heap_fail_after = -1;

/* ---- fixture A：纯文本（无块表，走 book_layout_build）。/ Plain prose, no blocks. ---- */
static const char* const A_PARAS[] = {
    "河边记事是一场缓慢的雨。云从山脊后面涌出来，把整片河谷压得很低，水鸟贴着水面飞，翅膀带起的风把芦苇压出一折折的亮痕。",
    "他坐在旧渡口的木桩上，看对岸的炊烟一根根立起来。船已经很久没来，渡绳在桩上磨出了深槽，槽里积着雨水，映着很小的天。",
    "The old ferry rope had worn a deep groove into the post, and the groove held rainwater that mirrored a very small sky.",
    "雨停的时候，河谷里会泛起薄雾。雾从水面起步，爬上石阶，漫过他的布鞋，把裤脚浸成深色，他却不动，像一尊被遗忘在渡口的雕像。",
    "有人说下游的桥修好了，船不再往来；也有人说桥是假的，夜里会发出空洞的响声。他两种都不信，只数对岸的炊烟。",
    "第一缕炊烟升起时，他知道有人家开始做饭；最后一缕炊烟消散时，黑夜就真的落下来了。星星从河谷上空一颗一颗亮起来，很慢，很耐心。",
    "他口袋里有一张褪色的车票，起点被磨掉了，终点写着远处的城市。票面上的日期每年被他用铅笔加深一次，如今厚得像一小块木板。",
    "a small wooden board of a date, deepened with pencil every year until the ink stood above the paper.",
    "冬天来得比往年早。芦苇枯成一片脆弱的黄，风一过就齐声倒下，像整片河谷在低声背诵一封很长的信。他听着，忽然听懂了似的点了点头。",
    "雪落在渡绳上，把麻绳的纤维一根一根描出来。他伸手抹掉一小块雪，露出底下的湿麻，那种凉意顺着指尖走，一直走到很远的过去。",
    "春天再临的时候，渡口来了一位年轻的画家。她架起画板，把倒扣的木船、磨穿的桩和坐着的人都收进画面，只是省略了天空。",
    "他问为什么省略天空。画家说，这里的天空从来不属于渡口，它只是路过。他想了想，从口袋里摸出那张车票，递给她看背面的空白。",
};
static char s_text_a[8192];
static size_t s_len_a;

/* ---- fixture B：题头/居中/右齐/插图/图旁空白/高图/整页回退图/段距。----
   Rich blocks: heading, centred, right-aligned, inline image, image spacer, tall image,
   whole-page fallback image and paragraph margins. */
static char s_text_b[8192];
static blk_t s_blocks_b[16];
static size_t s_len_b, s_count_b;

static void begin_fixture_b(void) {
    s_len_b = s_count_b = 0;
}
static void add_block_b(const char* body, bool heading, uint8_t align, int image,
                        uint8_t margin_before, uint8_t margin_after) {
    size_t len = strlen(body);
    assert(s_len_b + len + 1 < sizeof(s_text_b));
    memcpy(s_text_b + s_len_b, body, len);
    blk_t* b = &s_blocks_b[s_count_b++];
    memset(b, 0, sizeof(*b));
    b->offset = s_len_b;
    b->len = len;
    b->heading = heading;
    b->align = align;
    b->image = image;
    b->margin_before_percent = margin_before;
    b->margin_after_percent = margin_after;
    s_len_b += len;
    s_text_b[s_len_b++] = '\n'; // 段间换行，最后一块之外都由收尾修剪掉 / trimmed at the end
}
static void end_fixture_b(void) {
    assert(s_len_b > 0 && s_text_b[s_len_b - 1] == '\n');
    --s_len_b; // 最后一块必须正好收在文本末尾 / the last block must end exactly at len
    s_text_b[s_len_b] = 0;
}

static void build_fixtures(void) {
    size_t count = sizeof(A_PARAS) / sizeof(A_PARAS[0]);
    for (size_t i = 0; i < count; ++i) {
        size_t len = strlen(A_PARAS[i]);
        memcpy(s_text_a + s_len_a, A_PARAS[i], len);
        s_len_a += len;
        if (i + 1 < count) s_text_a[s_len_a++] = '\n';
    }
    s_text_a[s_len_a] = 0; // 结尾零不计入长度 / the terminator is not part of len
    begin_fixture_b();
    add_block_b("第三章 渡口", true, 0, -1, 0, 0);
    add_block_b(A_PARAS[0], false, 0, -1, 0, 0);
    add_block_b("——谨以此篇献给所有等待——", false, 1, -1, 0, 0);
    add_block_b(" ", false, 0, 0, 0, 0);          // 插图 0：流进正文的图
    add_block_b("　", false, 0, -1, 0, 0);        // 图旁全角空白（应被跳过）
    add_block_b(A_PARAS[1], false, 0, -1, 0, 0);
    add_block_b(" ", false, 0, 1, 0, 0);          // 插图 1：高图，逼翻页
    add_block_b(A_PARAS[3], false, 0, -1, 60, 40); // 带段前/段后距
    add_block_b(A_PARAS[4], false, 2, -1, 0, 0);   // 右对齐
    add_block_b(" ", false, 0, 2, 0, 0);          // 插图 2：无尺寸 → 整页回退
    add_block_b(A_PARAS[5], false, 0, -1, 0, 0);
    add_block_b(A_PARAS[8], false, 0, -1, 0, 0);
    end_fixture_b();
}

/* ---- 场景 ---- */
typedef struct {
    const char* name;
    EpdRect rect;
    int px;
    unsigned line_percent, paragraph_percent;
    int tracking;
    unsigned indent_em;
    int indent_adjust;
    unsigned reading_line;
    int reading_line_offset;
    bool images_visible;
    size_t lead_skip;
    unsigned lead_height;
    bool with_blocks;
    bool with_dims;
} scenario_t;

static const scenario_t SCENARIOS[] = {
    /* 竖屏正文（与 app 的 body_rect 同几何：margin36/px26/tracking0 均衡后）。*/
    {"plain_portrait",     {43, 188, 598, 916}, 26, 150, 50, 0, 2, 0, 0, 0, true, 0, 0, false, false},
    /* 横屏正文（P0 的目标几何）。/ Landscape body — P0's target geometry. */
    {"plain_landscape",    {36, 96, 1144, 500}, 26, 150, 50, 0, 2, 0, 0, 0, true, 0, 0, false, false},
    /* 字距 + 首行缩进联动。/ Tracking plus first-line indent. */
    {"plain_track_indent", {43, 188, 598, 916}, 26, 150, 50, 2, 3, 6, 0, 0, true, 0, 0, false, false},
    /* 富块表竖屏。/ Rich blocks, portrait. */
    {"rich_portrait",      {43, 188, 598, 916}, 26, 150, 50, 0, 2, 0, 0, 0, true, 0, 0, true, true},
    /* 富块表横屏。/ Rich blocks, landscape. */
    {"rich_landscape",     {36, 96, 1144, 500}, 26, 150, 50, 0, 2, 0, 0, 0, true, 0, 0, true, true},
    /* 行距/段距/负字距。/ Line/paragraph spacing and negative tracking. */
    {"rich_spacing",       {43, 188, 598, 916}, 26, 180, 80, -2, 1, -4, 0, 0, true, 0, 0, true, true},
    /* 关闭插图：图片块不占位。/ Images hidden: image blocks take no space. */
    {"rich_images_hidden", {43, 188, 598, 916}, 26, 150, 50, 0, 2, 0, 0, 0, false, 0, 0, true, true},
    /* 章首预留题头区。/ Chapter lead reservation. */
    {"rich_lead",          {43, 188, 598, 916}, 26, 150, 50, 0, 2, 0, 0, 0, true, 17, 214, true, true},
    /* 阅读线 + 偏移。/ Reading line with offset. */
    {"rich_reading_line",  {43, 188, 598, 916}, 22, 150, 50, 0, 2, 0, 1, -4, true, 0, 0, true, true},
    /* 窄栏：密集折行与禁则。/ Narrow column: dense wrapping and kinsoku. */
    {"kinsoku_narrow",     {36, 258, 300, 900}, 26, 150, 50, 0, 2, 0, 0, 0, true, 0, 0, false, false},
};

static bool test_dims(void* ctx, int image, int* width, int* height) {
    (void)ctx;
    if (image == 0) { *width = 480; *height = 360; return true; }
    if (image == 1) { *width = 900; *height = 1200; return true; }
    return false; // 插图 2：未知尺寸 → 整页回退 / image 2: unknown → whole-page fallback
}

/* ---- FNV-1a 64 位摘要：逐字段喂入，不碰结构体内存。----
   FNV-1a 64 digest over fields one by one: no struct memory is hashed. */
static unsigned long long s_hash = 1469598103934665603ULL;
static void hash_bytes(const void* p, size_t n) {
    const unsigned char* b = p;
    for (size_t i = 0; i < n; ++i) {
        s_hash ^= b[i];
        s_hash *= 1099511628211ULL;
    }
}
#define HASH(v) hash_bytes(&(v), sizeof(v))
static void hash_text(const char* text) {
    size_t n = strlen(text);
    HASH(n);
    hash_bytes(text, n);
}

static void reset_settings(void) {
    book_layout_set_spacing(150, 50);
    book_layout_set_typography(0);
    book_layout_set_first_line_indent(2);
    book_layout_set_first_line_indent_adjust(0);
    book_layout_set_reading_line(0);
    book_layout_set_reading_line_offset(0);
    book_layout_set_images_visible(true);
    book_layout_set_chapter_lead(0, 0);
    book_layout_set_image_dims(NULL, NULL);
}

static void run_scenario(const scenario_t* sc) {
    reset_settings();
    book_layout_set_spacing(sc->line_percent, sc->paragraph_percent);
    book_layout_set_typography(sc->tracking);
    book_layout_set_first_line_indent(sc->indent_em);
    book_layout_set_first_line_indent_adjust(sc->indent_adjust);
    book_layout_set_reading_line(sc->reading_line);
    book_layout_set_reading_line_offset(sc->reading_line_offset);
    book_layout_set_images_visible(sc->images_visible);
    book_layout_set_chapter_lead(sc->lead_skip, sc->lead_height);
    book_layout_set_image_dims(sc->with_dims ? test_dims : NULL, NULL);

    bool ok = sc->with_blocks
        ? book_layout_build_blocks(s_text_b, s_len_b, s_blocks_b, s_count_b, sc->rect, sc->px)
        : book_layout_build(s_text_a, s_len_a, sc->rect, sc->px);
    uint32_t gen = book_layout_generation();
    size_t pages = book_layout_page_count();
    EpdRect built = book_layout_body_rect();
    /* 代次是单调计数器（每次 build/free 自增），只进流不哈希——否则两遍必然不同。
       Generation is a monotonic counter (bumped on every build/free): printed, never hashed,
       or the two determinism passes could not match. */
    HASH(ok); HASH(pages);
    HASH(built.x); HASH(built.y); HASH(built.width); HASH(built.height);
    printf("scenario=%s ok=%d gen=%u pages=%zu body={%d,%d,%d,%d}\n",
           sc->name, ok, gen, pages, built.x, built.y, built.width, built.height);

    for (size_t page = 0; page < pages; ++page) {
        size_t start = book_layout_page_start_offset(page);
        int images = book_layout_page_image_count(page);
        HASH(start); HASH(images);
        printf("  page=%zu start=%zu images=%d", page, start, images);
        for (int i = 0; i < images; ++i) {
            int image = -1, y = 0, w = 0, h = 0;
            bool got = book_layout_page_image_at(page, i, &image, &y, &w, &h);
            HASH(got); HASH(image); HASH(y); HASH(w); HASH(h);
            printf(" [%d]img=%d,y=%d,%dx%d", i, image, y, w, h);
        }
        printf("\n");
        /* 整页回退图没有记录、没有文字：draw_page 直接返回，调用流为空。
           A whole-page fallback image has no record and no text: draw_page returns at once. */
        uint8_t fb = 0;
        s_call_count = 0;
        /* 阅读线全局量每页归零：否则跨页/跨场景累积，两遍摘要必不同。
           Reset the guide globals per page: they accumulate otherwise and the
           two determinism passes could never match. */
        test_guide_segments = 0; test_guide_first_y = 0;
        test_guide_height = 0; test_guide_gray = 0;
        book_layout_draw_page(&fb, page, sc->rect, sc->px);
        HASH(s_call_count);
        for (int i = 0; i < s_call_count; ++i) {
            const draw_call_t* c = &s_calls[i];
            HASH(c->op); HASH(c->x); HASH(c->y); HASH(c->px); HASH(c->align);
            HASH(c->tracking); HASH(c->target); hash_text(c->text);
        }
        HASH(test_guide_segments); HASH(test_guide_first_y);
        HASH(test_guide_height); HASH(test_guide_gray);
        printf("  page=%zu draws=%d guides=%d\n", page, s_call_count, test_guide_segments);
        for (int k = 0; k < 4; ++k) {
            int y = sc->rect.y + 20 + k * 36;
            for (int side = 0; side < 2; ++side) {
                int x = side ? sc->rect.x + sc->rect.width - 60 : sc->rect.x + 40;
                size_t word_start = 0, word_end = 0;
                bool hit = book_layout_hit_test(page, x, y, &word_start, &word_end);
                HASH(hit);
                if (hit) { HASH(word_start); HASH(word_end); }
                printf("  probe page=%zu (%d,%d) -> %d [%zu,%zu)\n",
                       page, x, y, hit, word_start, word_end);
            }
        }
    }
    size_t len = sc->with_blocks ? s_len_b : s_len_a;
    size_t probes[] = {0, 1, len / 3, len / 2, len - 1, len, len + 500};
    for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); ++i) {
        size_t page = book_layout_page_for_offset(probes[i]);
        HASH(page);
        printf("  offset=%zu -> page=%zu\n", probes[i], page);
    }
    book_layout_free();
}

/* 桩记录全局（r95 rebase 补）：本测试不校验阅读辅助线，但上游 book_layout.c 的竖排列
   绘制会经由 epdiy.h 的 epd_fill_rect 桩写入这些符号。这些符号此前只由
   book_layout_host_test.c 定义，上游 r94/r95（PR34）改动竖排辅助线后，本测试的编译单元
   也激活了桩引用路径 → 链接期 undefined reference。此处一次性补齐全套，避免上游再改动
   其它绘制路径时同类断裂复发。
   Stub recording globals (added on the r95 rebase): this test does not check the reading
   guide, but upstream's vertical-column drawing writes through the epd_fill_rect stub in
   epdiy.h. These symbols used to be defined only by book_layout_host_test.c; after upstream
   r94/r95 (PR34) changed the vertical guide, this translation unit activated the stub path
   too and failed to link. All six are defined here at once so a future upstream change to
   another drawing path cannot break this test the same way. */
int test_guide_segments;
int test_guide_first_x;
int test_guide_first_y;
int test_guide_second_y;
int test_guide_last_y;
int test_guide_height;

int main(void) {
    setbuf(stdout, NULL); // 断言中止时也不丢诊断输出 / keep diagnostics on abort
    build_fixtures();
    assert(s_len_a > 0 && s_len_b > 0 && s_count_b >= 10);
    for (size_t i = 0; i < sizeof(SCENARIOS) / sizeof(SCENARIOS[0]); ++i)
        run_scenario(&SCENARIOS[i]);
    unsigned long long first = s_hash;
    /* 确定性：整套再跑一遍，摘要必须一致。/ Determinism: a second full pass must match. */
    s_hash = 1469598103934665603ULL;
    for (size_t i = 0; i < sizeof(SCENARIOS) / sizeof(SCENARIOS[0]); ++i)
        run_scenario(&SCENARIOS[i]);
    assert(first == s_hash);
    printf("digest=%016llx\n", first);
    puts("book_layout_single_column: PASS");
    return 0;
}
'''


def run_c(unit):
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        (work / 'test.c').write_text(unit, encoding='utf-8')
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined',
                        '-I' + str(ROOT / 'tools' / 'book_layout_stubs'),
                        '-I' + str(ROOT / 'tools' / 'book_source_host_stubs'),
                        '-I' + str(ROOT / 'main' / 'book'),
                        str(work / 'test.c'), str(ROOT / 'main' / 'book' / 'book_layout.c'),
                        '-o', str(work / 'test')], check=True)
        proc = subprocess.run([str(work / 'test')], check=True, capture_output=True, text=True)
        return proc.stdout


def main():
    regen = '--regen' in sys.argv
    out = run_c(UNIT)
    digest = next(line for line in out.splitlines() if line.startswith('digest=')).split('=', 1)[1]
    stream = '\n'.join(line for line in out.splitlines()
                       if not line.startswith(('digest=', 'book_layout_single_column:')))
    if regen:
        GOLDEN.write_text('digest=%s\n' % digest, encoding='utf-8')
        STREAM.write_text(stream + '\n', encoding='utf-8')
        print('book_layout_single_column: golden frozen -> %s (%s)' % (GOLDEN.name, digest))
        return
    golden = GOLDEN.read_text(encoding='utf-8').strip()
    if golden != 'digest=%s' % digest:
        print('book_layout single-column layout CHANGED: golden %s, now %s' % (golden, digest))
        print('--- current stream (run with --regen only after an intentional layout change) ---')
        print(stream)
        raise SystemExit(1)
    print('book_layout_single_column: PASS (%s)' % digest)


if __name__ == '__main__':
    main()
