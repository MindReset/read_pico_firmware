#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""中文：视觉页地图（page_map）的寻址互证测试。
English: Addressing cross-check test for the visual page map (page_map).

背景 / Why: P0-1 在 app_book.c 落地视觉页地图模块：视觉页 → {起始逻辑页, 栏模式}，
寻址 API（count/index/start/mode/logical_pages/for_offset/clamp）全部经它。模块有两条
寻址路径——建图数组与退化公式（s_page_map_degenerate），方案要求两条路径等价，
且单栏下所有结果与旧逻辑页逐位一致（互证测试①）。

本测试从 app_book.c 抽取整个地图区块，配确定性页表桩编译运行：

  - 公式路径 vs 建图路径：同一 fixture 两遍快照，摘要必须一致；
  - 独立参考模型（Python 侧按方案规则重新实现一遍走线/配对/相邻规则）逐行校验
    调用流，防「两边同错」；
  - 开形态命名断言（§9.2 事例表：覆盖性 / 配对性 / JOINT 独占 / 末页落单），判据
    直接来自 fixture 规格、不经过参考模型，兜底「模型与实现同错」；
  - 原始地图数组平铺校验：条目严格递增、页覆盖无洞无重叠；
  - 失步退化：布局代次变了而地图没重建 → 退化公式 + 只告警一次，绝不静默错页；
  - 边界：空布局、越界下标/偏移、末页落单、JOINT 相邻、4096 页上限；
  - 确定性：整套跑两遍摘要一致。

state 行走明文 stdout 但不进摘要，便于独立模型判定每条快照走的是哪条路径；
摘要只覆盖寻址结果本身，公式/建图两条路径因此可直接比对。
The state lines print to stdout but stay out of the digest so the independent model can
tell which path each snapshot took; the digest covers addressing results only, which is
what makes the formula and built-map snapshots directly comparable.

用法 / Usage:
    python3 tools/reader_page_map_host_test.py           # 比对黄金值 + 模型校验
    python3 tools/reader_page_map_host_test.py --regen   # 重签黄金值（仅在有意变更寻址时）
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / 'main' / 'apps' / 'app_book.c'
HERE = Path(__file__).resolve().parent
GOLDEN = HERE / 'reader_page_map_golden.txt'
STREAM = HERE / 'reader_page_map_stream.txt'

# 地图区块的起止锚点：区块横幅 + 紧随其后的第一个调用者（draw_reader_stats）签名行。
# 模块必须排在所有调用点之前（C 先定义后使用），故尾锚点不再是 ensure_body_orientation
# 注释，而是搬位后紧跟模块的那一行；抽取内容与搬位前逐字节一致。
# Block anchors: the section banner and the signature line of its first caller
# (draw_reader_stats). The module must precede every call site (C define-before-use),
# so the tail anchor is no longer the ensure_body_orientation comment but the line that
# follows the relocated module; the extracted text stays byte-identical to pre-move.
MAP_HEAD = '/* ---- 视觉页地图 / Visual page map ---- */'
MAP_TAIL = 'static void draw_reader_stats(uint8_t* fb) {'
EXPECT_FUNCTIONS = ['reader_page_is_joint', 'reader_rebuild_page_map', 'reader_map_check_sync',
                    'reader_visual_count_checked', 'reader_visual_entry', 'reader_visual_count',
                    'reader_visual_index', 'reader_visual_start', 'reader_visual_mode',
                    'reader_logical_pages', 'reader_visual_for_offset', 'reader_clamp_visual_page']

MODE_COLUMNS2 = 0
MODE_JOINT = 1
MODEL_LINE = re.compile(r'^(count=|index logical=|clamp logical=|start visual=|mode visual=|pages visual=|offset off=|map \d+ )')


def map_block(text):
    """抽取地图区块全文；区块内函数名漂移立即报错。
    Extract the whole map block; any drift in its function names fails loudly."""
    i = text.index(MAP_HEAD)
    j = text.index(MAP_TAIL, i)
    block = text[i:j]
    for name in EXPECT_FUNCTIONS:
        assert re.search(r'\b' + name + r'\(', block), name
    return block


PRELUDE = r'''
/* 自包含测试前奏：类型、日志桩、book_layout 页表桩原型。不包含生产头。
   Self-contained prelude: types, log stub and book_layout page-table prototypes. */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char* TAG = "book";

/* 日志桩：只记次数与最后一条文本，不打印，保持 stdout 纯调用流。
   Log stub: counts calls and keeps the last message; nothing is printed. */
static int test_warn_count;
static char test_warn_last[192];
static void test_logw(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(test_warn_last, sizeof(test_warn_last), fmt, ap);
    va_end(ap);
    ++test_warn_count;
}
#define ESP_LOGW(tag, fmt, ...) test_logw(fmt, ##__VA_ARGS__)

/* book_layout 页表桩：页型由测试装入，代次可手动推进（模拟未重建的重排）。
   book_layout page-table stub: page kinds are loaded by the test and the generation
   can be advanced by hand (a reflow without a rebuild). */
size_t book_layout_page_count(void);
int book_layout_page_image(size_t page);
int book_layout_page_image_count(size_t page);
uint32_t book_layout_generation(void);
size_t book_layout_page_for_offset(size_t off);
'''

MAIN = r'''
/* 页表桩实现：页 k 覆盖字节 [k*100, (k+1)*100)，偏移越界夹末页。
   Page-table stub: page k owns bytes [k*100, (k+1)*100); excessive offsets clamp. */
static size_t test_page_count;
static int test_page_image[READER_PAGE_MAX];
static int test_page_image_slots[READER_PAGE_MAX];
static uint32_t test_generation = 1;

size_t book_layout_page_count(void) { return test_page_count; }
int book_layout_page_image(size_t page) { return page < test_page_count ? test_page_image[page] : -1; }
int book_layout_page_image_count(size_t page) {
    return page < test_page_count ? test_page_image_slots[page] : 0;
}
uint32_t book_layout_generation(void) { return test_generation; }
size_t book_layout_page_for_offset(size_t off) {
    if (!test_page_count) return 0;
    size_t page = off / 100;
    return page < test_page_count ? page : test_page_count - 1;
}

/* 页型：'.' 纯文字；'i' 页内混排插图（image>=0 且 slots>0，普通页）；'I' 整页插图（JOINT）。
   Page kinds: '.' plain text; 'i' inline images (image>=0 with slots>0, an ordinary
   page); 'I' whole-page image (JOINT). */
static void fixture_load(const char* spec) {
    test_page_count = strlen(spec);
    for (size_t k = 0; k < test_page_count; ++k) {
        test_page_image[k] = spec[k] == '.' ? -1 : spec[k] == 'i' ? 5 : 7;
        test_page_image_slots[k] = spec[k] == 'i' ? 1 : 0;
    }
}

/* FNV-1a 64 位摘要：整条调用流原文逐字节喂入，跨编译器无填充差异。
   FNV-1a 64 digest over the emitted stream text byte by byte. */
static unsigned long long s_hash = 1469598103934665603ULL;
static void hash_reset(void) { s_hash = 1469598103934665603ULL; }
static unsigned long long hash_value(void) { return s_hash; }
static void emit(const char* fmt, ...) {
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    fputs(line, stdout);
    for (const char* p = line; *p; ++p) {
        s_hash ^= (unsigned char)*p;
        s_hash *= 1099511628211ULL;
    }
}

/* 全量快照：所有公开入口 + 越界参数 + 原始地图数组。
   Full snapshot: every public entry, out-of-range arguments and the raw map array. */
static void snapshot(const char* tag) {
    emit("== %s\n", tag);
    size_t n = book_layout_page_count();
    size_t count = reader_visual_count();
    emit("count=%zu\n", count);
    for (size_t k = 0; k < n + 2; ++k) {
        emit("index logical=%zu -> %zu\n", k, reader_visual_index(k));
        emit("clamp logical=%zu -> %zu\n", k, reader_clamp_visual_page(k));
    }
    for (size_t v = 0; v < count + 2; ++v) {
        size_t left = 0, right = 0;
        reader_logical_pages(v, &left, &right);
        char rightbuf[32];
        if (right == SIZE_MAX) snprintf(rightbuf, sizeof(rightbuf), "none");
        else snprintf(rightbuf, sizeof(rightbuf), "%zu", right);
        emit("start visual=%zu -> %zu\n", v, reader_visual_start(v));
        emit("mode visual=%zu -> %d\n", v, reader_visual_mode(v));
        emit("pages visual=%zu -> left=%zu right=%s\n", v, left, rightbuf);
    }
    static const size_t offsets[] = {0, 1, 99, 100, 250, 999, 999999};
    for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i)
        emit("offset off=%zu -> %zu\n", offsets[i], reader_visual_for_offset(offsets[i]));
    for (size_t i = 0; i < s_page_map_count; ++i)
        emit("map %zu start=%u mode=%u\n", i, s_page_map[i].start, s_page_map[i].mode);
}

/* 独立平铺校验：每个逻辑页恰好属于一个条目（条目 i 覆盖 [start, 下一条 start) 或到末页），
   条目严格递增且不越界。与建图走线无关，专门抓"洞/重叠/乱序"。
   Independent tiling check: every logical page belongs to exactly one entry (entry i
   covers [start, next start) or up to the last page), entries strictly increase and
   stay in range. Independent of the build walk; catches holes, overlaps and disorder. */
static int check_tiling(size_t n) {
    if (!n) return s_page_map_count == 0;
    if (!s_page_map_count) return 0;
    for (size_t i = 0; i < s_page_map_count; ++i) {
        if (s_page_map[i].start >= n) return 0;
        if (i && s_page_map[i].start <= s_page_map[i - 1].start) return 0;
    }
    for (size_t p = 0; p < n; ++p) {
        int owner = 0;
        for (size_t i = 0; i < s_page_map_count; ++i) {
            size_t end = i + 1 < s_page_map_count ? s_page_map[i + 1].start : n;
            if (s_page_map[i].start <= p && p < end) { owner = 1; break; }
        }
        if (!owner) return 0;
    }
    return 1;
}

/* 4096 页上限：只打边界与平铺摘要，避免调用流爆炸。
   The 4096-page cap: print only boundaries and the tiling summary. */
static void big_case(void) {
    test_page_count = READER_PAGE_MAX;
    for (size_t k = 0; k < test_page_count; ++k) {
        test_page_image[k] = (k == 0 || k == READER_PAGE_MAX - 1) ? 7 : -1;
        test_page_image_slots[k] = 0;
    }
    reader_rebuild_page_map(2);
    emit("== big col2\n");
    emit("count=%zu\n", reader_visual_count());
    emit("index logical=0 -> %zu\n", reader_visual_index(0));
    emit("index logical=%zu -> %zu\n", READER_PAGE_MAX - 1, reader_visual_index(READER_PAGE_MAX - 1));
    emit("start visual=0 -> %zu\n", reader_visual_start(0));
    emit("start visual=last -> %zu\n", reader_visual_start(reader_visual_count() - 1));
    emit("clamp logical=last -> %zu\n", reader_clamp_visual_page(READER_PAGE_MAX - 1));
    emit("tile=%d\n", check_tiling(READER_PAGE_MAX));
    reader_rebuild_page_map(1);
    emit("== big col1\n");
    emit("count=%zu\n", reader_visual_count());
    emit("tile=%d\n", check_tiling(READER_PAGE_MAX));
}

static int run_fixture(const char* spec, int columns) {
    char tag[96];
    snprintf(tag, sizeof(tag), "fixture[%s] col%d", spec, columns);
    fixture_load(spec);
    reader_rebuild_page_map(columns);
    printf("state columns=%d degenerate=%d\n", s_page_map_columns, s_page_map_degenerate ? 1 : 0);
    hash_reset();
    snapshot(tag);
    unsigned long long built = hash_value();
    // 强制数组路径再拍一遍：无 JOINT 页（或单栏）时公式与建图必须逐位一致。
    // Shoot the array path again: with no JOINT pages (or single column) the formula
    // and the built map must agree bit for bit.
    s_page_map_degenerate = false;
    printf("state columns=%d degenerate=%d\n", s_page_map_columns, s_page_map_degenerate ? 1 : 0);
    hash_reset();
    snapshot(tag);
    if (built != hash_value()) {
        fprintf(stderr, "formula/array MISMATCH: %s\n", tag);
        return 1;
    }
    if (!check_tiling(test_page_count)) {
        fprintf(stderr, "tiling FAIL: %s\n", tag);
        return 1;
    }
    return 0;
}

/* 失步退化：代次推进而地图没重建 → 退化公式 + 只告警一次。
   Stale degradation: the generation moved without a rebuild → formula fallback and
   exactly one warning. */
static int stale_case(void) {
    fixture_load("..I..");
    reader_rebuild_page_map(2);
    printf("state columns=%d degenerate=%d\n", s_page_map_columns, s_page_map_degenerate ? 1 : 0);
    hash_reset();
    snapshot("stale_before");
    ++test_generation;   // 模拟一次没有重建地图的重排 / a reflow without a map rebuild
    test_warn_count = 0;
    (void)reader_visual_count();   // 触发失步判定：退化 + 告警一次 / trigger the sync check
    printf("state columns=%d degenerate=%d\n", s_page_map_columns, s_page_map_degenerate ? 1 : 0);
    if (test_warn_count != 1 || !s_page_map_degenerate) {
        fprintf(stderr, "stale degradation FAIL: warn=%d degenerate=%d\n",
                test_warn_count, s_page_map_degenerate ? 1 : 0);
        return 1;
    }
    emit("stale warn=%d text=%s\n", test_warn_count, test_warn_last);
    hash_reset();
    snapshot("stale_after");
    printf("state columns=%d degenerate=%d\n", s_page_map_columns, s_page_map_degenerate ? 1 : 0);
    hash_reset();
    snapshot("stale_again");
    if (test_warn_count != 1) {
        fprintf(stderr, "stale warning must fire once: warn=%d\n", test_warn_count);
        return 1;
    }
    emit("stale warn_again=%d\n", test_warn_count);
    return 0;
}

static const char* const FIXTURES[] = {
    "", ".", "..", ".....", "........",                    // 空、单页、奇偶纯文字
    "....I", "..I..", ".I.", "I...", "III", "...I..",      // JOINT：尾部/中间/相邻/首页/全JOINT
    "..i..i..", "iIi", "IiI",                             // 混排插图是普通页，不是 JOINT
    ".......I...........I.......I................",        // 长章多处 JOINT
};

/* 开形态（双栏）事例表断言：方案 §9.2 的覆盖性 / 配对性 / JOINT 独占 / 末页落单逐条
   命名校验。判据直接来自 fixture 规格（哪些页是整页插图），不经过参考模型——参考
   模型与实现两边同错的场景由这组断言兜底。输出走 printf 不进摘要：关形态参照仍是
   冻结 digest，开形态断言是第 2 步随视觉页化新增的命名防线。
   On-form (two-column) case-table assertions: the section 9.2 properties — coverage,
   pairing, JOINT exclusivity and a lone tail page — checked by name. The criteria come
   straight from the fixture spec (which pages are whole-page images), not through the
   reference model, so a shared misreading on both sides still gets caught. Printed, not
   emitted: the off-form reference stays the frozen digest; these are named guards added
   with the step-2 visual-page work. */
static int check_onform(const char* spec) {
    fixture_load(spec);
    reader_rebuild_page_map(2);
    size_t n = test_page_count;
    bool ok = true;
    // ① 覆盖性：条目严格递增、首条从 0 起、每条正好覆盖到下一试 start 或 n，无洞无重叠；
    //    每条目的跨度按建图规则从 spec 直接推出（JOINT 占 1 页，普通页在后页非 JOINT 时占 2 页）。
    //    Coverage: strictly increasing entries, the first starting at 0, each covering
    //    exactly up to the next start or n — no holes or overlaps; each entry's span comes
    //    straight from the spec (a JOINT page takes one page; an ordinary page takes two
    //    when the next page is not JOINT).
    if (n) {
        if (!s_page_map_count || s_page_map[0].start != 0) ok = false;
        for (size_t i = 0; ok && i < s_page_map_count; ++i) {
            size_t start = s_page_map[i].start;
            if (start >= n) { ok = false; break; }
            if (i && start <= s_page_map[i - 1].start) { ok = false; break; }
            size_t span = spec[start] == 'I' ? 1 :
                          (start + 1 < n && spec[start + 1] != 'I' ? 2 : 1);
            size_t end = i + 1 < s_page_map_count ? s_page_map[i + 1].start : n;
            if (end != start + span) ok = false;
        }
    } else if (s_page_map_count) {
        ok = false;
    }
    // ② JOINT 独占：每个整页插图页自成一条 JOINT 条目（start 即其自身）。
    //    JOINT exclusivity: every whole-page image gets its own JOINT entry starting at itself.
    for (size_t k = 0; ok && k < n; ++k) {
        if (spec[k] != 'I') continue;
        bool own = false;
        for (size_t i = 0; i < s_page_map_count; ++i)
            if (s_page_map[i].start == k && s_page_map[i].mode == READER_VISUAL_MODE_JOINT) own = true;
        if (!own) ok = false;
    }
    // ③ 配对性与末页落单：COLUMNS2 条目右栏存在 ⟺ 下一逻辑页是普通页；末页落单（最后
    //    一条目是 COLUMNS2 且 start 即末页）时右栏为 none。/ Pairing and the lone tail: a
    //    COLUMNS2 entry has a right column iff the next logical page is ordinary; a lone
    //    tail (the last entry is COLUMNS2 starting at the last page) leaves the right side
    //    absent.
    for (size_t v = 0; ok && v < s_page_map_count; ++v) {
        if (s_page_map[v].mode != READER_VISUAL_MODE_COLUMNS2) continue;
        size_t s = s_page_map[v].start;
        size_t left = 0, right = 0;
        reader_logical_pages(v, &left, &right);
        bool want_right = s + 1 < n && spec[s + 1] != 'I';
        if (left != s) ok = false;
        if (want_right != (right != SIZE_MAX)) ok = false;
    }
    printf("onform fixture[%s] col2 ok=%d\n", spec, ok ? 1 : 0);
    return ok ? 0 : 1;
}

static int run_all(void) {
    for (size_t i = 0; i < sizeof(FIXTURES) / sizeof(FIXTURES[0]); ++i)
        for (int columns = 1; columns <= 2; ++columns)
            if (run_fixture(FIXTURES[i], columns)) return 1;
    // 开形态命名断言随行：判据来自规格、不走模型，输出不进摘要。/ On-form named
    // assertions ride along: spec-grounded, model-free, and out of the digest.
    for (size_t i = 0; i < sizeof(FIXTURES) / sizeof(FIXTURES[0]); ++i)
        if (check_onform(FIXTURES[i])) return 1;
    if (stale_case()) return 1;
    big_case();
    return 0;
}

int main(void) {
    setbuf(stdout, NULL);
    if (run_all()) return 1;
    unsigned long long first = hash_value();
    // 确定性：同样输入再跑一遍，摘要必须一致。/ Determinism: a second run must match.
    hash_reset();
    if (run_all()) return 1;
    if (first != hash_value()) {
        fprintf(stderr, "determinism FAIL\n");
        return 1;
    }
    emit("digest=%016llx\n", hash_value());
    puts("reader_page_map: PASS");
    return 0;
}
'''


def build_unit():
    src = SOURCE.read_text(encoding='utf-8')
    return PRELUDE + map_block(src) + MAIN


def run_c(unit):
    with tempfile.TemporaryDirectory() as tmp:
        c = Path(tmp) / 'page_map.c'
        exe = Path(tmp) / 'page_map'
        c.write_text(unit, encoding='utf-8')
        subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-variable', '-Wno-unused-function',
                        '-fsanitize=address,undefined', str(c), '-o', str(exe)],
                       check=True)
        proc = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
        return proc.stdout


# ---- 独立参考模型：按方案规则重新实现走线、配对、相邻与退化公式。----
# ---- Independent reference model: the walk, pairing, adjacency and fallback formula
#      re-implemented from the plan's rules. ----


def build_entries(pages, columns):
    n = len(pages)
    joint = [k == 'I' for k in pages]
    if columns <= 1:
        return [(k, MODE_COLUMNS2) for k in range(n)]
    entries = []
    k = 0
    while k < n:
        if joint[k]:
            entries.append((k, MODE_JOINT))
            k += 1
            continue
        step = 2 if (k + 1 < n and not joint[k + 1]) else 1
        entries.append((k, MODE_COLUMNS2))
        k += step
    return entries


def predict(pages, columns, degenerate):
    """返回预期调用流（与 C 快照同序同格式的文本行列表）。
    Return the expected stream: the same lines, in the same order, as the C snapshot."""
    n = len(pages)
    entries = build_entries(pages, columns)
    built_degenerate = columns <= 1 or not any(k == 'I' for k in pages)
    deg = built_degenerate if degenerate is None else bool(degenerate)
    lines = []
    if n == 0:
        count = 0
    elif deg:
        count = n if columns <= 1 else (n + 1) // 2
    else:
        count = len(entries)
    lines.append('count=%d' % count)

    def index_of(logical):
        if n == 0:
            return 0
        k = min(logical, n - 1)
        if deg:
            return k if columns <= 1 else k // 2
        idx = 0
        for i, (start, _mode) in enumerate(entries):
            if start <= k:
                idx = i
            else:
                break
        return idx

    def start_of(visual):
        if count == 0:
            return 0
        v = min(visual, count - 1)
        if deg:
            s = v if columns <= 1 else v * 2
            return min(s, n - 1)
        return entries[v][0]

    def mode_of(visual):
        if count == 0:
            return MODE_COLUMNS2
        if deg:
            return MODE_COLUMNS2
        return entries[min(visual, count - 1)][1]

    def right_of(visual):
        if n == 0 or columns <= 1:
            return None
        v = min(visual, count - 1)
        if mode_of(visual) != MODE_COLUMNS2:
            return None
        start = start_of(visual)
        if deg:
            return start + 1 if start + 1 < n else None
        nxt = entries[v + 1][0] if v + 1 < len(entries) else None
        return start + 1 if (start + 1 < n and nxt != start + 1) else None

    for k in range(n + 2):
        lines.append('index logical=%d -> %d' % (k, index_of(k)))
        lines.append('clamp logical=%d -> %d' % (k, start_of(index_of(k))))
    for v in range(count + 2):
        right = right_of(v)
        lines.append('start visual=%d -> %d' % (v, start_of(v)))
        lines.append('mode visual=%d -> %d' % (v, mode_of(v)))
        lines.append('pages visual=%d -> left=%d right=%s' % (
            v, start_of(v), 'none' if right is None else right))
    for off in (0, 1, 99, 100, 250, 999, 999999):
        page = 0 if n == 0 else min(off // 100, n - 1)
        lines.append('offset off=%d -> %d' % (off, index_of(page)))
    for i, (start, mode) in enumerate(entries):
        lines.append('map %d start=%d mode=%d' % (i, start, mode))
    return lines


def parse_blocks(stream):
    """把调用流切成 (state 行, tag, 模型行列表)；非模型行不属于任何块。
    Split the stream into (state line, tag, model lines); other lines belong to no block."""
    blocks = []
    state = None
    for line in stream.splitlines():
        if line.startswith('state '):
            state = line
        elif line.startswith('== '):
            blocks.append((state, line[3:], []))
        elif blocks and MODEL_LINE.match(line):
            blocks[-1][2].append(line)
    return blocks


def check_model(stream):
    """用独立参考模型逐块校验快照；返回不一致的说明列表。
    Validate every snapshot block against the reference model."""
    problems = []
    for state, tag, lines in parse_blocks(stream):
        if state is None:
            problems.append('%s: missing state line' % tag)
            continue
        columns = int(re.search(r'columns=(\d+)', state).group(1))
        degenerate = int(re.search(r'degenerate=(\d+)', state).group(1))
        spec = re.search(r'fixture\[([.*iI]*)\]', tag)
        if spec is not None:
            pages = spec.group(1)
        elif tag in ('stale_before', 'stale_after', 'stale_again'):
            pages = '..I..'
        else:
            continue   # big 用例只打边界，交给黄金值冻结 / big cases only print boundaries
        expected = predict(pages, columns, degenerate)
        if expected != lines:
            for want, got in zip(expected, lines):
                if want != got:
                    problems.append('%s: expected %r, got %r' % (tag, want, got))
                    break
            if len(expected) != len(lines):
                problems.append('%s: expected %d lines, got %d' % (tag, len(expected), len(lines)))
    return problems


def check_tiling_lines(stream):
    """校验每个地图区块的平铺性质（严格递增、模式合法）。
    Check tiling for every map block: strictly increasing starts and legal modes."""
    problems = []
    entries = []

    def flush():
        starts = [e[1] for e in entries]
        if starts != sorted(starts) or len(set(starts)) != len(starts):
            problems.append('map entries not strictly increasing: %s' % starts)
        for _i, _start, mode in entries:
            if mode not in (MODE_COLUMNS2, MODE_JOINT):
                problems.append('map entry has bad mode %d' % mode)

    for line in stream.splitlines():
        m = re.match(r'map (\d+) start=(\d+) mode=(\d+)$', line)
        if m:
            entries.append((int(m.group(1)), int(m.group(2)), int(m.group(3))))
            continue
        if entries:
            flush()
            entries = []
    if entries:
        flush()
    return problems


def main():
    regen = '--regen' in sys.argv
    out = run_c(build_unit())
    stream = '\n'.join(line for line in out.splitlines()
                       if not line.startswith(('digest=', 'reader_page_map:')))
    problems = check_model(stream) + check_tiling_lines(stream)
    if problems:
        print('reader page map model MISMATCH:')
        for p in problems[:20]:
            print('  ' + p)
        raise SystemExit(1)
    digest = next(line for line in out.splitlines() if line.startswith('digest=')).split('=', 1)[1]
    if regen:
        GOLDEN.write_text('digest=%s\n' % digest, encoding='utf-8')
        STREAM.write_text(stream + '\n', encoding='utf-8')
        print('reader_page_map: golden frozen -> %s (%s)' % (GOLDEN.name, digest))
        return
    golden = GOLDEN.read_text(encoding='utf-8').strip()
    if golden != 'digest=%s' % digest:
        print('reader page map stream CHANGED: golden %s, now %s' % (golden, digest))
        print('--- current stream (run with --regen only after an intentional addressing change) ---')
        print(stream)
        raise SystemExit(1)
    print('reader_page_map: PASS (%s)' % digest)


if __name__ == '__main__':
    main()
