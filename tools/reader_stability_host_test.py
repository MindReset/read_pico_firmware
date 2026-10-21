"""Native-resolution packed reading backgrounds and bounded ticket layout.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
ROOT=Path(__file__).resolve().parents[1]
BG=ROOT/'main/ui/ui_reader_background.c'
unit=r'''
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
#include <strings.h>
#include <sys/stat.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define BOOK_IMAGE_FILE_MAX (50u*1024u*1024u)
typedef struct {int x,y,width,height;} EpdRect;
enum {EPD_ROT_LANDSCAPE,EPD_ROT_PORTRAIT,EPD_ROT_INVERTED_LANDSCAPE,EPD_ROT_INVERTED_PORTRAIT};
static int rotation,device_w=64,device_h=96;
static bool missing,oom,shrink_fail,decode_fail;
static unsigned allocations,decodes,source_w=684,source_h=1216;
static size_t available=2u*1024u*1024u,largest=2u*1024u*1024u,live,peak;
static time_t modified=1;
static int mock_stat(const char *p,struct stat *st){(void)p;if(missing)return -1;memset(st,0,sizeof(*st));st->st_mode=S_IFREG;st->st_size=100;st->st_mtime=modified;return 0;}
#define stat(p,s) mock_stat(p,s)
static size_t heap_caps_get_free_size(unsigned caps){assert(caps==3);return available;}
static size_t heap_caps_get_largest_free_block(unsigned caps){assert(caps==3);return largest;}
static void *heap_caps_malloc(size_t bytes,unsigned caps){assert(caps==3&&!live&&bytes<=831744);if(oom)return NULL;live=bytes;if(live>peak)peak=live;++allocations;return malloc(bytes);}
static void *heap_caps_realloc(void *p,size_t bytes,unsigned caps){assert(caps==3&&p&&bytes==(live+1)/2);if(shrink_fail)return NULL;void *out=realloc(p,bytes);if(out)live=bytes;return out;}
static void test_free(void *p){if(p)live=0;free(p);}
#define free(p) test_free(p)
static bool book_image_file_dimensions(const char *p,bool png,unsigned *w,unsigned *h){(void)p;(void)png;*w=source_w;*h=source_h;return true;}
static uint8_t sample(unsigned x,unsigned y){return (uint8_t)(x*47u+y*37u);}
static bool book_image_file_grayscale(const char *p,bool png,unsigned w,unsigned h,uint8_t *out){(void)p;(void)png;++decodes;if(decode_fail)return false;for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)out[(size_t)y*w+x]=sample(x,y);return true;}
static int epd_width(void){return device_w;}
static int epd_height(void){return device_h;}
static int epd_get_rotation(void){return rotation;}
static int epd_rotated_display_width(void){return rotation%2?device_h:device_w;}
static int epd_rotated_display_height(void){return rotation%2?device_w:device_h;}
static uint8_t epd_get_pixel(int x,int y,int w,int h,const uint8_t *fb){assert(x>=0&&y>=0&&x<w&&y<h);return fb[(size_t)y*w+x];}
static void epd_draw_pixel(int x,int y,uint8_t gray,uint8_t *fb){
 int px=x,py=y;
 switch(rotation){case EPD_ROT_PORTRAIT:px=device_w-y-1;py=x;break;case EPD_ROT_INVERTED_LANDSCAPE:px=device_w-x-1;py=device_h-y-1;break;case EPD_ROT_INVERTED_PORTRAIT:px=y;py=device_h-x-1;break;default:break;}
 assert(px>=0&&px<device_w&&py>=0&&py<device_h);fb[(size_t)py*device_w+px]=gray;
}
'''
unit+=function('ui_image_dither_gray',ROOT/'main/ui/ui_image_dither.c')+'\n'
s=BG.read_text();start=s.index('static uint8_t *s_gray;');end=s.index('void ui_reader_background_release(')
unit+=s[start:end]
for n in ('ui_reader_background_release','ui_reader_background_prepare','read_pixel','ui_reader_background_paint'):unit+=function(n,BG)+'\n'
unit+=r'''
int main(void){
 assert(!ui_reader_background_prepare("/sdcard/not-pictures/a.jpg")&&!live);
 uint8_t fb[64*96];
 for(rotation=0;rotation<4;++rotation){
  assert(ui_reader_background_prepare("/sdcard/pictures/a.jpg"));
  int w=epd_rotated_display_width(),h=epd_rotated_display_height();
  assert(s_width==(unsigned)w&&s_height==(unsigned)h&&live==((size_t)w*h+1)/2);
  unsigned count=allocations,reads=decodes;
  for(int i=0;i<50;++i)assert(ui_reader_background_prepare("/sdcard/pictures/a.jpg"));
  assert(count==allocations&&reads==decodes);
  memset(fb,0xf0,sizeof(fb));ui_reader_background_paint(fb,(EpdRect){0,0,w,h},true);
  // Every source pixel survives at native resolution; no fade, upscale or enlarged blocks.
  for(int y=0;y<h;++y)for(int x=0;x<w;++x)assert(read_pixel(fb,x,y)==(ui_image_dither_gray(sample(x,y),x,y)&0xf0));
  uint8_t partial[64*96];memset(partial,0xf0,sizeof(partial));
  ui_reader_background_paint(partial,(EpdRect){0,0,w,h/2},true);
  ui_reader_background_paint(partial,(EpdRect){0,h/2,w,h-h/2},true);
  assert(!memcmp(fb,partial,sizeof(fb)));
  memset(partial,0xf0,sizeof(partial));ui_reader_background_paint(partial,(EpdRect){8,9,24,32},true);
  for(int y=9;y<41;++y)for(int x=8;x<32;++x)assert(read_pixel(fb,x,y)==read_pixel(partial,x,y));
  assert(read_pixel(partial,2,2)==0xf0);
  memset(partial,0xf0,sizeof(partial));epd_draw_pixel(12,12,0,partial);epd_draw_pixel(13,12,0x80,partial);
  ui_reader_background_paint(partial,(EpdRect){4,4,24,32},true);
  assert(read_pixel(partial,12,12)==0);
  assert(read_pixel(partial,13,12)==((((ui_image_dither_gray(sample(13,12),13,12)>>4)*8+7)/15)<<4));
  memset(partial,0xf0,sizeof(partial));ui_reader_background_paint(partial,(EpdRect){8,8,w/4,h/4},false);
  for(int y=0;y<h/4;++y)for(int x=0;x<w/4;++x)assert(read_pixel(partial,8+x,8+y)==(ui_image_dither_gray(sample(x*4,y*4),x*4,y*4)&0xf0));
  ui_reader_background_paint(partial,(EpdRect){-1,0,24,32},false);
  assert(count==allocations&&reads==decodes);
 }
 // Native device geometry stays full-size while shrinking the one buffer in place.
 ui_reader_background_release();rotation=0;device_w=684;device_h=1216;
 assert(ui_reader_background_prepare("/sdcard/pictures/native.png"));assert(s_width==684&&s_height==1216&&live==415872&&peak==831744);
 unsigned count=allocations,reads=decodes;assert(ui_reader_background_prepare("/sdcard/pictures/native.png")&&allocations==count&&decodes==reads);
 ++modified;assert(ui_reader_background_prepare("/sdcard/pictures/native.png")&&allocations==count+1);
 missing=true;assert(!ui_reader_background_prepare("/sdcard/pictures/native.png")&&!live);missing=false;
 available=1024;assert(!ui_reader_background_prepare("/sdcard/pictures/native.png")&&!live);available=2u*1024u*1024u;
 largest=100;assert(!ui_reader_background_prepare("/sdcard/pictures/native.png")&&!live);largest=2u*1024u*1024u;
 oom=true;assert(!ui_reader_background_prepare("/sdcard/pictures/native.png")&&!live);oom=false;
 decode_fail=true;assert(!ui_reader_background_prepare("/sdcard/pictures/native.png")&&!live);decode_fail=false;
 shrink_fail=true;assert(!ui_reader_background_prepare("/sdcard/pictures/native.png")&&!live);shrink_fail=false;
 device_w=63;device_h=95;assert(ui_reader_background_prepare("/sdcard/pictures/odd.png")&&live==(63u*95u+1)/2&&(s_gray[live-1]&15)==15);
 device_w=2000;assert(!ui_reader_background_prepare("/sdcard/pictures/odd.png")&&!live);
 puts("PASS: native 684x1216/16-gray wallpaper; original tones and single-pixel detail; preview matches reader; fixed partial repaints in four rotations; black/AA preserved; one 415872B cache/831744B decode plane; reuse/modified file/low memory/fragmentation/OOM/decode/shrink failure and odd-size packing");
}
'''
ticket=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#define EPD_DRAW_ALIGN_LEFT 0
static int scale=100,face_width=100,draws,min_y,max_y;
static int ui_text_effective_px(int px){return (px+2)*scale/100;}
static int ui_text_fixed_width_px(int px,const char *s){int width=0;for(const unsigned char *p=(void*)s;*p;++p)if((*p&0xc0)!=0x80)width+=*p<128?px/2:px;return width*face_width/100;}
static void ui_text_fixed(uint8_t *fb,int x,int y,int px,const char *s,int align,bool inv){(void)fb;(void)align;(void)inv;assert(x==82&&y>=min_y&&y+px<=max_y);assert(ui_text_fixed_width_px(px,s)<=520);++draws;}
'''
ticket+=function('text_fit',ROOT/'main/book/book_ticket.c')+'\n'+function('ticket_stat',ROOT/'main/book/book_ticket.c')+'\n'
ticket+=r'''
int main(void){uint8_t fb[1];char line[128];
 for(scale=100;scale<=200;scale+=25)for(face_width=85;face_width<=150;face_width+=5){
  min_y=914;max_y=962;snprintf(line,sizeof(line),"今日阅读 %llu 分钟  ·  翻页 %u 次",(unsigned long long)(UINT64_MAX/60),UINT32_MAX);ticket_stat(fb,min_y,48,28,line);
  min_y=969;max_y=1043;snprintf(line,sizeof(line),"累计阅读 %llu 小时 %llu 分钟  ·  翻页 %u 次",(unsigned long long)(UINT64_MAX/3600),(unsigned long long)59,UINT32_MAX);ticket_stat(fb,min_y,74,28,line);
  min_y=802;max_y=856;ticket_stat(fb,min_y,54,34,"已读 100%  ·  第 4294967295 章");
 }assert(draws);puts("PASS: UINT64 durations/UINT32 turns and chapters remain within ticket at 100-200% UI and varying font widths");}
'''
with tempfile.TemporaryDirectory(dir=ROOT/'build') as directory:
 for name,code in [('background',unit),('ticket',ticket)]:
  c=Path(directory)/(name+'.c');exe=Path(directory)/name;c.write_text(code)
  subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
