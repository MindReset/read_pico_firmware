"""中文：实测细点阵的密度、旋转边界、书架模式隔离与低内存回退。
English: Exercise fine-dot density, rotated bounds, shelf-mode isolation and low-memory fallback.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(path, name):
    text = (ROOT / path).read_text()
    scan = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"',
                  lambda m: ' ' * len(m[0]), text)
    match = re.search(r'^(?:static )?[^\n]+\b' + name + r'\([^;{}]*?\)\s*\{', scan, re.M)
    assert match, name
    at, depth = match.end(), 1
    while depth:
        depth += (scan[at] == '{') - (scan[at] == '}')
        at += 1
    return text[match.start():at]


unit = r'''
// 本测试自带按旋转换算的尺寸替身，必须在任何头被拉进来之前关掉 stub 的 static inline 版，
// 否则后到的 include 会先定义它、与下面的实现重定义。
// This test ships its own rotation-aware size shim, so the static inline one has to be
// switched off before any header is pulled in, or a later include defines it first.
#define EPDIY_STUB_NO_ROTATED_SIZE
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "ui_image_dither.h"
#define W 1216
#define H 684
#define BYTES (W*H/2)
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
typedef struct {int x,y,width,height;} EpdRect;
enum {EPD_ROT_LANDSCAPE, EPD_ROT_PORTRAIT, EPD_ROT_INVERTED_LANDSCAPE, EPD_ROT_INVERTED_PORTRAIT};
enum {SHELF,MANAGE};
static int rotation=EPD_ROT_INVERTED_PORTRAIT, style=2, s_view=SHELF;
static bool fast=true, oom;
static unsigned allocs;
static int epd_width(void){return W;}
static int epd_height(void){return H;}
static int epd_rotated_display_width(void){return rotation&1?H:W;}
static int epd_rotated_display_height(void){return rotation&1?W:H;}
static int epd_get_rotation(void){return rotation;}
static int app_settings_shelf_style(void){return style;}
static bool app_settings_main_fast_refresh(void){return fast;}
static uint8_t epd_get_pixel(int x,int y,int w,int h,const uint8_t* fb){
    assert(w==W && h==H && x>=0 && y>=0 && x<W && y<H);
    unsigned offset=(unsigned)y*W+x;
    return ((fb[offset/2]>>((offset&1)*4))&15)<<4;
}
static void epd_draw_pixel(int x,int y,uint8_t gray,uint8_t* fb){
    assert(x>=0&&x<epd_rotated_display_width()&&y>=0&&y<epd_rotated_display_height());
    int px=x,py=y;
    if(rotation==EPD_ROT_PORTRAIT){px=W-y-1;py=x;}
    if(rotation==EPD_ROT_INVERTED_LANDSCAPE){px=W-x-1;py=H-y-1;}
    if(rotation==EPD_ROT_INVERTED_PORTRAIT){px=y;py=H-x-1;}
    unsigned offset=(unsigned)py*W+px,shift=(offset&1)*4;
    fb[offset/2]=(fb[offset/2]&~(15<<shift))|((gray>>4)<<shift);
}
static void epd_fill_rect(EpdRect r,uint8_t gray,uint8_t* fb){
    for(int y=r.y;y<r.y+r.height;++y)for(int x=r.x;x<r.x+r.width;++x)epd_draw_pixel(x,y,gray,fb);
}
static void ui_hairline(uint8_t* fb,int y,int x,int width,uint8_t gray){epd_fill_rect((EpdRect){x,y,width,1},gray,fb);}
static void epd_draw_line(int x0,int y0,int x1,int y1,uint8_t gray,uint8_t* fb){
    assert(x0==x1||y0==y1);
    epd_fill_rect((EpdRect){x0,y0,x1-x0+1,y1-y0+1},gray,fb);
}
static void epd_draw_circle(int x,int y,int radius,uint8_t gray,uint8_t* fb){
    for(int yy=-radius;yy<=radius;++yy)for(int xx=-radius;xx<=radius;++xx){
        int d=xx*xx+yy*yy;
        if(d<=radius*radius&&d>(radius-1)*(radius-1))epd_draw_pixel(x+xx,y+yy,gray,fb);
    }
}
static void* heap_caps_malloc(size_t bytes,unsigned caps){
    assert(caps==3&&bytes<=644*128);++allocs;
    return oom?NULL:malloc(bytes);
}
'''
for name in ('ui_read_pixel', 'ui_acrylic_bw_rect', 'draw_frosted',
             'ui_draw_acrylic_guard', 'ui_draw_frosted_pocket'):
    unit += function('main/ui/ui_kit.c', name) + '\n'
unit += function('main/apps/app_book.c', 'draw_shelf_furniture') + '\n'
unit += r'''
static uint8_t original[BYTES],gray[BYTES],dots[BYTES],again[BYTES];
static bool inside(int x,int y){
    if(style!=2&&style!=3)return false;
    for(int row=0;row<3;++row){
        int top=224+row*282;
        if(style==2&&x>=20&&x<664&&y>=top+136&&y<top+226)return true;
        if(style==3)for(int col=0;col<3;++col){
            int left=40+col*205;
            if(x>=left&&x<left+180&&y>=top+92&&y<top+220)return true;
        }
    }
    return false;
}
int main(void){
    for(unsigned tone=0;tone<256;++tone){
        unsigned white=0;
        for(int y=0;y<16;++y)for(int x=0;x<16;++x){
            uint8_t p=ui_image_dither_acrylic_bw((uint8_t)tone,x,y);
            assert(p==0||p==255);white+=p==255;
            assert(p==ui_image_dither_acrylic_bw((uint8_t)tone,x+8,y-8));
            if(tone)assert(p>=ui_image_dither_acrylic_bw((uint8_t)(tone-1),x,y));
        }
        double expected=(double)tone*256/255;
        assert(white-expected<2.01&&expected-white<2.01);
    }
    // 50%灰是严格的一像素棋盘格，负坐标及重复绘制的相位相同。
    // Midgray is a strict single-pixel checkerboard with stable phase at negative coordinates and repeated renders.
    for(int y=-16;y<16;++y)for(int x=-16;x<16;++x){
        uint8_t p=ui_image_dither_acrylic_bw(128,x,y);
        assert(p==(((x+y)&1)?0:255));
        assert(p!=ui_image_dither_acrylic_bw(128,x+1,y));
        assert(p!=ui_image_dither_acrylic_bw(128,x,y+1));
    }
    for(rotation=0;rotation<4;++rotation){
        memset(dots,0x77,BYTES);memcpy(original,dots,BYTES);
        ui_acrylic_bw_rect(dots,(EpdRect){-2,-2,39,41});
        for(int y=0;y<epd_rotated_display_height();++y)for(int x=0;x<epd_rotated_display_width();++x){
            unsigned value=ui_read_pixel(dots,x,y);
            if(x<37&&y<39)assert(value==0||value==15);else assert(value==7);
        }
        memcpy(again,dots,BYTES);ui_acrylic_bw_rect(dots,(EpdRect){-2,-2,39,41});
        assert(!memcmp(dots,again,BYTES));
        ui_acrylic_bw_rect(dots,(EpdRect){INT_MAX,INT_MAX,INT_MAX,INT_MAX});
        ui_acrylic_bw_rect(dots,(EpdRect){INT_MIN,INT_MIN,INT_MAX,INT_MAX});
        ui_acrylic_bw_rect(NULL,(EpdRect){0,0,10,10});
        assert(!memcmp(dots,again,BYTES));
    }
    rotation=EPD_ROT_INVERTED_PORTRAIT;
    for(int y=0;y<W;++y)for(int x=0;x<H;++x)
        epd_draw_pixel(x,y,((x/7+y/11)%2)?255:0,original);
    for(style=0;style<5;++style)for(oom=false;;oom=true){
        memcpy(gray,original,BYTES);fast=false;draw_shelf_furniture(gray);
        memcpy(dots,original,BYTES);fast=true;draw_shelf_furniture(dots);
        for(int y=0;y<W;++y)for(int x=0;x<H;++x){
            unsigned actual=ui_read_pixel(dots,x,y);
            if(inside(x,y))assert(actual==0||actual==15);
            else assert(actual==ui_read_pixel(gray,x,y));
        }
        memcpy(again,original,BYTES);s_view=MANAGE;draw_shelf_furniture(again);s_view=SHELF;
        assert(!memcmp(again,gray,BYTES));
        memcpy(again,original,BYTES);draw_shelf_furniture(again);
        assert(!memcmp(dots,again,BYTES));
        if(oom)break;
    }
    puts("PASS: one-pixel dots; all 256 tone means; stable phase/monotonic density; 4 rotations/overflow clips; actual 5 shelf styles; ordinary and detail isolation; OOM composition fallback; navigation outside guards untouched");
}
'''
with tempfile.TemporaryDirectory(dir=ROOT / 'build') as folder:
    source, exe = Path(folder) / 'dots.c', Path(folder) / 'dots'
    source.write_text(unit)
    subprocess.run(['cc', '-std=gnu11', '-O1', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I'+str(ROOT/'main/ui'),
                    str(source), str(ROOT/'main/ui/ui_image_dither.c'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
# 绘制端不再注册灰区；真实present的行为在UI与display调度回归中验证。
# Rendering no longer registers gray bands; UI and display regressions exercise the real present routes.
assert 'display_main_transition_gray_bands' not in function('main/apps/app_book.c', 'present')
