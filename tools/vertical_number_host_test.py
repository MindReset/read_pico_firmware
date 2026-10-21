#!/usr/bin/env python3
"""竖排编号居中回归：半角数字整串共用一条竖轴，加一位不许让编号列偏移。
Vertical number centring regression: a run of halfwidth digits shares one axis, and adding a digit
must not shift the number column.
SPDX-License-Identifier: Apache-2.0
"""
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FONT = (ROOT / 'main/assets/builtin.ttf').read_bytes()

HEADERS = {
    'esp_err.h': ('typedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n'
                  '#define ESP_ERR_NO_MEM 0x101\n#define ESP_ERR_NOT_FOUND 0x105\n'
                  '#define ESP_ERR_INVALID_RESPONSE 0x108\n'),
    'esp_log.h': ('#pragma once\nstatic inline void tlog(const char *t,const char *f,...){(void)t;(void)f;}\n'
                  '#define ESP_LOGI(...) tlog(__VA_ARGS__)\n#define ESP_LOGW(...) tlog(__VA_ARGS__)\n'
                  '#define ESP_LOGE(...) tlog(__VA_ARGS__)\n'),
    'esp_timer.h': '#pragma once\n#include <stdint.h>\nstatic inline int64_t esp_timer_get_time(void){return 0;}\n',
    'esp_heap_caps.h': ('#pragma once\n#include <stdlib.h>\n#define MALLOC_CAP_SPIRAM 1\n'
                        '#define MALLOC_CAP_8BIT 2\nvoid *heap_caps_malloc(size_t n,int caps);\n'
                        'void *heap_caps_calloc(size_t n,size_t s,int caps);\nvoid heap_caps_free(void *p);\n'
                        'static inline size_t heap_caps_get_largest_free_block(int c){(void)c;return 0;}\n'),
}

ASSETS = ('#include <stdint.h>\nconst uint8_t data[] asm("_binary_builtin_ttf_start")={' +
          ','.join(map(str, FONT)) + '};\n' +
          f'asm(".globl _binary_builtin_ttf_end\\n.set _binary_builtin_ttf_end, '
          f'_binary_builtin_ttf_start + {len(FONT)}");\n')

UNIT = r'''
#include "ttf_font.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define FB_W 220
#define FB_H 128
static uint8_t fb[FB_W * FB_H];
void epd_draw_pixel(int x,int y,uint8_t c,uint8_t *f){(void)f;if(x>=0&&x<FB_W&&y>=0&&y<FB_H)fb[y*FB_W+x]=c>>4;}
void *heap_caps_malloc(size_t n,int c){(void)c;return malloc(n);}
void *heap_caps_calloc(size_t n,size_t s,int c){(void)c;return calloc(n,s);}
void heap_caps_free(void *p){free(p);}
const char *app_settings_fonts_dir(void){return "/absent";}
const char *app_settings_font_path(void){return "builtin";}
bool ui_font_has_text(const char *s){(void)s;return true;}
int ui_font_title_px(int px,const char *s){(void)s;return px;}
int ui_font_text_width_px(int px,const char *s){(void)px;(void)s;return 0;}
void ui_font_measure_line_px(int px,const char *s,int *a,int *b){(void)px;(void)s;*a=0;*b=0;}
void ui_font_draw_title_px(uint8_t *f,int x,int y,int px,const char *s,enum EpdFontFlags a){
    (void)f;(void)x;(void)y;(void)px;(void)s;(void)a;}
// 画一列竖排文字，回报墨迹的横向范围与推进量。/ Draw a vertical column, report its ink box and advance.
static void measure(const char *text,int px,int *centre,int *advance,int *left,int *right){
    const int x0 = 40;
    memset(fb, 15, sizeof(fb));
    *advance = ttf_draw_text_px_vertical(fb, x0, 4, px, text, 0, 0, 15);
    *left = 1 << 20; *right = -1;
    for (int y = 0; y < FB_H; ++y)
        for (int x = 0; x < FB_W; ++x)
            if (fb[y * FB_W + x] == 0) { if (x < *left) *left = x; if (x > *right) *right = x; }
    assert(*right > *left);
    *centre = (*left + *right) / 2;
}
int main(void){
    assert(ttf_font_open_builtin() == ESP_OK && ttf_font_ready());
    const int px = 48;
    // 参考轴：一个全角汉字的墨迹中心，就是这一列该对齐的位置。
    // The reference axis: the ink centre of a fullwidth ideograph in this column.
    int han_centre, han_advance, han_left, han_right;
    measure("第", px, &han_centre, &han_advance, &han_left, &han_right);
    // 一位、两位、三位数字：整串共用一条竖轴，且落在汉字的轴上（±2px）。
    // One, two and three digits: one shared axis, sitting on the ideographs' axis (within 2 px).
    const char *numbers[] = {"1", "12", "123"};
    int centre[3], advance[3];
    for (unsigned i = 0; i < sizeof(numbers) / sizeof(numbers[0]); ++i) {
        int left, right;
        measure(numbers[i], px, &centre[i], &advance[i], &left, &right);
        if (centre[i] < han_centre - 2 || centre[i] > han_centre + 2) {
            printf("FAIL: %s ink centre %d, ideograph axis %d\n", numbers[i], centre[i], han_centre);
            return 1;
        }
        if (i && centre[i] != centre[0]) {
            printf("FAIL: %s ink centre %d differs from %s's %d\n", numbers[i], centre[i], numbers[0], centre[0]);
            return 1;
        }
        if (advance[i] != ttf_vertical_text_height_px(px, numbers[i], 0)) {
            printf("FAIL: %s advance %d does not match its measured height\n", numbers[i], advance[i]);
            return 1;
        }
    }
    // 题头编号列：加一位数字只让列变长，不许让整列左右偏移。
    // A chapter number column: one more digit makes it taller, never shifts it sideways.
    int one, two, advance_one, advance_two, l1, r1, l2, r2;
    measure("第1章", px, &one, &advance_one, &l1, &r1);
    measure("第12章", px, &two, &advance_two, &l2, &r2);
    if (one != two || l1 != l2 || r1 != r2) {
        printf("FAIL: 第1章 ink [%d..%d] vs 第12章 ink [%d..%d]\n", l1, r1, l2, r2);
        return 1;
    }
    assert(advance_two > advance_one);
    puts("PASS: halfwidth digits share one vertical axis on the ideographs' centre line, and a longer chapter number only makes the column taller");
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='pico-vertical-number-') as directory:
    work = Path(directory)
    for name, text in HEADERS.items():
        (work / name).write_text(text, encoding='utf-8')
    (work / 'assets.c').write_text(ASSETS, encoding='utf-8')
    (work / 'test.c').write_text(UNIT, encoding='utf-8')
    flags = ['cc', '-std=gnu11', '-Wall', '-Wextra', # GCC 13 会把 ttf_font.c 里既有的 snprintf 告警升级成错误。
             '-Wno-error=format-truncation',
             '-I' + str(work), '-I' + str(ROOT / 'main/font'), '-I' + str(ROOT / 'main/ui'),
             '-I' + str(ROOT / 'main'), '-I' + str(ROOT / 'tools/ui_gesture_stubs')]
    subprocess.run(flags + [str(work / 'test.c'), str(work / 'assets.c'),
                            str(ROOT / 'main/font/ttf_font.c'), '-lm',
                            '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)
