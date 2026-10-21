#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 中文：验证已确认的阅读设置共用矩形、点按周期、二级页与间隙不穿透。
# English: Validate approved reading-sheet geometry, cycles, secondary destinations and inert gaps.
from pathlib import Path
import re
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]


def mask(text):
    """掩码注释，保留字符串与代码，便于按标记截取。/ Mask comments for marker slicing."""
    tokens = r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    return re.sub(tokens, lambda m: ' ' * len(m[0]) if m[0].startswith(('//', '/*')) else m[0], text)


def define(text, name):
    """抽取一条 #define（整行）。/ Extract one #define line."""
    m = re.search(r'^#define\s+' + name + r'\b[^\n]*$', text, re.M)
    assert m, name
    return m.group(0)


def declaration(text, head, tail):
    """按起止标记抽取声明（typedef 等）；先定位尾标记，再向前找最近的头标记。
    Extract a declaration by head/tail markers: anchor on the tail first, then walk
    back to the nearest head, so other same-shaped declarations cannot match."""
    scan = mask(text)
    j = scan.index(tail)
    i = scan.rindex(head, 0, j)
    return text[i:j + len(tail)]


def initializer(text, head):
    """抽取带花括号初始化器的变量定义。/ Extract a variable with a brace initializer."""
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

unit = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {int unused;} app_ctx_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_PAGE} app_redraw_t;
enum {READER_PANEL_NONE,READER_PANEL_REFRESH_SETTINGS,READER_PANEL_TURN_SETTINGS,READER_PANEL_KEY_SETTINGS};
static int s_reader_panel, s_turns, invalidates, toggles[9], full_pages = 10, effect;
static int two_column;
static bool ui_rect_hit(EpdRect r,unsigned x,unsigned y){return x>=(unsigned)r.x&&x<(unsigned)(r.x+r.width)&&y>=(unsigned)r.y&&y<(unsigned)(r.y+r.height);}
static unsigned app_settings_reader_full_pages(void){return full_pages;}
static void app_settings_set_reader_full_pages(unsigned n){full_pages=n;}
static unsigned app_settings_reader_turn_effect(void){return effect;}
static void app_settings_set_reader_turn_effect(unsigned n){effect=n;}
// 归因：我方行表有四个开关行（多出「横屏阅读」kind=3）。/ Attribution: our table has
// four toggle rows (the extra one is 横屏阅读, kind 3).
static app_redraw_t apply_reader_option(app_ctx_t *ctx,int i){(void)ctx;assert(i>=0&&i<9&&i!=4&&i!=5&&i!=6&&i!=7);++toggles[i];return APP_REDRAW_PAGE;}
bool app_settings_reader_two_column(void){return two_column!=0;}
void app_settings_set_reader_two_column(bool on){two_column=on?1:0;}
static void invalidate_prep(void){++invalidates;}
'''
source = ROOT/'main/apps/app_book.c'
src = source.read_text()
unit+='static unsigned app_settings_system_font_size(void){return 100;}\n'
# 归因：我方把槽位计数参数化（READING_ROW_*），并把「按键控制」之外的卡片也纳入行表，
# 上游原版用的是字面量与三个开关行的序号映射，故这里补抽行表本身。
# Attribution: our table parameterises the slot count (READING_ROW_*) and covers the cards
# too, where upstream used literals and an ordinal mapping for three toggle rows; extract
# the table itself.
for name in ('READING_ROW_KEYS', 'READING_ROW_FULL', 'READING_ROW_WAVE', 'READING_ROW_TURN',
             'READING_ROW_COUNT', 'READING_ROW_COLS'):
    unit += define(src, name)+'\n'
unit += declaration(src, 'typedef enum {', '} reading_cell_t;')+'\n'
unit += declaration(src, 'typedef struct {', '} reading_row_t;')+'\n'
unit += initializer(src, 'static const reading_row_t s_reading_rows')+'\n'
for name in ('reader_ui_extra', 'reading_settings_top', 'reading_setting_rect',
             'reading_toggle_rect', 'reading_row_count', 'reading_settings_action'):
    unit += function(name, source)+'\n'
unit += r'''
int main(void){
    app_ctx_t ctx={0};
    // 9个控件位于半屏内、不重叠（第 4 步起两列五行）；按键控制严格在翻页模式下一行。
    // Nine controls stay inside the half sheet without overlap (two columns x five rows
    // from step 4); key controls sit directly below turn mode.
    for(int i=0;i<reading_row_count();++i){
        EpdRect a=reading_setting_rect(i);assert(a.x>=36&&a.x+a.width<=648&&a.y>=702&&a.y+a.height<=1122);
        for(int j=i+1;j<reading_row_count();++j){EpdRect b=reading_setting_rect(j);assert(a.x+a.width<=b.x||b.x+b.width<=a.x||a.y+a.height<=b.y||b.y+b.height<=a.y);}
    }
    EpdRect mode=reading_setting_rect(2),keys=reading_setting_rect(4);
    // 第 4 步 5 行几何：行高 76+extra、间距 10、首行 702——行距 86，槽 2 在 788、槽 4 在 874。
    // Step-4 five-row geometry: 76+extra rows, 10px gaps, first row at 702 — an 86 pitch,
    // slot 2 at 788 and slot 4 at 874.
    assert(keys.x==mode.x&&keys.y>mode.y&&keys.y==874);
    const unsigned expected[]={15,30,0,5,10};
    for(unsigned i=0;i<5;++i){s_turns=100;assert(reading_settings_action(&ctx,120,750)==APP_REDRAW_PAGE);assert(full_pages==(int)expected[i]&&!s_turns);}
    full_pages=77;reading_settings_action(&ctx,120,750);assert(full_pages==5);
    for(unsigned i=0;i<6;++i){reading_settings_action(&ctx,450,750);assert(effect==(int)((i+1)%2));}
    s_reader_panel=READER_PANEL_REFRESH_SETTINGS;
    reading_settings_action(&ctx,120,820);assert(s_reader_panel==READER_PANEL_TURN_SETTINGS);
    s_reader_panel=READER_PANEL_REFRESH_SETTINGS;
    reading_settings_action(&ctx,120,910);assert(s_reader_panel==READER_PANEL_KEY_SETTINGS);
    // 归因：reading_toggle_rect 现在按槽位直编，开关槽由行表的 cell 字段给出。
    // Attribution: reading_toggle_rect is slot-indexed now; the table's cell field
    // tells which slots are toggles.
    for(int i=0;i<reading_row_count();++i){
        if(s_reading_rows[i].cell!=READING_CELL_TOGGLE) continue;
        EpdRect a=reading_toggle_rect(i);
        assert(reading_settings_action(&ctx,a.x+a.width-5,a.y+a.height-5)==APP_REDRAW_PAGE&&toggles[s_reading_rows[i].kind]==1);
    }
    s_reader_panel=READER_PANEL_REFRESH_SETTINGS;int before=invalidates;
    assert(reading_settings_action(&ctx,342,760)==APP_REDRAW_NONE);
    // 惰性间隙随 5 行几何更新：行距 86、行高 76 → 间隙 778..788（取 783）。
    // Inert gaps follow the five-row geometry: 86 pitch over 76 height -> the gap at 778..788 (783 probed).
    assert(reading_settings_action(&ctx,120,783)==APP_REDRAW_NONE);
    assert(reading_settings_action(&ctx,120,1130)==APP_REDRAW_NONE);
    assert(s_reader_panel==READER_PANEL_REFRESH_SETTINGS&&invalidates==before);
    reading_settings_action(&ctx,342,1170);assert(s_reader_panel==READER_PANEL_NONE);
    s_reader_panel=READER_PANEL_REFRESH_SETTINGS;
    reading_settings_action(&ctx,342,579);assert(s_reader_panel==READER_PANEL_NONE);
    puts("PASS: approved half-sheet bounds, nonoverlapping shared hit areas, key controls below turn mode, full-refresh/effect cycles, toggles, secondary pages, Done and inert gaps");
}
'''
with tempfile.TemporaryDirectory(dir=ROOT/'build') as folder:
    c,exe=Path(folder)/'test.c',Path(folder)/'test'
    c.write_text(unit)
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
