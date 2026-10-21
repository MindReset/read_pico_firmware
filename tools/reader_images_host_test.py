#!/usr/bin/env python3
"""中文：实际阅读插图函数的缓存、失败回退及灰阶边界回归。
English: Regress actual reader image caching, failure recovery and grayscale bounds.
SPDX-FileCopyrightText: 2026 mindreset
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'main/apps/app_book.c').read_text()

def function(name):
    match = re.search(r'^static [^\n]+\b' + name + r'\([^;{}]*\)\s*\{', source, re.M)
    assert match, name
    cleaned = re.sub(r'/\*.*?\*/|//[^\n]*', lambda m: ' ' * len(m[0]), source, flags=re.S)
    at, depth, quote, escaped = match.end(), 1, None, False
    while depth:
        c = cleaned[at]
        if quote:
            if escaped: escaped = False
            elif c == '\\': escaped = True
            elif c == quote: quote = None
        elif c in "\"'": quote = c
        elif c == '{': depth += 1
        elif c == '}': depth -= 1
        at += 1
    return source[match.start():at]

unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "html_text.h"
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {int leaf;} app_ctx_t;
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define UI_PX_CAPTION 20
#define EPD_DRAW_ALIGN_CENTER 0
#define BOOK_KIND_EPUB 1
#define TAG "test"
static void test_log(const char *tag,const char *format,...){(void)tag;(void)format;}
#define ESP_LOGW(...) test_log(__VA_ARGS__)
#define ESP_LOGI(...) test_log(__VA_ARGS__)
#define ESP_FAIL -1
enum {READING,SHELF};
typedef struct {uint8_t *gray;int width,height;} reader_image_t;
static reader_image_t *s_page_images;
static int s_page_image_count;
static size_t s_page_images_for=SIZE_MAX;
static uint32_t s_page_images_generation;
static size_t s_page,s_chapter=3,s_image_count;
static char **s_images;
static bool hidden,locked;
static int alloc_fail_after=-1,read_failure=-1,decode_failure=-1;
static unsigned reads,decodes,pixels,notices,invalidations;
static int reading_image;
static EpdRect body={40,20,100,200};
// 插图可用区域：默认等于正文栏，通栏用例把它换成更宽的一条带。
// Illustration region: the body column by default; the full-bleed case widens the band.
static EpdRect image_band={40,20,100,200};
static EpdRect reader_area_rect={40,20,100,200};
static int pixel_min_x=1<<30,pixel_max_x=-1,pixel_min_y=1<<30,pixel_max_y=-1;
static struct {int image,y,width,height;} slots[40];
static int slot_count;
static int whole_image=-1;
typedef struct {size_t chapter;char **images;size_t count;} reader_dims_ctx_t;
static reader_dims_ctx_t s_reader_dims_ctx;
static bool (*dims_fn)(void*,int,int*,int*);
static void *dims_ctx;
static size_t measured_chapter;
static uint32_t generation=1;
static bool app_settings_reader_hide_images(void){return hidden;}
static EpdRect body_rect(void){return body;}
static EpdRect reader_area(void){return reader_area_rect;}
static EpdRect book_layout_image_rect(void){return image_band;}
static size_t free_block=4u*1024u*1024u;
static size_t heap_caps_get_largest_free_block(int caps){(void)caps;return free_block;}
static size_t heap_caps_get_free_size(int caps){(void)caps;return free_block;}
static unsigned uxTaskGetStackHighWaterMark(void *task){(void)task;return 4096;}
// 竖排题头：本套用例只覆盖排版侧，几何一律留零。
// Vertical heading: this suite only covers the layout side, so the geometry stays zero.
typedef struct {int width,title_px,title_x,label_px,label_x,text_height;} reader_heading_t;
static reader_heading_t reader_heading_metrics(const char *title,const char *label,EpdRect area){
 (void)title;(void)label;(void)area;return (reader_heading_t){0};
}
static bool app_settings_book_vertical(void){return false;}
static uint32_t book_layout_generation(void){return generation;}
static int book_layout_page_image_count(size_t page){return page==s_page?slot_count:0;}
static int book_layout_page_image(size_t page){return page==s_page?whole_image:-1;}
static void book_layout_set_image_dims(bool (*fn)(void*,int,int*,int*),void *ctx){dims_fn=fn;dims_ctx=ctx;}
static bool book_layout_page_image_at(size_t page,int i,int *image,int *y,int *w,int *h){
 if(page!=s_page||i<0||i>=slot_count)return false;
 if(image)*image=slots[i].image;
 if(y)*y=slots[i].y;
 if(w)*w=slots[i].width;
 if(h)*h=slots[i].height;
 return true;
}
static bool fail_alloc(void){if(!alloc_fail_after)return true;if(alloc_fail_after>0)--alloc_fail_after;return false;}
static void *heap_caps_malloc(size_t n,int c){(void)c;if(n>free_block)return NULL;return fail_alloc()?NULL:malloc(n);}
static void *heap_caps_calloc(size_t n,size_t size,int c){(void)c;return fail_alloc()?NULL:calloc(n,size);}
static void invalidate_prep(void){++invalidations;}
static void copy_text(char *out,size_t size,const char *in){snprintf(out,size,"%s",in);}
static void fit_text(char *out,int px,int max){(void)px;if(max<40)out[0]=0;}
static const char *esp_err_to_name(int err){(void)err;return "error";}
static void vTaskDelay(unsigned ticks){(void)ticks;}
static esp_err_t book_chapter_image(size_t chapter,const char *path,uint8_t **out,size_t *size,bool *png){
 assert(path);if(locked)measured_chapter=chapter;else assert(chapter==s_chapter);++reads;reading_image=atoi(path);
 if(reading_image==read_failure)return ESP_FAIL;
 *out=malloc(1);assert(*out);*size=1;*png=false;return ESP_OK;
}
static bool book_image_dimensions(const uint8_t *data,size_t len,bool png,unsigned *w,unsigned *h){
 (void)png;assert(data&&len==1);*w=60;*h=20;return true;
}
static bool book_image_grayscale(const uint8_t *data,size_t len,bool png,unsigned w,unsigned h,uint8_t *gray){
 (void)png;assert(data&&len==1&&w&&h&&gray);++decodes;
 if(reading_image==decode_failure)return false;
 memset(gray,128,(size_t)w*h);return true;
}
static uint8_t ui_image_dither_gray(uint8_t g,int x,int y){(void)x;(void)y;assert(g==128);return g;}
static void epd_draw_pixel(int x,int y,uint8_t gray,uint8_t *fb){
 (void)fb;
 assert(x>=image_band.x&&x<image_band.x+image_band.width&&gray==128);++pixels;
 assert(y>=reader_area_rect.y&&y<reader_area_rect.y+reader_area_rect.height);
 if(x<pixel_min_x)pixel_min_x=x;
 if(x>pixel_max_x)pixel_max_x=x;
 if(y<pixel_min_y)pixel_min_y=y;
 if(y>pixel_max_y)pixel_max_y=y;
}
static void ui_text_vc(uint8_t *fb,int x,int y,int px,const char *text,int align,bool inv){
 (void)fb;(void)align;(void)inv;assert(x>=body.x&&x<=body.x+body.width&&y-px/2>=body.y&&y+px/2<=body.y+body.height&&text);++notices;
}
static const char s_path[]="test";
static void book_excerpt_offer(const char *path,const html_text_t *text){(void)path;(void)text;}
static char *s_text;
static blk_t *s_blocks;
static html_run_t *s_runs;
static size_t s_run_count;
static void book_layout_set_runs(const html_run_t *runs,size_t count){(void)runs;(void)count;}
static size_t s_text_len,s_block_count,s_selected_toc=2,s_jump_offset,s_jump_page;
static size_t s_chapter_lead_skip;
static unsigned s_chapter_lead_height,s_chapter_lead_row_height;
static unsigned s_chapter_lead_width;
static int s_px=10,s_view=READING;
static char s_message[128],s_chapter_heading_title[128],s_chapter_heading_label[32],s_title[128],s_font_path[128];
static int load_error,reflow_failures,reflows;
static void lock_draw(void){assert(!locked);locked=true;}
static void unlock_draw(void){assert(locked);locked=false;}
static int book_kind(void){return 0;}
static const char *ttf_font_path(void){return "font";}
static esp_err_t book_chapter_title(size_t chapter,char *out,size_t cap){snprintf(out,cap,"chapter%zu",chapter);return ESP_OK;}
void html_text_free(html_text_t *text){
 free(text->utf8);free(text->blocks);free(text->runs);for(size_t i=0;i<text->image_count;++i)free(text->images[i]);free(text->images);memset(text,0,sizeof(*text));
}
static esp_err_t book_chapter_load_blocks_target(size_t chapter,const char *anchor,size_t source,size_t *off,html_text_t *out){
 (void)chapter;(void)anchor;(void)source;assert(!locked);if(load_error)return ESP_FAIL;*off=4;
 out->utf8=strdup("new\nIMG");out->len=7;out->blocks=calloc(2,sizeof(blk_t));out->count=2;
 out->blocks[0]=(blk_t){.offset=0,.len=3,.image=-1};out->blocks[1]=(blk_t){.offset=4,.len=3,.image=0};
 out->images=calloc(1,sizeof(char*));out->images[0]=strdup("0");out->image_count=1;return ESP_OK;
}
static void book_layout_set_chapter_lead(size_t skip,unsigned height){(void)skip;(void)height;}
static void book_layout_set_chapter_lead_width(unsigned px){(void)px;}
static void apply_chapter_lead(const char *title,const char *label,size_t skip,unsigned height){
 (void)height;
 (void)title;(void)label;s_chapter_lead_skip=skip;s_chapter_lead_height=0;s_chapter_lead_width=0;
 book_layout_set_chapter_lead(skip,0);
}
static bool book_layout_build_blocks(const char *text,size_t len,const blk_t *blocks,size_t count,EpdRect r,int px){
 (void)text;(void)len;(void)blocks;(void)count;(void)r;(void)px;assert(locked);++reflows;++generation;
 if(dims_fn){int w=0,h=0;assert(dims_fn(dims_ctx,0,&w,&h)&&w==60&&h==20);}
 if(reflow_failures){--reflow_failures;return false;}return true;
}
/* 第 4 步起 load_chapter 经 reader_relayout_cols 单出口建版——本测试单栏语义，桥接桩
   恒等转发（cols 恒 1 时建版矩形即版心本身），栏化不在本测试语义内。
   From step 4 load_chapter builds through reader_relayout_cols — single-column semantics
   here, so the bridge forwards identically (at cols=1 the build rect is the body itself);
   columnization is out of scope. */
static int reader_column_count(void){return 1;}
static bool reader_relayout_cols(const char *text,size_t len,blk_t *blocks,size_t count,EpdRect body,int px,int cols){
 (void)cols;return book_layout_build_blocks(text,len,blocks,count,body,px);
}
static bool reader_relayout(EpdRect body){
 return reader_relayout_cols(s_text,s_text_len,s_blocks,s_block_count,body,s_px,1);
}

static size_t book_layout_page_count(void){return 2;}
static size_t book_layout_page_for_offset(size_t off){return off>=4?1:0;}
/* P0-1 视觉页地图身份桩：单栏下视觉页恒等于逻辑页，与生产退化路径一致；真实地图
   由 tools/reader_page_map_host_test.py 覆盖。/ P0-1 visual page map identity stub:
   in single column a visual page equals its logical page, matching the production
   degenerate path; the real map is covered by tools/reader_page_map_host_test.py. */
static size_t reader_visual_count(void){return book_layout_page_count();}
static size_t reader_visual_index(size_t logical){return logical;}
static size_t reader_visual_start(size_t visual){return visual;}
static size_t reader_visual_for_offset(size_t off){return book_layout_page_for_offset(off);}
static void release_page_images(void);
static void free_book(void){
 release_page_images();free(s_text);free(s_blocks);free(s_runs);for(size_t i=0;i<s_image_count;++i)free(s_images[i]);free(s_images);
 s_text=NULL;s_blocks=NULL;s_runs=NULL;s_run_count=0;s_images=NULL;s_image_count=0;
 book_layout_set_image_dims(NULL,NULL);s_reader_dims_ctx=(reader_dims_ctx_t){0};
}
'''
for name in ('inline_ink_gray', 'reader_image_slots', 'reader_image_slot', 'reader_image_dims', 'alloc_page_bitmap', 'release_page_images', 'prepare_inline_image', 'draw_reader_images', 'heading_text_matches', 'load_chapter_at'):
    unit += '\n' + function(name)
unit += r'''
static void setup_images(int count){
 release_page_images();for(size_t i=0;i<s_image_count;++i)free(s_images[i]);free(s_images);
 s_images=calloc((size_t)count,sizeof(char*));s_image_count=(size_t)count;slot_count=count;
 for(int i=0;i<count;++i){char path[16];snprintf(path,sizeof(path),"%d",i);s_images[i]=strdup(path);slots[i].image=i;slots[i].y=i*12;slots[i].width=20;slots[i].height=10;}
 ++generation;alloc_fail_after=-1;hidden=false;read_failure=decode_failure=-1;reads=decodes=pixels=notices=0;whole_image=-1;
}
int main(void){
 uint8_t fb=0;
 // Text-to-text within one layout keeps prefetch; image pages and reflows still invalidate it.
 setup_images(0);prepare_inline_image();unsigned prior_invalidations=invalidations;
 ++s_page;prepare_inline_image();assert(invalidations==prior_invalidations);
 ++generation;prepare_inline_image();assert(invalidations==prior_invalidations+1);
 setup_images(12);prepare_inline_image();assert(reads==12&&decodes==12&&s_page_image_count==12);
 draw_reader_images(&fb,s_page,body);assert(pixels==12*20*6&&!notices);
 prepare_inline_image();assert(reads==12);
 unsigned old_pixels=pixels;draw_reader_images(&fb,s_page+1,body);assert(pixels==old_pixels);
 ++generation;draw_reader_images(&fb,s_page,body);assert(pixels==old_pixels);
 prepare_inline_image();assert(reads==24);draw_reader_images(&fb,s_page,body);assert(pixels==24*20*6);
 hidden=true;prepare_inline_image();assert(!s_page_images&&!s_page_image_count);draw_reader_images(&fb,s_page,body);assert(pixels==24*20*6);
 hidden=false;prepare_inline_image();assert(reads==36&&s_page_image_count==12);
 // 未知尺寸槽仍解码，保持原始比例和灰阶；失败提示留在自己的位置。
 // Unknown-size slots still decode proportionally in grayscale; failures stay in their own slots.
 setup_images(1);slots[0].width=100;slots[0].height=200;
 prepare_inline_image();assert(s_page_images[0].width==60&&s_page_images[0].height==20);
 draw_reader_images(&fb,s_page,body);assert(pixels==1200);
 // PR9 整页回退没有混排记录，调用侧仍须显示图片。/ PR9's full-page fallback has no inline record but must still draw.
 setup_images(1);slot_count=0;whole_image=0;prepare_inline_image();
 assert(s_page_image_count==1&&s_page_images[0].gray&&reads==1);
 draw_reader_images(&fb,s_page,body);assert(pixels==1200);
 setup_images(3);for(int i=0;i<3;++i){slots[i].width=100;slots[i].height=40;slots[i].y=i*50;}
 read_failure=0;decode_failure=1;prepare_inline_image();draw_reader_images(&fb,s_page,body);
 assert(reads==3&&decodes==2&&notices==2&&pixels==60*20);
 // 数组及位图分配失败都安全释放，下一次布局代次可恢复。
 // Array and bitmap allocation failures release safely and recover on a new layout generation.
 setup_images(1);slots[0].width=100;slots[0].height=200;
 alloc_fail_after=0;prepare_inline_image();assert(!s_page_images&&!reads);draw_reader_images(&fb,s_page,body);assert(notices==1);
 alloc_fail_after=-1;++generation;prepare_inline_image();assert(s_page_images[0].gray);
 setup_images(1);slots[0].width=100;slots[0].height=200;
 alloc_fail_after=1;prepare_inline_image();assert(s_page_images&&!s_page_images[0].gray);draw_reader_images(&fb,s_page,body);assert(notices==1);
 alloc_fail_after=-1;++generation;prepare_inline_image();assert(s_page_images[0].gray);
 // 新章节加载/重排失败保留原章、图名、页码、目录选择及题头。
 // Failed chapter load/layout preserves the old chapter, images, page, selection and header.
 s_text=strdup("old\nIMG");s_text_len=7;s_blocks=calloc(2,sizeof(blk_t));s_block_count=2;
 s_blocks[0]=(blk_t){.offset=0,.len=3,.image=-1};s_blocks[1]=(blk_t){.offset=4,.len=3,.image=0};
 s_chapter=3;s_page=1;s_selected_toc=2;s_chapter_lead_skip=2;s_chapter_lead_height=12;
 strcpy(s_chapter_heading_title,"old title");char *old_text=s_text;char **old_images=s_images;app_ctx_t ctx={0};
 s_reader_dims_ctx=(reader_dims_ctx_t){s_chapter,s_images,s_image_count};book_layout_set_image_dims(reader_image_dims,&s_reader_dims_ctx);
 load_error=1;assert(!load_chapter_at(&ctx,4,0,false,NULL,SIZE_MAX));
 assert(s_text==old_text&&s_images==old_images&&s_chapter==3&&s_page==1&&s_selected_toc==2&&!locked);
 load_error=0;reflow_failures=1;assert(!load_chapter_at(&ctx,4,0,false,NULL,SIZE_MAX));
 assert(reflows==2&&s_text==old_text&&s_images==old_images&&s_chapter==3&&s_page==1&&s_selected_toc==2&&!locked);
 assert(s_chapter_lead_skip==2&&s_chapter_lead_height==12&&!strcmp(s_chapter_heading_title,"old title"));
 assert(measured_chapter==3&&s_reader_dims_ctx.images==old_images);
 assert(load_chapter_at(&ctx,4,0,false,NULL,SIZE_MAX));
 assert(s_chapter==4&&!strcmp(s_text,"new\nIMG")&&s_page==0&&s_selected_toc==SIZE_MAX&&!locked);
 assert(measured_chapter==4&&s_reader_dims_ctx.images==s_images);
 reflow_failures=2;assert(!load_chapter_at(&ctx,5,0,false,NULL,SIZE_MAX));assert(!s_text&&s_view==SHELF&&!locked);
 assert(!dims_fn&&!s_reader_dims_ctx.images);
 free_book();release_page_images();
 // 通栏位图放不进最大空闲块时按块大小缩一档，而不是丢掉整页插图。
 // A full-bleed bitmap that does not fit the largest free block steps down instead of dropping.
 setup_images(1);slots[0].width=100;slots[0].height=200;
 free_block=600;prepare_inline_image();
 assert(s_page_images[0].gray&&s_page_images[0].width==39&&s_page_images[0].height==12);
 pixel_min_x=1<<30;pixel_max_x=-1;
 draw_reader_images(&fb,s_page,body);assert(pixels==468&&pixel_min_x==70&&pixel_max_x==108);
 free_block=4u*1024u*1024u;release_page_images();
 // 页首插图贴到阅读区顶端，正文上方的呼吸空间只留给正文。
 // A page-start illustration sits at the top of the reading area; the inset above body text
 // stays with the text.
 setup_images(1);slots[0].width=100;slots[0].height=100;slots[0].y=0;
 reader_area_rect=(EpdRect){40,0,100,200};
 prepare_inline_image();assert(s_page_images[0].width==60&&s_page_images[0].height==20);
 pixel_min_y=1<<30;pixel_max_y=-1;
 draw_reader_images(&fb,s_page,body);assert(pixel_min_y==0&&pixel_max_y==19);
 reader_area_rect=body;release_page_images();
 // 通栏：插图在整屏宽的一条带里居中，正文栏的左右边距不再限制它。
 // Full bleed: the illustration centers in the panel-wide band and the body margins no longer
 // bound it.
 setup_images(1);slots[0].width=60;slots[0].height=20;
 image_band=(EpdRect){0,body.y,200,body.height};
 pixel_min_x=1<<30;pixel_max_x=-1;
 prepare_inline_image();assert(s_page_images[0].width==60&&s_page_images[0].height==20);
 draw_reader_images(&fb,s_page,body);
 assert(pixels==1200&&pixel_min_x==70&&pixel_max_x==129);
 image_band=body;
 release_page_images();
 puts("PASS: >8 images, layout cache generation, grayscale bounds, partial decode/OOM, fallback aspect fit, full bleed and chapter rollback");
}
'''
with tempfile.TemporaryDirectory() as folder:
    c, binary = Path(folder) / 'test.c', Path(folder) / 'test'
    c.write_text(unit)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    '-fsanitize=address,undefined', '-I'+str(root/'main/book'),
                    '-I'+str(root/'tools/html_text_stubs'), str(c), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
