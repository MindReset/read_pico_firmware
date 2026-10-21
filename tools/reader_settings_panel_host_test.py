#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""中文：阅读设置面板的竖屏调用流基线 + 行表驱动行为用例。
English: Portrait draw-call-stream baseline and table-driven behavior cases for the reading settings panel.

背景 / Why: 阶段 0 把面板行几何从硬编码改成行表驱动（绘制锚点、热区、kind 派发同一事实源）。
这次重构只许改变「坐标从哪来」，不许改变「画什么」。本测试把 draw_reading_settings()
的整段绘制原语调用流（函数 + 全部参数 + 文本，按序）做 FNV-1a 摘要：

  - 黄金值在表格化之前的树上用 --regen 冻结并签入；
  - 之后每次运行都比对：摘要不一致 = 面板渲染被改坏，立即红。

宿主层无法真栅化字形（仓库惯例是桩掉字体），调用流摘要比「无字像素缓冲」更严格：
同序、同参、同文 = 同渲染。选择「竖屏」是因为面板在横屏阅读时也始终按竖屏几何绘制
（render() 的旋转决策只放行纯正文与点词卡，见 app_book.c 的 want 计算）。

阶段 0b 起同一单元还跑热区行为用例（reading_settings_action）：
逐行点中点断言分发（全表格驱动，不硬编码行索引）、Done 关闭、行间间隙无动作、
四张固定卡片回归。apply_reader_option / reader_manual_refresh / invalidate_prep
用记录桩，断言的是「行 → kind / 子面板」的映射本身。

用法 / Usage:
    python3 tools/reader_settings_panel_host_test.py           # 比对黄金值 + 行为用例
    python3 tools/reader_settings_panel_host_test.py --regen   # 重签黄金值（仅在有意变更面板时）
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / 'main' / 'apps' / 'app_book.c'
HERE = Path(__file__).resolve().parent
GOLDEN = HERE / 'reader_settings_panel_golden.txt'
STREAM = HERE / 'reader_settings_panel_stream.txt'

# 阶段 0b：行模型（常量/类型/表）+ 绘制侧 + 热区侧一并抽取，顺序即 C 编译序。
# Phase 0b extracts the row model (defines/typedef/table) plus the draw side and the
# hot-zone side together; the order below is the C declaration order.
# 归因：r91 把阅读设置改成半屏双列卡片，READING_ROW_TOP/PITCH（单栏行几何）随之消失，
# 改由 reading_setting_rect() 计算；行表新增 READING_ROW_FULL/WAVE/TURN 三个非开关槽常量。
# Attribution: r91 turned the reading settings into a two-column card sheet, so the
# single-column ROW_TOP/PITCH constants are gone and geometry comes from
# reading_setting_rect(); the table gained the FULL/WAVE/TURN non-toggle slot constants.
MODEL_DEFINES = ['READING_ROW_KEYS', 'READING_ROW_FULL', 'READING_ROW_WAVE',
                 'READING_ROW_TURN', 'READING_ROW_COUNT', 'READING_ROW_COLS',
                 'READING_ROW_HEIGHT']
# 归因：draw_reading_keys_row / draw_reading_rows / reading_done_rect 随上游卡片壳移除
# （按键控制改为卡片槽、「完成」改回固定矩形），补上新壳的四个槽位绘制函数。
# Attribution: draw_reading_keys_row / draw_reading_rows / reading_done_rect went away with
# upstream's card shell (the keys entry became a slot and Done a fixed rect); the shell's
# slot-drawing functions take their place.
EXTRACT = ['reading_row_count', 'reading_row_on', 'reading_setting_rect',
           'reading_settings_top', 'reading_toggle_rect',
           'draw_reading_card', 'draw_reading_toggle', 'draw_sheet',
           'draw_reading_settings', 'reading_settings_action']


def mask(text):
    """掩码注释，保留字符串与代码，便于花括号配对。
    Mask comments, keep strings and code so brace matching stays sane."""
    tokens = r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    return re.sub(tokens, lambda m: ' ' * len(m[0]) if m[0].startswith(('//', '/*')) else m[0], text)


def function(text, signature):
    """按花括号配对抽取函数全文；注释先掩码，避免注释里的引号/花括号干扰。
    Extract a whole function by brace matching; comments are masked first so quotes
    and braces inside comments cannot confuse the scan."""
    scan = mask(text)
    found = re.search(r"^(?:static )?[^\n]+\b" + signature + r"\([^;{}]*?\)\s*\{", text, re.M)
    assert found, signature
    start, at, depth, quote, escape = found.start(), found.end(), 1, None, False
    while depth:
        c = scan[at]
        if quote:
            if escape:
                escape = False
            elif c == '\\':
                escape = True
            elif c == quote:
                quote = None
        elif c in '"\'':
            quote = c
        elif c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
        at += 1
    return text[start:at]


def define(text, name):
    """抽取一条 #define（整行，含任何尾注释）。/ Extract one #define line."""
    m = re.search(r'^#define\s+' + name + r'\b[^\n]*$', text, re.M)
    assert m, name
    return m.group(0)


def declaration(text, head, tail):
    """按起止标记抽取声明（typedef 等）；先定位唯一的尾标记，再向前找最近的头标记。
    Extract a declaration by head/tail markers: anchor on the unique tail first, then
    walk back to the nearest head, so other same-shaped declarations cannot match."""
    scan = mask(text)
    j = scan.index(tail)
    i = scan.rindex(head, 0, j)
    return text[i:j + len(tail)]


def initializer(text, head):
    """抽取带花括号初始化器的变量定义（表内字符串不含花括号）。
    Extract a variable definition with a brace initializer (no braces inside the strings)."""
    scan = mask(text)
    i = scan.index(head)
    at, depth = scan.index('{', i), 0
    while True:
        if scan[at] == '{':
            depth += 1
        elif scan[at] == '}':
            depth -= 1
            if depth == 0:
                break
        at += 1
    return text[i:text.index(';', at) + 1]


PRELUDE = r'''
/* 自包含测试前奏：类型、常量、记录桩、设置替身。不包含生产头，避免拖入字体/资产依赖。
   Self-contained prelude: types, constants, recording stubs and settings fakes. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct { int x, y, width, height; } EpdRect;
enum EpdFontFlags { EPD_DRAW_ALIGN_LEFT, EPD_DRAW_ALIGN_CENTER, EPD_DRAW_ALIGN_RIGHT };
/* 归因：r91 的循环卡片用 UI_ICON_ROTATE_CW（旋转箭头）标示可取值的槽位。
   Attribution: r91 marks cycling cards with UI_ICON_ROTATE_CW. */
typedef enum { UI_ICON_ROTATE_CW = 98, UI_ICON_RADIO_TOWER = 99,
               UI_ICON_CHEVRON_RIGHT = 100 } ui_icon_t;
#define UI_GRAY_WHITE 0xFF
#define UI_GRAY_BLACK 0x00
/* 面板在横屏阅读时也按竖屏几何绘制（render() 的旋转决策只放行纯正文与点词卡），
   所以这里取竖屏常量；生产侧的动态值来自 ui_kit.h 的 epd_rotated_display_*()。
   The panel always draws with portrait geometry even during landscape reading, so the
   portrait constants are correct here; production gets them dynamically from ui_kit.h. */
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216

/* 面板枚举与宿主状态：与生产同序同值。/ Panel enum and host state mirror production. */
typedef enum {
    READER_PANEL_NONE,
    READER_PANEL_TOOLS,
    READER_PANEL_FONT_SETTINGS,
    READER_PANEL_LAYOUT_SETTINGS,
    READER_PANEL_RULE_SETTINGS,
    READER_PANEL_FONT_PICKER,
    READER_PANEL_STATS,
    READER_PANEL_BOOKMARKS,
    READER_PANEL_STATS_RECENT,
    READER_PANEL_REFRESH_SETTINGS,
    READER_PANEL_TURN_SETTINGS,
    READER_PANEL_KEY_SETTINGS,
    READER_PANEL_KEY_PICKER,
    READER_PANEL_DICT,
} reader_panel_t;
static reader_panel_t s_reader_panel;
static int s_turns;
typedef enum { APP_REDRAW_NONE, APP_REDRAW_PAGE, APP_REDRAW_FULL } app_redraw_t;
typedef struct { int unused; } app_ctx_t;

/* 绘制调用流：一次原语调用一条记录。/ Draw-call stream: one record per primitive call. */
enum {
    OP_FILL_ROUND_RECT = 1, OP_CONTROL_FRAME, OP_TEXT, OP_TEXT_VC,
    OP_BUTTON, OP_ICON, OP_FILL_CIRCLE, OP_DRAW_CIRCLE, OP_FILL_RECT,
    OP_TEXT_BOX, OP_PANEL_TEXT, OP_PANEL_TEXT_VC, OP_SWITCH
};
typedef struct {
    int op, x, y, w, h, px, align, inv, icon, gray;
    char text[64];
} draw_call_t;
static draw_call_t s_calls[512];
static int s_call_count;

static draw_call_t* record(int op) {
    assert(s_call_count < (int)(sizeof(s_calls) / sizeof(s_calls[0])));
    draw_call_t* c = &s_calls[s_call_count++];
    memset(c, 0, sizeof(*c));
    c->op = op;
    return c;
}
static void record_rect(int op, EpdRect r, int px, int gray) {
    draw_call_t* c = record(op);
    c->x = r.x; c->y = r.y; c->w = r.width; c->h = r.height; c->px = px; c->gray = gray;
}
static void record_text(int op, int x, int y, int px, const char* text, int align, int inv) {
    draw_call_t* c = record(op);
    size_t n = strlen(text);
    assert(n < sizeof(c->text));
    memcpy(c->text, text, n + 1);
    c->x = x; c->y = y; c->px = px; c->align = align; c->inv = inv;
}

// 归因：r91 的卡片与面板文本走 ui_text_box / reader_panel_text*，并引入系统字号缩放
// reader_ui_extra（测试里返回 0，即不缩放），这里补三个记录桩。
// Attribution: r91 draws cards and panel text via ui_text_box / reader_panel_text* and adds
// the reader_ui_extra system-scale hook (0 here, i.e. no scaling); three recording stubs.
static int reader_ui_extra(int base_px) { (void)base_px; return 0; }
static void copy_text(char* to, size_t cap, const char* from) {
    size_t n = strlen(from);
    if (n >= cap) n = cap - 1;
    memcpy(to, from, n);
    to[n] = 0;
}
void ui_text_box(uint8_t* framebuffer, EpdRect box, int px, const char* text,
                 enum EpdFontFlags align, bool bold) {
    (void)framebuffer;
    record_rect(OP_TEXT_BOX, box, px, bold ? 1 : 0);
    record_text(OP_TEXT_BOX, box.x, box.y, px, text, (int)align, 0);
}
void reader_panel_text_vc(uint8_t* framebuffer, int cx, int y_mid, int px, const char* text,
                          enum EpdFontFlags align, bool inverted) {
    (void)framebuffer;
    record_text(OP_PANEL_TEXT_VC, cx, y_mid, px, text, (int)align, inverted ? 1 : 0);
}
void draw_reading_switch(uint8_t* framebuffer, EpdRect track, bool on) {
    (void)framebuffer; record_rect(OP_SWITCH, track, on ? 1 : 0, 0);
}
void ui_fill_round_rect(uint8_t* framebuffer, EpdRect rect, int radius, uint8_t color) {
    (void)framebuffer; record_rect(OP_FILL_ROUND_RECT, rect, radius, color);
}
void ui_draw_control_frame(uint8_t* framebuffer, EpdRect rect, int radius, uint8_t color) {
    (void)framebuffer; record_rect(OP_CONTROL_FRAME, rect, radius, color);
}
void ui_text(uint8_t* framebuffer, int x, int y_top, int px, const char* text,
             enum EpdFontFlags align, bool inverted) {
    (void)framebuffer; record_text(OP_TEXT, x, y_top, px, text, (int)align, inverted ? 1 : 0);
}
void ui_text_vc(uint8_t* framebuffer, int x, int center_y, int px, const char* text,
               enum EpdFontFlags align, bool inverted) {
    (void)framebuffer; record_text(OP_TEXT_VC, x, center_y, px, text, (int)align, inverted ? 1 : 0);
}
void ui_draw_button(uint8_t* framebuffer, EpdRect rect, const char* label, bool on) {
    (void)framebuffer; draw_call_t* c = record(OP_BUTTON);
    c->x = rect.x; c->y = rect.y; c->w = rect.width; c->h = rect.height; c->inv = on ? 1 : 0;
    size_t n = strlen(label);
    assert(n < sizeof(c->text));
    memcpy(c->text, label, n + 1);
}
void ui_draw_icon(uint8_t* framebuffer, int cx, int cy, int size, ui_icon_t icon, uint8_t gray) {
    (void)framebuffer; draw_call_t* c = record(OP_ICON);
    c->x = cx; c->y = cy; c->px = size; c->icon = (int)icon; c->gray = gray;
}
void epd_fill_circle(int x, int y, int r, uint8_t color, uint8_t* framebuffer) {
    (void)framebuffer; draw_call_t* c = record(OP_FILL_CIRCLE);
    c->x = x; c->y = y; c->px = r; c->gray = color;
}
void epd_draw_circle(int x, int y, int r, uint8_t color, uint8_t* framebuffer) {
    (void)framebuffer; draw_call_t* c = record(OP_DRAW_CIRCLE);
    c->x = x; c->y = y; c->px = r; c->gray = color;
}
void epd_fill_rect(EpdRect rect, uint8_t color, uint8_t* framebuffer) {
    (void)framebuffer; record_rect(OP_FILL_RECT, rect, 0, color);
}
/* r93 的快速填充替换了面板绘制里的 epd_fill_rect：宿主桩转发到同一记录器，
   几何断言不受填充实现影响。/ r93's fast fill replaces epd_fill_rect in panel
   drawing: the host stub forwards to the same recorder; geometry assertions are
   unaffected by the fill implementation. */
void ui_fill_rect_fast(EpdRect rect, uint8_t color, uint8_t* framebuffer) {
    epd_fill_rect(rect, color, framebuffer);
}

/* 与生产一致的点命中语义：半开区间 [x, x+w)。/ Same hit semantics as production. */
static bool ui_rect_hit(EpdRect r, uint16_t x, uint16_t y) {
    return x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height;
}

/* 记录桩：派发/全刷/失效各记一笔，行为用例断言的就是这些记录。
   Recording stubs; the behavior cases assert on these counters. */
static int s_apply_kind = -1, s_apply_calls, s_manual_calls, s_invalidate_calls;
app_redraw_t apply_reader_option(app_ctx_t* ctx, int kind) {
    (void)ctx; s_apply_kind = kind; ++s_apply_calls; return APP_REDRAW_PAGE;
}
app_redraw_t reader_manual_refresh(app_ctx_t* ctx) {
    (void)ctx; ++s_manual_calls; return APP_REDRAW_FULL;
}
static void invalidate_prep(void) { ++s_invalidate_calls; }

/* 设置替身：固定取值，黄金值对应的就是这一组状态。/ Settings fakes: fixed values, the golden matches this state. */
static int test_full_pages = 10;
static int test_turn_effect = 0;
static bool test_vertical_turn, test_power_turn = true;
static bool test_immersive, test_hide_images, test_landscape = true;
int app_settings_reader_full_pages(void) { return test_full_pages; }
int app_settings_reader_turn_effect(void) { return test_turn_effect; }
bool app_settings_reader_vertical_turn(void) { return test_vertical_turn; }
bool app_settings_reader_power_turn(void) { return test_power_turn; }
static int test_two_column;
bool app_settings_reader_two_column(void) { return test_two_column != 0; }
void app_settings_set_reader_two_column(bool on) { test_two_column = on ? 1 : 0; }
bool app_settings_reader_immersive(void) { return test_immersive; }
bool app_settings_reader_hide_images(void) { return test_hide_images; }
bool app_settings_reader_landscape(void) { return test_landscape; }
void app_settings_set_reader_full_pages(uint8_t pages) { test_full_pages = pages; }
void app_settings_set_reader_turn_effect(uint8_t effect) { test_turn_effect = effect; }
'''

MAIN = r'''
/* FNV-1a 64 位摘要：逐字段喂入，不碰结构体内存，跨编译器无填充字节差异。
   FNV-1a 64 digest over fields one by one: no struct memory is hashed, so
   compiler padding cannot shift the result. */
static unsigned long long s_hash = 1469598103934665603ULL;
static void hash_bytes(const void* p, size_t n) {
    const unsigned char* b = p;
    for (size_t i = 0; i < n; ++i) {
        s_hash ^= b[i];
        s_hash *= 1099511628211ULL;
    }
}
#define HASH(v) hash_bytes(&(v), sizeof(v))

static const char* op_name(int op) {
    switch (op) {
        case OP_FILL_ROUND_RECT: return "fill_round_rect";
        case OP_CONTROL_FRAME: return "control_frame";
        case OP_TEXT: return "text";
        case OP_TEXT_VC: return "text_vc";
        case OP_BUTTON: return "button";
        case OP_ICON: return "icon";
        case OP_FILL_CIRCLE: return "fill_circle";
        case OP_DRAW_CIRCLE: return "draw_circle";
        case OP_FILL_RECT: return "fill_rect";
        // 归因：r91 的卡片文本走 ui_text_box，面板文本/开关走 reader_panel_text* 与
        // draw_reading_switch，补名字以便流文件可直接读。/ Attribution: r91 draws card text
        // via ui_text_box and panel text / switches via reader_panel_text* and
        // draw_reading_switch; name them so the stream file stays readable.
        case OP_TEXT_BOX: return "text_box";
        case OP_PANEL_TEXT: return "panel_text";
        case OP_PANEL_TEXT_VC: return "panel_text_vc";
        case OP_SWITCH: return "switch";
        default: return "?";
    }
}

static unsigned long long digest_calls(void) {
    s_hash = 1469598103934665603ULL;
    for (int i = 0; i < s_call_count; ++i) {
        const draw_call_t* c = &s_calls[i];
        HASH(c->op); HASH(c->x); HASH(c->y); HASH(c->w); HASH(c->h);
        HASH(c->px); HASH(c->align); HASH(c->inv); HASH(c->icon); HASH(c->gray);
        HASH(c->text);
    }
    return s_hash;
}

/* 行为用例失败只写 stderr，stdout 保持纯调用流。/ Failures go to stderr only. */
static int fail(const char* what) {
    fprintf(stderr, "case FAIL: %s\n", what);
    return 1;
}

/* 热区行为：全表格驱动，不硬编码行索引。/ Hot-zone behavior: fully table-driven. */
static int behavior_cases(void) {
    app_ctx_t ctx;
    s_reader_panel = READER_PANEL_REFRESH_SETTINGS;

    /* 逐槽点中点：按槽位类型派发——开关槽走 apply_reader_option(kind)，子面板槽进
       对应子面板，值循环槽推进取值。行表的 cell 字段决定期望，断言不硬编码槽号。
       Tap each slot's center: the cell type decides — toggle slots call
       apply_reader_option(kind), sub-page slots open their panel, cycling slots advance.
       Expectations come from the table's cell field, never from hardcoded slot numbers. */
    for (int i = 0; i < reading_row_count(); ++i) {
        EpdRect row = reading_toggle_rect(i);
        s_apply_calls = 0; s_apply_kind = -1;
        app_redraw_t r = reading_settings_action(&ctx,
            (uint16_t)(row.x + row.width / 2), (uint16_t)(row.y + row.height / 2));
        switch (s_reading_rows[i].kind) {
            case READING_ROW_KEYS:
                if (r != APP_REDRAW_PAGE || s_reader_panel != READER_PANEL_KEY_SETTINGS || s_apply_calls)
                    return fail("keys slot must open the key sub-panel");
                s_reader_panel = READER_PANEL_REFRESH_SETTINGS; // 还原供下一槽 / restore for the next slot
                break;
            case READING_ROW_TURN:
                if (r != APP_REDRAW_PAGE || s_reader_panel != READER_PANEL_TURN_SETTINGS || s_apply_calls)
                    return fail("turn slot must open the turn sub-panel");
                s_reader_panel = READER_PANEL_REFRESH_SETTINGS;
                break;
            case READING_ROW_FULL:
                // options {5,10,15,30,0}：初值 10，点一下推进到 15。/ from 10 to 15 on one tap
                if (r != APP_REDRAW_PAGE || s_apply_calls || test_full_pages != 15)
                    return fail("auto-refresh slot must cycle its value");
                test_full_pages = 10; // 还原 / restore
                break;
            case READING_ROW_WAVE:
                if (r != APP_REDRAW_PAGE || s_apply_calls || test_turn_effect != 1)
                    return fail("turn-effect slot must toggle the ripple");
                test_turn_effect = 0; // 还原 / restore
                break;
            default:
                if (s_apply_calls != 1 || s_apply_kind != s_reading_rows[i].kind)
                    return fail("toggle slot must dispatch its table kind");
                break;
        }
    }

    /* 行间间隙（行底那一像素）→ 无动作。/ The 1px gap below a row hits nothing. */
    {
        EpdRect row = reading_toggle_rect(0);
        if (reading_settings_action(&ctx, 36, (uint16_t)(row.y + READING_ROW_HEIGHT)) != APP_REDRAW_NONE)
            return fail("row gap must be inert");
    }

    /* 归因：r90 的四张固定卡片（手动全刷 / 自动全刷 / 翻页效果 / 翻页模式）在 r91 已并入
       卡片槽位，旧坐标不复存在；等价覆盖交给上面的逐槽循环。手动全刷仍可由按键触发，
       不在本面板验证。
       Attribution: r91 folded the four r90 fixed cards into slot cards, so the old
       coordinates are gone; the per-slot loop above covers the same behavior. Manual
       refresh is still reachable by key and is not verified in this panel. */

    /* Done 与顶部标题区点击都关闭面板并失效预渲染。
       Done and the header both close the panel and invalidate the prep cache. */
    s_reader_panel = READER_PANEL_REFRESH_SETTINGS; s_invalidate_calls = 0;
    if (reading_settings_action(&ctx, 342, 1163) != APP_REDRAW_PAGE ||
        s_reader_panel != READER_PANEL_NONE || s_invalidate_calls != 1)
        return fail("done button closes the panel");

    s_reader_panel = READER_PANEL_REFRESH_SETTINGS; s_invalidate_calls = 0;
    if (reading_settings_action(&ctx, 342, 100) != APP_REDRAW_PAGE ||
        s_reader_panel != READER_PANEL_NONE || s_invalidate_calls != 1)
        return fail("header tap closes the panel");

    return 0;
}

int main(void) {
    draw_reading_settings(NULL);
    assert(s_call_count > 0);
    unsigned long long digest = digest_calls();
    printf("digest=%016llx\n", digest);
    printf("calls=%d\n", s_call_count);
    for (int i = 0; i < s_call_count; ++i) {
        const draw_call_t* c = &s_calls[i];
        printf("%3d %-16s x=%-4d y=%-4d w=%-4d h=%-4d px=%-3d align=%d inv=%d icon=%-3d gray=%-3d text=%s\n",
               i, op_name(c->op), c->x, c->y, c->w, c->h, c->px, c->align, c->inv, c->icon, c->gray,
               c->text[0] ? c->text : "-");
    }
    /* 屏界内：所有矩形必须落在竖屏 684x1216 里。唯一例外是 draw_sheet 的圆角底帽——
       它按设计向下溢出 28px（圆角半径），由显示硬件裁剪，见 draw_sheet 的
       UI_LOCK_HEIGHT - top + 28。/ On-screen: every rect must fit 684x1216 portrait. The one
       exception is draw_sheet's rounded bottom cap, which bleeds 28px (the corner radius)
       by design and is clipped by the panel. */
    const int bottom_bleed = 28;
    for (int i = 0; i < s_call_count; ++i) {
        const draw_call_t* c = &s_calls[i];
        if (c->op == OP_TEXT || c->op == OP_TEXT_VC || c->op == OP_ICON ||
            c->op == OP_FILL_CIRCLE || c->op == OP_DRAW_CIRCLE) continue;
        if (!(c->x >= 0 && c->y >= 0 && c->x + c->w <= 684 && c->y + c->h <= 1216 + bottom_bleed)) {
            fprintf(stderr, "OUT i=%d op=%d x=%d y=%d w=%d h=%d\n", i, c->op, c->x, c->y, c->w, c->h);
            return 1;
        }
    }
    /* 确定性：同样输入再画一遍，摘要必须一致。/ Determinism: a second render must match. */
    s_call_count = 0;
    draw_reading_settings(NULL);
    assert(digest == digest_calls());

    if (behavior_cases()) return 1;
    puts("reader_settings_panel: PASS");
    return 0;
}
'''


def build_unit():
    src = SOURCE.read_text(encoding='utf-8')
    unit = PRELUDE
    for name in MODEL_DEFINES:
        unit += define(src, name) + '\n'
    # 归因：行表新增槽位类型枚举，必须先于结构体抽取。
    # Attribution: the table gained a cell-type enum, which has to precede the struct.
    unit += declaration(src, 'typedef enum {', '} reading_cell_t;') + '\n'
    unit += declaration(src, 'typedef struct {', '} reading_row_t;') + '\n'
    unit += initializer(src, 'static const reading_row_t s_reading_rows') + '\n'
    for name in EXTRACT:
        unit += function(src, name) + '\n'
    unit += MAIN
    return unit


def run_c(unit):
    with tempfile.TemporaryDirectory() as tmp:
        c = Path(tmp) / 'panel.c'
        exe = Path(tmp) / 'panel'
        c.write_text(unit, encoding='utf-8')
        subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-variable', '-Wno-unused-function',
                        '-fsanitize=address,undefined', str(c), '-o', str(exe)],
                       check=True)
        # 归因（2026-10-16 审计修复）：不用 check=True——过去行为用例非零退出会抛
        # CalledProcessError，把 stdout 里的 digest 行一并吞掉，digest 比对永远执行不到，
        # 于是「行为红 + 流变」被静默。这里改为取回 returncode，把 digest 比对与行为
        # 断言解耦：digest 行一旦印出就先比对，行为失败也照常上报。
        # Attribution (audit fix 2026-10-16): no check=True — a non-zero exit from a
        # behavior case used to raise and swallow the digest line on stdout, so the
        # digest comparison never ran and "behavior red + stream change" went silent.
        # We capture returncode and decouple: the digest line, once printed, is compared
        # first; a behavior failure is still surfaced afterwards.
        proc = subprocess.run([str(exe)], capture_output=True, text=True)
        return proc.stdout, proc.stderr, proc.returncode


def main():
    regen = '--regen' in sys.argv
    out, err, rc = run_c(build_unit())
    digest = next((line for line in out.splitlines() if line.startswith('digest=')), None)
    if digest is None:
        # C 单元在打印 digest 之前就崩了（如 assert abort）：行为失败优先报，但不假装有流比对。
        # The C unit crashed before the digest line (e.g. assert abort): surface the
        # behavior failure first, but do not pretend a stream comparison happened.
        print('reader settings panel: NO DIGEST (C unit crashed before digest line)')
        if rc != 0:
            print(err)
        raise SystemExit(1)
    digest = digest.split('=', 1)[1]
    stream = '\n'.join(line for line in out.splitlines()
                       if not line.startswith(('digest=', 'calls=')) and not line.startswith('reader_settings_panel:'))
    if regen:
        GOLDEN.write_text('digest=%s\n' % digest, encoding='utf-8')
        STREAM.write_text(stream + '\n', encoding='utf-8')
        print('reader_settings_panel: golden frozen -> %s (%s)' % (GOLDEN.name, digest))
        return
    golden = GOLDEN.read_text(encoding='utf-8').strip()
    if golden != 'digest=%s' % digest:
        # digest 红（绘制流与冻结值不符）：这是停手异象，但行为 rc 仍属现场信息，
        # 必须一并打印——不能把「行为短路 digest」换向成「digest 短路行为」。
        # Digest is red (draw stream diverged from the frozen value): this is a
        # stop-and-report anomaly, but the behavior rc is still scene info and must
        # be surfaced too — do not swap "behavior-masked-digest" for "digest-masked-behavior".
        print('reader settings panel draw stream CHANGED: golden %s, now %s' % (golden, digest))
        print('--- current stream (run with --regen only after an intentional panel change) ---')
        print(stream)
        if rc != 0:
            print('reader settings panel: behavior/on-screen case also FAILED (rc=%d)' % rc)
            print(err)
        raise SystemExit(1)
    # digest 通过；若 C 单元仍非零退出（行为/屏界用例失败），此刻再上报——不再吞掉 digest。
    # Digest passed; if the C unit still exited non-zero (behavior / on-screen case
    # failed), surface it now instead of letting it mask the digest comparison.
    if rc != 0:
        print('reader_settings_panel: digest OK (%s) but behavior/on-screen case FAILED' % digest)
        print(err)
        raise SystemExit(1)
    print('reader_settings_panel: PASS (%s)' % digest)


if __name__ == '__main__':
    main()
