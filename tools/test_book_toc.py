"""Exercise the firmware's real directory layout and title normalization on the host."""
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "main/apps/book_toc.c"

STUBS = {
    "epdiy.h": """#pragma once
#include <stdint.h>
typedef struct {int x,y,width,height;} EpdRect;
enum EpdFontFlags {EPD_DRAW_ALIGN_LEFT=0, EPD_DRAW_ALIGN_CENTER=1, EPD_DRAW_ALIGN_RIGHT=2};
void epd_fill_rect(EpdRect rect, uint8_t color, uint8_t *fb);
void epd_fill_circle(int x,int y,int radius,uint8_t color,uint8_t *fb);
void epd_draw_circle(int x,int y,int radius,uint8_t color,uint8_t *fb);
""",
    "book_source.h": """#pragma once
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
esp_err_t book_navigation_title(size_t position, char *out, size_t cap);
size_t book_navigation_chapter(size_t position);
""",
    "ui_kit.h": """#pragma once
#include <stdbool.h>
#include "epdiy.h"
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
#define UI_GRAY_WHITE 255
void ui_clear_page(uint8_t *fb);
void ui_fill_round_rect(uint8_t *fb, EpdRect r, int radius, uint8_t gray);
void ui_draw_round_rect(uint8_t *fb, EpdRect r, int radius, uint8_t gray);
/* r93 的 book_toc_render 用 ui_fill_rect_fast 清页；签名与 main/ui/ui_kit.h 逐字一致。
   r93 book_toc_render clears the page through ui_fill_rect_fast; signature matches ui_kit.h. */
void ui_fill_rect_fast(EpdRect rect, uint8_t gray, uint8_t *fb);
void ui_text_fixed(uint8_t *fb,int x,int y,int px,const char *text,enum EpdFontFlags align,bool inverse);
int ui_text_fixed_width_px(int px,const char *text);
int ui_text_effective_px(int px);
EpdRect ui_inset_rect(EpdRect rect,int d);
void ui_text_box(uint8_t *fb,EpdRect box,int px,const char *text,enum EpdFontFlags align,bool inverse);
bool ui_rect_hit(EpdRect r,int x,int y);
void ui_hairline(uint8_t *fb,int y,int x,int width,uint8_t gray);
""",
    "ui_nav.h": """#pragma once
#include <stdint.h>
void ui_nav_status(uint8_t *fb);
void ui_nav_back(uint8_t *fb,int x,int y);
""",
}

HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "book_toc.c"
static int cards;
static int labels;
static int large_titles,skip_labels,jump_summary,borders,pager_labels,jump_labels;
static size_t nav_shift,seen_sources[8];
static int seen_count;
void epd_fill_rect(EpdRect rect,uint8_t color,uint8_t *fb){(void)rect;(void)color;(void)fb;}
void epd_fill_circle(int x,int y,int radius,uint8_t color,uint8_t *fb){(void)x;(void)y;(void)radius;(void)color;(void)fb;}
void epd_draw_circle(int x,int y,int radius,uint8_t color,uint8_t *fb){(void)x;(void)y;(void)radius;(void)color;(void)fb;}
void ui_clear_page(uint8_t *fb){(void)fb;}
void ui_fill_rect_fast(EpdRect rect,uint8_t gray,uint8_t *fb){(void)rect;(void)gray;(void)fb;}
void ui_fill_round_rect(uint8_t *fb,EpdRect r,int radius,uint8_t gray){(void)fb;(void)r;(void)radius;(void)gray;cards++;}
void ui_draw_round_rect(uint8_t *fb,EpdRect r,int radius,uint8_t gray){(void)fb;(void)r;(void)radius;assert(gray<=0x60);borders++;}
void ui_text_fixed(uint8_t *fb,int x,int y,int px,const char *text,enum EpdFontFlags align,bool inverse){
    (void)fb;(void)x;(void)y;(void)px;(void)align;(void)inverse;
    assert(!strchr(text,'\n') && !strchr(text,'\r') && !strchr(text,'\t'));
    if(px==30) large_titles++;
    if(strstr(text,"10页")){assert(px>=24);skip_labels++;}
    if(strstr(text,"上页")||strstr(text,"下页")){assert(px>=26);pager_labels++;}
    if(strstr(text,"按百分比跳转")){assert(px>=28);jump_labels++;}
    if(strstr(text,"第 99～105 节")) jump_summary++;
    labels++;
}
int ui_text_fixed_width_px(int px,const char *text){int width=0;for(;*text;text++)if(((unsigned char)*text&0xc0)!=0x80)width+=((unsigned char)*text<128?px/2:px);return width;}
int ui_text_effective_px(int px){return px;}
EpdRect ui_inset_rect(EpdRect r,int d){return (EpdRect){r.x+d,r.y+d,r.width-2*d,r.height-2*d};}
void ui_text_box(uint8_t *fb,EpdRect box,int px,const char *text,enum EpdFontFlags align,bool inverse){assert(box.width>0&&box.height>0);ui_text_fixed(fb,box.x,box.y,px,text,align,inverse);}
bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
void ui_hairline(uint8_t *fb,int y,int x,int width,uint8_t gray){(void)fb;(void)y;(void)x;(void)width;(void)gray;}
void ui_nav_status(uint8_t *fb){(void)fb;}
void ui_nav_back(uint8_t *fb,int x,int y){(void)fb;(void)x;(void)y;}
esp_err_t book_navigation_title(size_t position,char *out,size_t cap){
    size_t index=book_navigation_chapter(position);
    if(seen_count<8)seen_sources[seen_count++]=index;
    const char *name=index==0?"第一章\n重叠\t标题":"普通章节";
    snprintf(out,cap,"%s",name);
    return ESP_OK;
}
size_t book_navigation_chapter(size_t position){return position+nav_shift;}
int main(void){
    assert(book_toc_pages(0)==1 && book_toc_pages(7)==1 && book_toc_pages(8)==2);
    assert(book_toc_hit(58,110,0,8)==BOOK_TOC_BACK);
    assert(book_toc_hit(100,270,0,8)==0);
    assert(book_toc_hit(100,270,1,8)==7);
    assert(book_toc_hit(100,TOC_ROW_TOP+TOC_ROW_STEP,1,8)==-1);
    assert(book_toc_hit(600,1060,0,8)==BOOK_TOC_SKIP_NEXT);
    assert(book_toc_hit(98,1060,0,80)==BOOK_TOC_SKIP_PREV);
    assert(book_toc_hit(190,1060,0,80)==BOOK_TOC_PREV);
    assert(book_toc_hit(480,1060,0,80)==BOOK_TOC_NEXT);
    assert(book_toc_hit(320,1130,0,80)==BOOK_TOC_JUMP);
    assert(book_toc_jump_hit(120,880)==BOOK_TOC_JUMP_CANCEL);
    assert(book_toc_jump_hit(480,880)==BOOK_TOC_JUMP_CONFIRM);
    assert(book_toc_jump_hit(342,640)==BOOK_TOC_JUMP_TRACK);
    assert(book_toc_jump_percent(92)==0 && book_toc_jump_percent(592)==100);
    assert(book_toc_page_from_percent(0,24)==0);
    assert(book_toc_page_from_percent(4,24)==0);
    assert(book_toc_page_from_percent(60,24)==14);
    assert(book_toc_page_from_percent(100,24)==23);
    char title[160]; one_line(title,sizeof(title),"  第一章\n 重叠\t标题  ");
    assert(strcmp(title,"第一章 重叠 标题")==0);
    char long_title[160]; memset(long_title,'A',sizeof(long_title)-1);long_title[159]=0;
    fit_line(long_title,30,100);assert(strstr(long_title,"…")!=NULL);
    uint8_t fb=0;
    book_toc_render(&fb,"书名\n下一行",8,1,0,"");
    assert(cards==13 && labels>=17 && large_titles==7 && skip_labels==2);
    assert(borders==26 && pager_labels==2 && jump_labels==1);
    book_toc_render_jump(&fb,168,60);
    assert(jump_summary==1);
    nav_shift=1;seen_count=0;book_toc_render(&fb,"目录",4,1,0,"");
    assert(seen_count==4&&seen_sources[0]==1&&seen_sources[3]==4);
    puts("book_toc: pages, taps, overflow and single-line rendering passed");
    return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    directory = Path(tmp)
    for name, content in STUBS.items():
        (directory / name).write_text(content)
    harness = directory / "test.c"
    harness.write_text(HARNESS)
    output = directory / "test"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-I", str(directory),
                    "-I", str(SOURCE.parent), str(harness), "-o", str(output)], check=True)
    subprocess.run([str(output)], check=True)
