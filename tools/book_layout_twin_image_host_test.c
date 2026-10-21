/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：双栏 × 插图布局 fixture（e2e 返工范围第 1 条，用户裁决四 2026-10-17 钉死三场景）。
 * English: Two-column x illustration layout fixture (e2e rework scope item 1; the three
 * scenarios were pinned by adjudication 4, 2026-10-17).
 * 冻结：只用于宿主测试。/ Frozen: Host testing only.
 *
 * 场景参数忠实复刻设备侧双栏路径（main/apps/app_book.c）：
 *   - build 输入 = reader_layout_rect_for(body, cols) = reader_column_rect(body, cols, 0)
 *     ——第 0 栏（竖排最右栏）矩形，栏宽 = (body.width - gutter) / cols；
 *   - book_layout_set_image_bleed_width(栏宽) ——G7-A1 修复后接线：reader_relayout_cols
 *     出口按栏数快照发布出血宽，双栏发栏宽（修复前为整屏宽 UI_LOCK_WIDTH）；
 *   - book_layout_set_vertical(true) ——横屏双栏复用竖排分栏（栏自右向左铺、栏内向下走）。
 * The scenario parameters faithfully replay the device two-column path (app_book.c):
 * build input is column 0's rect; the bleed width is the column width (post-G7-A1-fix
 * wiring: the reader_relayout_cols exit publishes the bleed width at the column
 * snapshot, column width at two columns — the panel width before the fix); and the
 * two-column mode reuses vertical pagination (columns fill right to left).
 *
 * 三场景（钉死）：大插图（修复验证：通栏大图收进栏内）+ 小插图（正常对照）+ 无图（基线）。
 * The three pinned scenarios: big illustration (fix verification: full-bleed art now
 * stays inside the column), small illustration (normal control), no illustration (baseline).
 *
 * 断言表达设计意图（e2e 返工范围钉死：放置几何不越栏、不遮挡对侧栏）：
 *   A1 记录层：插图记录宽 ≤ 栏宽；
 *   A2 遮挡层：按设备侧绘制契约（draw_reader_images 的 band 居中公式）复算绘制矩形，
 *      包含于本栏且不与对侧栏相交；G7-A1 修复后双栏 band 即栏宽（bleed=栏宽），
 *      机制登记信息行照实打印实测值，不作判据；
 *   A3 对照：小插图场景上述两条恒绿（「小图不挡栏」）；
 *   A4 基线：无图场景纯文本双栏行为不受影响；
 *   A5 失败特征签名对表（照片 3 的两种形态在宿主侧分形）：
 *      - 绘制流（drawn）逐字完整但几何越栏    →「被盖住」类（几何/遮挡）；
 *      - 绘制流本身缺字符（行首缺字形态）      →「未绘制」类（布局/丢字）。
 *      照片上两者同形；本 fixture 以已知文本把它们在宿主侧分开（用户第二轮裁决定档）。
 *      可执行注记①：「icture ← picture」「e eye ← the eye」行首缺字形态作大插图场景
 *      失败特征签名，跑完对表——G7-A1 宿主侧复现轮结论为「被盖住」类（字形已绘制、
 *      被插图盖住），缺字签名未复现，按注记① 留 gen8 设备侧像素层补采；
 *   A6 slots 行口径 fixture 化：插图页必在无行视图集合——设备侧 slots 日志行只在
 *      count==0 时打印（有图页结构性失明，1:1 误设已推翻）；宿主侧逐页断言地图分类器
 *      的两个 O(1) 输入（页级插图索引 + 混排槽数）与插图页零文字贡献，检测口径不再
 *      靠设备日志推断。
 * The assertions express design intent (never cross the column, never cover the
 * opposite column). A5 splits the two same-looking photo shapes on the host with a
 * known text: complete draw stream + crossing geometry = "covered"; missing glyphs in
 * the draw stream = "never drawn". Per executable note 1, the missing-leading-glyph
 * shapes are the adopted failure signature: a match confirms the reproduction, a miss
 * means the phenomenon surface needs more sampling. A6 fixture-izes the slots row
 * criterion: the device slots log line only prints when count == 0 (structurally blind
 * to image pages), so the host asserts the map classifier's two O(1) inputs and the
 * illustration page's zero text contribution page by page.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "book_layout.h"
#include "ttf_font.h"

/* ---- 宿主替身字体（照 book_layout_host_test.c 模式：记录绘制流与落点）---- */
/* Host stand-in font (same pattern as book_layout_host_test.c): records the draw
 * stream and where each call landed. */
static char drawn[20000];
static int first_draw_x, last_draw_x, first_draw_y, last_draw_y;
static int first_draw_px, last_draw_px;
static int test_cjk_advance;
static int vertical_draws, first_vertical_x, last_vertical_x;
static int rotated_draws;
int test_heap_fail_after = -1; /* esp_heap_caps.h 桩的失效注入点 / heap stub failure injection */
size_t test_heap_largest;      /* esp_heap_caps.h 桩的最大分配记录 / heap stub largest-alloc record */

int test_guide_segments;
int test_guide_first_x;   /* r95 rebase 补：竖排辅助线经 epd_fill_rect 桩写入，链接期需定义 */
int test_guide_first_y;
int test_guide_second_y;  /* r95 rebase 补：同上 */
int test_guide_last_y;    /* r95 rebase 补：同上 */
int test_guide_height;
uint8_t test_guide_gray;

void ttf_draw_set_runs(const ttf_run_t* runs, size_t count) { (void)runs; (void)count; }

static void note_draw(const char* text, int x, int y, int px) {
    if (!drawn[0]) { first_draw_px = px; first_draw_x = x; first_draw_y = y; }
    last_draw_px = px; last_draw_x = x; last_draw_y = y;
    assert(strlen(drawn) + strlen(text) < sizeof(drawn));
    strcat(drawn, text);
}
int ttf_font_select(int slot) { (void)slot; return 0; }
int ttf_font_selected(void) { return 0; }
bool ttf_font_slot_ready(int slot) { (void)slot; return true; }
int ttf_text_width_px(int px, const char* text) {
    int n = 0, width = 0;
    for (; *text; text++) if (((unsigned char)*text & 0xc0) != 0x80) {
        n++;
        width += (unsigned char)*text >= 0x80 && test_cjk_advance ? test_cjk_advance : px;
    }
    return width + n; /* 每字再留 1px 间距，避免零宽塌陷 / +1px per glyph so nothing collapses */
}
int ttf_ascender_px(int px) { return px; }
int ttf_text_left_bearing_px(int px, const char* text) { (void)px; (void)text; return 0; }
void ttf_draw_text_px(uint8_t* fb, int x, int y, int px, const char* text,
                      enum EpdFontFlags align, uint8_t fg, uint8_t bg) {
    (void)fb; (void)align; assert(fg <= 15 && bg <= 15);
    note_draw(text, x, y, px);
}
void ttf_draw_text_px_spaced(uint8_t* fb, int x, int y, int px, const char* text,
                             int tracking_px, uint8_t fg, uint8_t bg) {
    (void)fb; (void)tracking_px;
    ttf_draw_text_px(fb, x, y, px, text, EPD_DRAW_ALIGN_LEFT, fg, bg);
}
void ttf_draw_text_px_fitted(uint8_t* fb, int x, int y, int px, const char* text,
                             int tracking_px, int target_width, uint8_t fg, uint8_t bg) {
    (void)target_width;
    ttf_draw_text_px_spaced(fb, x, y, px, text, tracking_px, fg, bg);
}
int ttf_draw_text_px_rotated(uint8_t* fb, int x, int y, int px, const char* text,
                             uint8_t fg, uint8_t bg) {
    (void)fb; (void)fg; (void)bg;
    ++rotated_draws;
    note_draw(text, x, y, px);
    return ttf_text_width_px(px, text);
}
int ttf_draw_text_px_vertical(uint8_t* fb, int x, int y, int px, const char* text,
                              int tracking_px, uint8_t fg, uint8_t bg) {
    (void)fb; (void)tracking_px; (void)fg; (void)bg;
    ++vertical_draws;
    if (vertical_draws == 1) { first_vertical_x = x; }
    last_vertical_x = x;
    note_draw(text, x, y, px);
    return ttf_text_width_px(px, text);
}
int ttf_vertical_text_height_px(int px, const char* text, int tracking_px) {
    return ttf_text_width_px(px, text) + (tracking_px > 0 ? tracking_px : 0);
}

/* ---- 断言记账：一次跑完所有断言，红点全列，退出码区分形态 ---- */
/* Assertion ledger: run every assertion, list every red, and let the exit code tell
 * the shapes apart. */
static int checks, reds;
static int check(int ok, const char* label) {
    ++checks;
    if (!ok) { ++reds; printf("  FAIL %s\n", label); }
    return ok;
}
static int rects_intersect_x(EpdRect a, int bx, int bw) {
    int a_lo = a.x, a_hi = a.x + a.width;
    int b_lo = bx, b_hi = bx + bw;
    return a_lo < b_hi && b_lo < a_hi;
}

/* dims 回调：按块的 image_width/image_height 供数（PR9 契约：尺寸由调用方提供）。
 * dims callback: dimensions come from the block (PR9 contract). */
static bool fixture_image(void* ctx, int image, int* width, int* height) {
    const html_text_t* chapter = ctx;
    for (size_t i = 0; i < chapter->count; ++i) {
        if (chapter->blocks[i].image == image) {
            *width = (int)chapter->blocks[i].image_width;
            *height = (int)chapter->blocks[i].image_height;
            return *width > 0 && *height > 0;
        }
    }
    return false;
}

/* 照片 3 的可读正文（已知文本，A5 对表用）。/ The readable body of photo 3 (known text for A5). */
static const char PHOTO3_TEXT[] =
    "This page carries both a picture and a word, so that the eye has somewhere "
    "to rest and the mind has something to follow.";

/* 设备侧同构场景参数。/ Device-homomorphic scenario parameters. */
#define BODY_W 1000 /* 横屏版心宽 / landscape body width */
#define BODY_H 800  /* 横屏版心高 / landscape body height */
#define GUTTER 48   /* reader_column_gutter：版心边距下限 48 / gutter floor */
#define COL_W ((BODY_W - GUTTER) / 2)      /* 栏宽 = (1000-48)/2 = 476 */
#define COL0_X 0                            /* 栏 0 x：reader_column_offset(i=0)=0，版心左起第一栏 */
#define BLEED_W COL_W /* G7-A1 修复后接线：reader_relayout_cols 双栏发布栏宽
                         （修复前 on_enter 发布 UI_LOCK_WIDTH=整屏宽，已收归出口） */
#define FONT_PX 24

/* 对侧栏（栏 1）矩形：reader_column_offset(i=1)=栏宽+栏距，在版心右半。
 * Opposite column (column 1): offset is column width plus gutter, on the right half. */
#define COL1_X (COL_W + GUTTER)

/* 构造「图块 + 文块」并 build；返回页数。/ Build an image block plus a text block.
 * 文本缓冲所有权随 doc 移交调用方：book_layout_build_blocks 只借存 s_text 指针（不拷贝），
 * 调用方须在最后一次 book_layout_draw_page 之后才能释放（draw 要读 s_text）。
 * Ownership of the text buffer passes to the caller through doc->utf8: build_blocks only
 * borrows the s_text pointer without copying, so the caller may free it only after the
 * last draw_page (drawing reads s_text). */
static size_t build_illustrated(html_text_t* doc, blk_t* blocks, const char* text,
                                int image, unsigned iw, unsigned ih, EpdRect column) {
    size_t text_len = strlen(text);
    blocks[0] = (blk_t){.offset = 0, .len = 3, .image = image,
                        .image_width = iw, .image_height = ih};
    blocks[1] = (blk_t){.offset = 4, .len = text_len, .image = -1};
    char* buf = malloc(4 + text_len + 1);
    assert(buf);
    memcpy(buf, "IMG", 3);
    buf[3] = '\n';
    memcpy(buf + 4, text, text_len + 1);
    doc->blocks = blocks;
    doc->count = 2;
    doc->utf8 = buf;
    doc->len = 4 + text_len;
    book_layout_set_image_dims(fixture_image, doc);
    int ok = book_layout_build_blocks(buf, doc->len, blocks, 2, column, FONT_PX);
    assert(ok);
    return book_layout_page_count();
}

/* 一个场景的公共断言。/ Shared per-scenario checks.
 *
 * 判据分两层，都表达设计意图「放置几何不越栏、不遮挡对侧栏」：
 *   A1 记录层：插图记录宽 ≤ 栏宽（大图修复后 476 = 栏宽、小图 200 恒绿）；
 *   A2 遮挡层：按设备侧绘制契约（draw_reader_images 的 band 居中公式）复算绘制矩形，
 *      要求包含于本栏、不与对侧栏相交（大图修复后 [0,476] 恰为本栏、小图 [138,338] 绿）。
 *
 * G7-A1 修复后双栏 band 即栏宽（bleed=栏宽，reader_relayout_cols 出口按栏数快照发布）——
 * band 实测值照实打头机制登记信息行，不作判据。PR9「双栏下整页图与栏宽关系未定」在
 * A 方案（ bleed 按栏数发布）下落订；B 方案（跨栏独占整视觉页）登记 P2，不混入本 fixture。
 *
 * expect_clamp：本场景预期记录宽是否被栏宽截断（大图 900>476 = 1，小图 200<476 = 0）
 * ——fixture 自身判别力对表：截断必须恰在原生宽越栏时发生（防「大小图同被截/同不截」）。
 * 返回几何层（A1+A2）红点数，供退出码形态分流与 A5 对表取用。*/
static int check_geometry(const char* scenario, int image_index, int native_w, int native_h,
                          int expect_clamp) {
    int img = -1, y = -1, w = 0, h = 0;
    int geometry_reds = 0;
    char label[256];
    /* 找插图所在的逻辑页（竖排下插图独占一页）。/ Locate the illustration's logical page
     * (vertical pagination gives an illustration a page of its own). */
    int image_page = -1;
    for (size_t p = 0; p < book_layout_page_count(); ++p) {
        if (book_layout_page_image_count(p) > 0) { image_page = (int)p; break; }
    }
    snprintf(label, sizeof(label), "%s: 插图独占逻辑页（竖排约定）", scenario);
    if (!check(image_page >= 0, label)) return 0;
    snprintf(label, sizeof(label), "%s: page_image_at(%d) 可取到插图记录", scenario, image_page);
    if (!check(book_layout_page_image_at((size_t)image_page, 0, &img, &y, &w, &h) && img == image_index, label))
        return 0;

    /* 附问 A 的结构钉死：竖排下插图恒在页首（记录 y = 0）——build_vertical 对图块
     * 「当前页已用过就另起一页、图后再起一页给正文」，插图永不在文字页中段或页脚；
     * 照片 2 的页脚图形因此不可能是内联图像（设备侧页脚只有 UI 层进度滑轨矩形与
     * 页码/章节名文本，draw_reader_images 也只画正文区插图）。
     * Attached question A, structurally pinned: in vertical setting an illustration always
     * sits at a page start (record y = 0) — build_vertical breaks before and after an image
     * block, so an illustration never lands mid-page or at a text page's footer; photo 2's
     * footer graphic therefore cannot be an inline image (the device footer holds only the
     * UI progress bar and page-number text, and draw_reader_images paints body-area images
     * only). */
    snprintf(label, sizeof(label),
             "%s: 附问 A——插图记录 y = 0（竖排恒在页首，永不在文字页页脚）", scenario);
    check(y == 0, label);

    /* A1 记录层：插图显示宽 ≤ 栏宽。/ A1 record layer: display width <= column width. */
    snprintf(label, sizeof(label),
             "%s: A1 插图记录宽 %d ≤ 栏宽 %d（放置几何不越栏）", scenario, w, COL_W);
    geometry_reds += !check(w <= COL_W, label);
    snprintf(label, sizeof(label),
             "%s: A1 插图记录高 %d ≤ 栏高 %d", scenario, h, BODY_H);
    geometry_reds += !check(h <= BODY_H, label);

    /* A2 遮挡层：复算设备侧绘制契约的绘制矩形。/ A2 occlusion layer: replay the device
     * draw contract's centered rect. draw_reader_images 用 band 居中公式定位图：
     *   int left = band.x + (band.width - image->width) / 2;
     * 本 fixture 以同式复算（band = book_layout_image_rect()，图宽 = 记录宽 w），再断言
     * 绘制矩形包含于本栏 [COL0_X, COL0_X+COL_W]、不与对侧栏 [COL1_X, COL1_X+COL_W] 相交。*/
    EpdRect band = book_layout_image_rect();
    int draw_left = band.x + (band.width - w) / 2;
    int draw_right = draw_left + w;
    snprintf(label, sizeof(label),
             "%s: A2 绘制矩形左缘 %d ≥ 栏左缘 %d（图不画出本栏左界）", scenario, draw_left, COL0_X);
    geometry_reds += !check(draw_left >= COL0_X, label);
    snprintf(label, sizeof(label),
             "%s: A2 绘制矩形右缘 %d ≤ 栏右缘 %d（图不画出本栏右界）",
             scenario, draw_right, COL0_X + COL_W);
    geometry_reds += !check(draw_right <= COL0_X + COL_W, label);
    snprintf(label, sizeof(label),
             "%s: A2 绘制矩形 [%d,%d] 与对侧栏 [%d,%d] 不相交（不遮挡对侧栏）",
             scenario, draw_left, draw_right, COL1_X, COL1_X + COL_W);
    geometry_reds += !check(!rects_intersect_x((EpdRect){draw_left, 0, w, 0}, COL1_X, COL_W), label);

    /* fixture 自身判别力对表：记录宽被栏宽截断必须恰在原生宽越栏时发生（防「大小图
     * 同被截/同不截」——截断失效会让大图回到 900 越栏，误截会把小图抬到 476）。
     * The fixture's own power to tell shapes apart: the clamp to the column width must
     * engage exactly when the native width crosses it (a failed clamp sends the big image
     * back to 900 crossing the column; a wrong clamp lifts the small one to 476). */
    snprintf(label, sizeof(label),
             "%s: 判别力对表——记录宽 %d %s栏宽 %d（原生 %d），截断与否与原生宽越栏与否一致",
             scenario, w, w == COL_W ? "=" : "<", COL_W, native_w);
    check((w == COL_W) == (expect_clamp != 0), label);

    /* 机制登记信息行（非判据）：band 与绘制矩形的实实测值，供判读取数。
     * Mechanism registration line (not an assertion): measured band and draw rect. */
    printf("  [%s] 记录 %dx%d @y=%d（原生 %dx%d）；band x=%d 宽=%d（机制登记：bleed=栏宽）；绘制矩形 [%d,%d]（band 居中公式）；本栏 [%d,%d]；对侧栏 [%d,%d]\n",
           scenario, w, h, y, native_w, native_h, band.x, band.width, draw_left, draw_right,
           COL0_X, COL0_X + COL_W, COL1_X, COL1_X + COL_W);
    return geometry_reds;
}

/* A5：绘制流完整性——照片上「被盖住」与「未绘制」同形，宿主侧以 known text 分形。
 * A5: draw-stream completeness — the two same-looking photo shapes split apart here.
 * drawn 是全部竖列绘制内容的无缝拼接（每列一次 ttf_draw_text_px_vertical，按序 strcat；
 * 列间断点不出现在流里），故与已知文本逐字比对即可判定有没有字形根本没进绘制流。*/
static void check_draw_stream(const char* scenario, const char* text) {
    char label[256];
    snprintf(label, sizeof(label),
             "%s: A5 绘制流非空（vertical_draws=%d rotated_draws=%d）",
             scenario, vertical_draws, rotated_draws);
    if (!check(drawn[0] != 0, label)) return;
    /* 逐字一致：drawn 应恰为已知文本（顺序、字数、内容全覆盖）——「未绘制」类会在此现形。
     * Exact match: drawn must be the known text glyph for glyph — the "never drawn" class
     * shows up right here. */
    snprintf(label, sizeof(label),
             "%s: A5 绘制流与已知文本逐字一致（%zu 字符）", scenario, strlen(text));
    check(strcmp(drawn, text) == 0, label);
    /* 照片 3 的缺字形态签名探针：行首缺 p（picture）与行首缺 th（the eye）。
     * Photo 3's missing-glyph signature probes: leading 'p' of "picture", leading "th" of "the eye". */
    const char* probes[] = {"picture", "the eye"};
    for (unsigned i = 0; i < sizeof(probes) / sizeof(probes[0]); ++i) {
        snprintf(label, sizeof(label),
                 "%s: A5 签名探针——绘制流含完整 %s", scenario, probes[i]);
        check(strstr(drawn, probes[i]) != NULL, label);
    }
    /* 全文逐字在流（含行首字符）：drawn 含 text 的每个字符（与逐字比对互补的粗粒度诊断）。
     * Every glyph of the text appears in the stream, leading glyphs included (coarse
     * diagnostic next to the exact match). */
    size_t missing = 0;
    for (const char* c = text; *c; ++c) {
        if (*c == ' ') continue;
        if (!strchr(drawn, *c)) ++missing;
    }
    snprintf(label, sizeof(label),
             "%s: A5 正文 %zu 字符全部进入绘制流（缺失 %zu）", scenario, strlen(text), missing);
    check(missing == 0, label);
}

/* A5 对表（用户可执行注记①）：照片 3 采用「icture ← picture」「e eye ← the eye」行首缺字
 * 形态作大插图场景的失败特征签名；跑完对表——吻合即复现成功，不吻合则现象面还需再采。
 * A5 cross-table (executable note 1): photo 3 adopts the missing-leading-glyph shapes as
 * the big-illustration scenario's failure signature; after the run, a match confirms the
 * reproduction, a miss means the phenomenon surface needs more sampling.
 * 本函数只分类、不断言：宿主侧形态落在「被盖住」类还是「未绘制」类，由绘制流与几何层
 * 共同判定，打印供判读。/ Classification only, no assertion: the host shape is decided
 * by the draw stream together with the geometry layer, printed for adjudication. */
static void report_signature(const char* scenario, int geometry_red) {
    int complete = drawn[0] != 0 && strcmp(drawn, PHOTO3_TEXT) == 0;
    /* 签名词自身残缺即命中：「icture」在而「picture」不在 = L6 形态；「e eye」在而
     * 「the eye」不在 = L7 形态。/ A hit is the signature word itself broken: "icture"
     * present without "picture" is the L6 shape; "e eye" without "the eye" is L7. */
    int l6 = strstr(drawn, "icture") != NULL && strstr(drawn, "picture") == NULL;
    int l7 = strstr(drawn, "e eye") != NULL && strstr(drawn, "the eye") == NULL;
    printf("  [%s] A5 对表：绘制流%s；几何层%s → ", scenario,
           complete ? "逐字完整" : "有缺字",
           geometry_red ? "见红（越栏/遮挡）" : "全绿（不越栏）");
    if (complete && geometry_red)
        printf("宿主侧形态 = 「被盖住」类（字形已绘制、被插图盖住）——缺字签名未复现，按注记① 不吻合，现象面还需再采\n");
    else if (!complete && (l6 || l7))
        printf("宿主侧形态 = 「未绘制」类，缺字签名%s吻合——按注记① 复现成功\n",
               (l6 && l7) ? "（L6+L7）" : l6 ? "（L6）" : "（L7）");
    else if (!complete)
        printf("宿主侧形态 = 「未绘制」类，但缺字不在签名词内——按注记① 不吻合，现象面还需再采\n");
    else
        printf("宿主侧形态 = 全绿（放置几何合规——G7-A1 修复后的预期落点）\n");
}

/* A6：slots 行口径 fixture 化——「插图页必在无行视图集合」从设备日志推论升级为宿主侧
 * 可机检断言。/ A6: the slots row criterion, fixture-ized — "an illustration page must be
 * in the no-row-view set" upgrades from a device-log inference to host-side assertions.
 *
 * 背景（做法方案 §599）：设备侧 slots 日志行只在 count==0 时打印（prepare_inline_image
 * 的 `if (!count || count > MAX)` 分支），有图页根本不打这行——「47 行全 0」对「有没有
 * 图页」结构性失明，1:1 误设已由用户裁决推翻；检测问题不可再靠设备日志推断。
 * Context (§599): the device slots log line only prints when count == 0, so image pages
 * never print it — log-based detection was structurally blind to image pages; the 1:1
 * assumption is refuted and detection must not rely on device logs again.
 *
 * 本函数逐页绘制并记账地图分类器的两个 O(1) 输入（reader_page_is_joint 的合取项：
 * 页级插图索引与混排槽数）及无行视图集合成员性：
 *   a) 插图记录恰好落在一页，其余页为 0（slots 口径）；
 *   b) 插图页的 draw_page 对绘制流贡献为 0（无行视图集合成员：图由调用方画）；
 *   c) 插图页级索引有效且混排槽为 1——按地图口径不是 JOINT 回退页（JOINT 要求
 *      count==0），与文字页配对成双栏视觉页。
 * This walks the pages, drawing each, and ledgers the map classifier's two O(1) inputs
 * (the conjuncts of reader_page_is_joint: page-level image index and inline slot count)
 * plus no-row-view-set membership. */
static void draw_pages_with_slots_check(uint8_t* fb, EpdRect column, const char* scenario,
                                        int image_index) {
    char label[256];
    int image_pages = 0;
    size_t image_page = 0;
    for (size_t p = 0; p < book_layout_page_count(); ++p)
        if (book_layout_page_image_count(p) > 0) { ++image_pages; image_page = p; }
    snprintf(label, sizeof(label), "%s: A6 slots 口径——恰好一页含插图记录（实得 %d 页）",
             scenario, image_pages);
    if (!check(image_pages == 1, label)) return;

    for (size_t p = 0; p < book_layout_page_count(); ++p) {
        int count = book_layout_page_image_count(p);
        size_t before = strlen(drawn);
        book_layout_draw_page(fb, p, column, FONT_PX);
        size_t contributed = strlen(drawn) - before;
        if (p == image_page) {
            snprintf(label, sizeof(label),
                     "%s: A6 无行视图集合——插图页 %zu（%d 条记录）draw_page 文字贡献为 0（实得 %zu）",
                     scenario, p, count, contributed);
            check(contributed == 0, label);
            snprintf(label, sizeof(label),
                     "%s: A6 地图分类器输入——插图页 %zu 页级索引 = %d、混排槽 = %d（非 JOINT 回退）",
                     scenario, p, book_layout_page_image(p), count);
            check(book_layout_page_image(p) == image_index && count == 1, label);
        } else {
            snprintf(label, sizeof(label),
                     "%s: A6 地图分类器输入——非插图页 %zu 混排槽 = 0（实得 %d）", scenario, p, count);
            check(count == 0, label);
        }
    }
}

int main(void) {
    EpdRect column = {COL0_X, 0, COL_W, BODY_H}; /* = reader_layout_rect_for(body, 2) */
    book_layout_set_vertical(true);              /* 横屏双栏复用竖排分栏 */
    /* G7-A1 修复后接线：出血宽 = 栏宽——镜像 reader_relayout_cols 出口按栏数快照的发布
     * （cols>1 发 build_rect.width；三场景同栏快照，故入口发布一次即等价于逐次发布）。
     * Post-fix wiring: bleed = column width — mirrors the exit's publish at the column
     * snapshot (cols>1 publishes build_rect.width; all three scenarios share one column
     * snapshot, so one publish at the entry is equivalent to per-build publishes). */
    book_layout_set_image_bleed_width(BLEED_W);  /* = COL_W */
    uint8_t fb = 0; /* framebuffer 替身：draw_page 只要求非 NULL，落点由字体桩记录进 drawn */
    int scene1_geom = 0, scene2_geom = 0;
    int scene1_reds = 0, scene2_reds = 0, scene3_reds = 0;
    int reds_before;

    /* ============ 场景 1：大插图（修复验证：通栏大图收进栏内）============ */
    /* Scenario 1: big illustration (fix verification: full-bleed art stays inside the
     * column). 900x600 landscape raster, wider than the 476px column — the G7-A1 shape
     * from photo 3; post-fix the record clamps to the column and the draw rect is the
     * column itself. */
    printf("== 场景 1：大插图（修复验证：通栏大图收进栏内，900x600 横构图，宽于栏宽）==\n");
    reds_before = reds;
    {
        html_text_t doc = {0};
        blk_t blocks[2];
        drawn[0] = 0; vertical_draws = rotated_draws = 0;
        size_t pages = build_illustrated(&doc, blocks, PHOTO3_TEXT, 0, 900, 600, column);
        check(pages >= 2, "大插图：图后另起一页给正文（竖排约定，页数 ≥ 2）");
        scene1_geom = check_geometry("大插图", 0, 900, 600, 1);
        /* 走一遍真实绘制：book_layout_draw_page 只画文字（图由调用方按设备契约画），
         * 但 A5 的 drawn 流必须由它真实产出，不能靠构造；A6 顺带逐页记账 slots 行口径
         * 与无行视图集合成员性。
         * Walk the real draw: draw_page draws the text only (the caller draws the image),
         * but A5's stream must come from it for real, not from construction; A6 ledgers
         * the slots criterion and no-row-view-set membership page by page. */
        draw_pages_with_slots_check(&fb, column, "大插图", 0);
        check_draw_stream("大插图", PHOTO3_TEXT);
        report_signature("大插图", scene1_geom > 0);
        book_layout_free();
        free(doc.utf8); /* 最后一次 draw_page 之后才可释放（s_text 借存指针）*/
    }
    scene1_reds = reds - reds_before;

    /* ============ 场景 2：小插图（正常对照）============ */
    /* Scenario 2: small illustration (normal control). 200x150, narrower than the column. */
    printf("== 场景 2：小插图（正常对照，200x150，窄于栏宽）==\n");
    reds_before = reds;
    {
        html_text_t doc = {0};
        blk_t blocks[2];
        drawn[0] = 0; vertical_draws = rotated_draws = 0;
        size_t pages = build_illustrated(&doc, blocks, PHOTO3_TEXT, 1, 200, 150, column);
        check(pages >= 2, "小插图：图后另起一页给正文（竖排约定，页数 ≥ 2）");
        scene2_geom = check_geometry("小插图", 1, 200, 150, 0);
        draw_pages_with_slots_check(&fb, column, "小插图", 1);
        check_draw_stream("小插图", PHOTO3_TEXT);
        report_signature("小插图", scene2_geom > 0);
        book_layout_free();
        free(doc.utf8);
    }
    scene2_reds = reds - reds_before;

    /* ============ 场景 3：无图（基线）============ */
    /* Scenario 3: no illustration (baseline). Pure text must page exactly as before. */
    printf("== 场景 3：无图（基线）==\n");
    reds_before = reds;
    {
        html_text_t doc = {0};
        blk_t blocks[1];
        size_t text_len = strlen(PHOTO3_TEXT);
        char* buf = malloc(text_len + 1);
        assert(buf);
        memcpy(buf, PHOTO3_TEXT, text_len + 1);
        blocks[0] = (blk_t){.offset = 0, .len = text_len, .image = -1};
        doc.blocks = blocks; doc.count = 1; doc.utf8 = buf; doc.len = text_len;
        drawn[0] = 0; vertical_draws = rotated_draws = 0;
        assert(book_layout_build_blocks(buf, doc.len, blocks, 1, column, FONT_PX));
        check(book_layout_page_image_count(0) == 0, "无图：无任何插图记录");
        /* 无图时插图路径不会被调用，book_layout_image_rect() 的返回值对本场景无意义，
         * 故不设区域层断言。/ With no illustration the image path never runs, so the
         * region query is meaningless here and no region-layer assertion is made. */
        /* A6 基线：逐页混排槽为 0（slots 口径的对照面），再逐页绘制。
         * A6 baseline: every page carries zero inline slots (the control face of the
         * slots criterion); then draw page by page. */
        for (size_t p = 0; p < book_layout_page_count(); ++p) {
            char label[128];
            snprintf(label, sizeof(label), "无图：A6 第 %zu 页混排槽 = 0", p);
            check(book_layout_page_image_count(p) == 0, label);
            book_layout_draw_page(&fb, p, column, FONT_PX);
        }
        check_draw_stream("无图", PHOTO3_TEXT);
        report_signature("无图", 0);
        book_layout_free();
        free(buf);
    }
    scene3_reds = reds - reds_before;

    book_layout_set_vertical(false);
    book_layout_set_image_bleed_width(0);

    /* 形态分流的退出码（runner 契约，G7-A1 修复后口径）：
     * 0 = 全绿（三场景放置几何合规——修复后的预期落点，runner 直跑通过）；
     * 1 = 大插图场景几何层见红且对照/基线全绿（修复不完整，或布局层回归把大图放回越栏）；
     * 2 = 意外形态（其余任何红点分布：对照/基线见红 = 新缺陷；大图红点不在几何层 =
     *     非预期形态，照新缺陷拦）。
     * Exit-code contract for the runner (post-fix reading): 0 all green (the expected
     * landing spot after the fix, the runner passes straight through); 1 red in the big
     * scenario's geometry layer with control and baseline green (an incomplete fix, or a
     * layout-layer regression putting the big image back across the column); 2 an
     * unexpected shape (any other red pattern: red in the control or baseline = new
     * defect; big-scenario red outside the geometry layer = unexpected form). */
    printf("twin_image: %d checks, %d red（大图：几何红 %d / 场景总红 %d；小图：场景总红 %d；无图：场景总红 %d）\n",
           checks, reds, scene1_geom, scene1_reds, scene2_reds, scene3_reds);
    if (reds == 0) {
        printf("twin_image_host_test: PASS（宿主侧三场景全绿）\n");
        return 0;
    }
    if (scene1_geom > 0 && scene2_reds == 0 && scene3_reds == 0) {
        printf("twin_image_host_test: FIX_INCOMPLETE（大插图场景几何层仍见红，对照/基线全绿——修复不完整或布局层回归）\n");
        return 1;
    }
    printf("twin_image_host_test: UNEXPECTED（红点分布非预期：对照/基线见红 = 新缺陷；大图红点不在几何层 = 非预期形态）\n");
    return 2;
}
