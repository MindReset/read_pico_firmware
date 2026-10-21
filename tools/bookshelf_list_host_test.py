#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 中文：真实列表绘制、原比例封面和双行标题的边界检查与原生预览。
# English: Native list painting, aspect-fill covers, two-line bounds and a native preview.
import argparse
from pathlib import Path
import subprocess
import tempfile
import platform
from home_cover_cache_host_test import function
ROOT=Path(__file__).resolve().parents[1]
a=argparse.ArgumentParser();a.add_argument('--font',type=Path,default=ROOT/'main/assets/builtin.ttf');a.add_argument('--covers',type=Path);a.add_argument('--output',type=Path);args=a.parse_args()
with tempfile.TemporaryDirectory() as folder:
 p=Path(folder)
 (p/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n#define ESP_ERR_NO_MEM 0x101\n#define ESP_ERR_NOT_FOUND 0x105\n#define ESP_ERR_INVALID_RESPONSE 0x108\n')
 (p/'esp_log.h').write_text('#pragma once\nstatic inline void host_log(const char *tag,const char *fmt,...){(void)tag;(void)fmt;}\n#define ESP_LOGI(...) host_log(__VA_ARGS__)\n#define ESP_LOGW(...) host_log(__VA_ARGS__)\n#define ESP_LOGE(...) host_log(__VA_ARGS__)\n')
 (p/'esp_timer.h').write_text('#pragma once\n#include <stdint.h>\nstatic inline int64_t esp_timer_get_time(void){return 0;}\n')
 (p/'esp_heap_caps.h').write_text('''#pragma once
#include <stddef.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
void *heap_caps_malloc(size_t,int);
void *heap_caps_calloc(size_t,size_t,int);
void heap_caps_free(void*);
size_t heap_caps_get_free_size(int);
size_t heap_caps_get_largest_free_block(int);
''')
 (p/'app.h').write_text((ROOT/'tools/ui_gesture_stubs/app.h').read_text().replace('int64_t now_ms;', 'int64_t now_ms; int leaf;'))
 epd=(ROOT/'tools/ui_gesture_stubs/epdiy.h').read_text()+'\nvoid epd_fill_circle(int,int,int,uint8_t,uint8_t*);\n'
 (p/'epdiy.h').write_text(epd)
 builtin=(ROOT/'main/assets/builtin.ttf').read_bytes()
 assets='#include <stdint.h>\nconst uint8_t data[] asm("_binary_builtin_ttf_start")={'+','.join(map(str,builtin))+'};\n'
 assets+=f'asm(".globl _binary_builtin_ttf_end\\n.set _binary_builtin_ttf_end, _binary_builtin_ttf_start + {len(builtin)}");\n';(p/'assets.c').write_text(assets)
 unit=r'''
// 本测试自带按旋转换算的尺寸替身，必须在任何头被拉进来之前关掉 stub 的 static inline 版，
// 否则后到的 include 会先定义它、与下面的实现重定义。
// This test ships its own rotation-aware size shim, so the static inline one has to be
// switched off before any header is pulled in, or a later include defines it first.
#define EPDIY_STUB_NO_ROTATED_SIZE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "ttf_font.h"
#include "epdiy.h"
#include "ui_pinpad.h"
static struct {void *p;size_t bytes;} allocations[8192];
static size_t live,capacity=8*1024*1024;
static int fail_after=-1,fallbacks;
void *heap_caps_malloc(size_t n,int caps){assert(caps==3);if(fail_after==0)return NULL;if(fail_after>0)--fail_after;if(n>capacity-live)return NULL;void *v=malloc(n);if(!v)return NULL;for(int i=0;i<8192;++i)if(!allocations[i].p){allocations[i].p=v;allocations[i].bytes=n;live+=n;return v;}abort();}
void *heap_caps_calloc(size_t n,size_t z,int caps){if(z&&n>SIZE_MAX/z)return NULL;void *v=heap_caps_malloc(n*z,caps);if(v)memset(v,0,n*z);return v;}
void host_free(void *v){if(!v)return;for(int i=0;i<8192;++i)if(allocations[i].p==v){live-=allocations[i].bytes;allocations[i].p=NULL;free(v);return;}assert(0);}
void heap_caps_free(void *v){host_free(v);}
size_t heap_caps_get_free_size(int c){(void)c;return capacity-live;}
size_t heap_caps_get_largest_free_block(int c){(void)c;return capacity-live;}
void lock_pin_wipe(void *p,unsigned n){volatile uint8_t *b=p;while(n--)*b++=0;}
static enum EpdRotation display_rotation=EPD_ROT_PORTRAIT;
int epd_width(void){return display_rotation==EPD_ROT_PORTRAIT||display_rotation==EPD_ROT_INVERTED_PORTRAIT?1216:684;}
int epd_height(void){return display_rotation==EPD_ROT_PORTRAIT||display_rotation==EPD_ROT_INVERTED_PORTRAIT?684:1216;}
int epd_rotated_display_width(void){return 684;}int epd_rotated_display_height(void){return 1216;}
enum EpdRotation epd_get_rotation(void){return display_rotation;}
typedef struct {uint16_t x,y;} Coord_xy;
#define _swap_int(a,b) {int temp=a;a=b;b=temp;}
static uint8_t ui_contrast_gray(uint8_t gray){return gray;}
'''
 for n in ('_rotate','epd_draw_pixel','epd_get_pixel','epd_draw_hline','epd_draw_vline','epd_fill_rect','epd_draw_circle','epd_fill_circle_helper','epd_fill_circle','epd_write_line','epd_draw_line'):
  unit+=function(n,ROOT/'components/epdiy/src/epdiy.c')+'\n'
 for n in ('ui_rotate_rect_to_fb','ui_fill_rect_fast','ui_blit_mono_bitmap','clamp_radius','draw_arc','ui_draw_round_rect','ui_fill_round_rect','ui_inset_rect','ui_draw_control_frame','ui_draw_separator'):unit+=function(n,ROOT/'main/ui/ui_kit.c')+'\n'
 unit+=function('pixel',ROOT/'main/ui/ui_pinpad.c')+'\n'
 unit+='typedef struct {unsigned x,y,width,height;} book_crop_t;\n'
 unit+=function('book_cover_crop',ROOT/'main/book/book_cover.c')+'\n'
 unit+=r'''
#include "ui_image_dither.h"
#define UI_GRAY_WHITE 255
#define UI_GRAY_BLACK 0
#define UI_GRAY_LIGHT 224
#define BOOK_COVER_W 176
#define BOOK_COVER_H 240
static int s_view=0,style=5,s_pressed_control=-1;
enum {SHELF,BULK};
static unsigned s_cover_pending_mask;
#define SHELF_BOOK_LIFT_PX 16
#define SHELF_LIST_BOUNCE_PX 8
#define SHELF_FAST_COVER_STRIDE 21
#define SHELF_FAST_COVER_WIDTH 164
static struct {uint8_t *gray,*fast_bits;unsigned width,height;bool fast_white_edge;} s_covers[4];
typedef struct {char name[256],author[128];bool has_progress,favorite;unsigned pct;} shelf_entry_t;
static shelf_entry_t *s_shelf;
static int s_visible_count=4;
static int shelf_rows(void){return 4;}
static int app_settings_shelf_style(void){return style;}
static bool app_settings_main_fast_refresh(void){return false;}
static bool fast_cover_matches(int row,EpdRect r){(void)row;(void)r;return false;}
static int ui_content_width(void){return 612;}
#define UI_MARGIN 36
#define UI_GAP 20
#define UI_BTN_H 60
int ui_text_fixed_width_px(int px,const char *v){return ttf_text_width_px(px,v);}
static void ui_text_fixed_vc(uint8_t *fb,int x,int cy,int px,const char *v,enum EpdFontFlags align,bool inv){int a=0,b=0;ttf_measure_line_px(px,v,&a,&b);ttf_draw_text_px(fb,x,cy+(a-b)/2,px,v,align,inv?15:0,15);}
static void ui_text_fixed_ink_vc(uint8_t *fb,int x,int cy,int px,const char *v,enum EpdFontFlags align,uint8_t ink){int a=0,b=0;ttf_measure_line_px(px,v,&a,&b);ttf_draw_text_px(fb,x,cy+(a-b)/2,px,v,align,ink>>4,15);}
#define ui_text_vc ui_text_fixed_vc
static void ui_hairline(uint8_t *fb,int y,int x,int w,uint8_t c){epd_draw_hline(x,y,w,c,fb);}
static void copy_text(char *d,size_t n,const char *v){snprintf(d,n,"%s",v);}
static void fit_text(char *v,int px,int w){while(*v&&ui_text_fixed_width_px(px,v)>w){size_t n=strlen(v)-1;while(n&&((unsigned char)v[n]&0xc0)==0x80)--n;v[n]=0;}}
'''
 for n in ('row_rect','shelf_cover_image','shelf_cover_frame_ready','draw_favorite_icon','draw_shelf_favorite_icon','cover_favorite_needs_white_edge','draw_shelf_cover','paint_list_cover_feedback','draw_list_title','draw_list_row'):
  unit+=function(n,ROOT/'main/apps/app_book.c')+'\n'
 unit+=r'''
static void save(const char *path,uint8_t *frame){FILE *f=fopen(path,"wb");assert(f);for(int y=0;y<1216;++y)for(int x=0;x<684;++x){uint8_t v=(pixel(frame,x,y)>>4)*17;assert(fwrite(&v,1,1,f)==1);}assert(!fclose(f));}
int main(int argc,char **argv){
 assert(argc==4);assert(ttf_font_open(argv[1])==ESP_OK);uint8_t *fb=malloc(684*1216/2);assert(fb);
 memset(fb,0xee,684*1216/2);
 ui_text_fixed_vc(fb,36,44,22,"09:41",EPD_DRAW_ALIGN_LEFT,false);
 ui_text_fixed_vc(fb,578,44,22,"78%",EPD_DRAW_ALIGN_RIGHT,false);
 ui_draw_round_rect(fb,(EpdRect){602,33,37,20},4,0);epd_fill_rect((EpdRect){607,38,23,10},0,fb);
 ui_text_fixed_vc(fb,36,122,52,"书架",EPD_DRAW_ALIGN_LEFT,false);
 ui_fill_round_rect(fb,(EpdRect){442,94,97,54},18,255);ui_draw_round_rect(fb,(EpdRect){442,94,97,54},18,0x70);ui_text_fixed_vc(fb,490,121,20,"管理",EPD_DRAW_ALIGN_CENTER,false);
 ui_fill_round_rect(fb,(EpdRect){551,94,97,54},18,255);ui_draw_round_rect(fb,(EpdRect){551,94,97,54},18,0x70);ui_text_fixed_vc(fb,599,121,20,"+ 导入",EPD_DRAW_ALIGN_CENTER,false);
 epd_fill_rect((EpdRect){36,195,612,2},0x60,fb);ui_fill_round_rect(fb,(EpdRect){36,184,612,788},20,255);ui_draw_control_frame(fb,(EpdRect){36,184,612,788},20,0x50);
 shelf_entry_t items[4]={{"我与地坛","史铁生",true,true,42},{"十八岁出门远行","余华",true,false,18},{"夏天、烟火和我的尸体","乙一",false,true,0},{"活山","娜恩 · 谢泼德",true,false,100}};
 s_shelf=items;app_ctx_t ctx={.fb=fb};
 for(int i=0;i<4;++i){char path[1024];snprintf(path,sizeof(path),"%s/%d.raw",argv[2],i);FILE *f=fopen(path,"rb");assert(f);assert(fread(&s_covers[i].width,4,1,f)==1&&fread(&s_covers[i].height,4,1,f)==1);unsigned bytes=s_covers[i].width*s_covers[i].height;assert(bytes<=176*240);s_covers[i].gray=malloc(bytes);assert(s_covers[i].gray&&fread(s_covers[i].gray,1,bytes,f)==bytes);fclose(f);
 EpdRect r=shelf_cover_image(i);assert(r.x>=64&&r.x+r.width<=198&&r.y>=184+i*197&&r.y+r.height<=184+(i+1)*197);
 assert(r.x==71&&r.width==120&&r.height==156);
 book_crop_t crop=book_cover_crop(s_covers[i].width,s_covers[i].height,r.width,r.height);
 assert(crop.width&&crop.height&&crop.x+crop.width<=s_covers[i].width&&crop.y+crop.height<=s_covers[i].height);
 assert(abs((int)crop.width*r.height-(int)crop.height*r.width)<=r.height);
 draw_shelf_cover(fb,row_rect(i),i,items[i].name,items[i].favorite);draw_list_row(fb,i,&items[i]);
 // 真实像素验证上弹/回位只改变封面并集，不影响面板、文字与分隔线。
 // Actual pixels: bounce/settle touch only the cover union, keeping panel, text and separators.
 uint8_t *before=malloc(684*1216/2);assert(before);memcpy(before,fb,684*1216/2);
 s_pressed_control=i;EpdRect area=paint_list_cover_feedback(&ctx,i);
 assert(area.x==r.x&&area.y==r.y-8&&area.width==r.width&&area.height==r.height+8);
 assert(area.y>=184+i*197&&area.y+area.height<184+(i+1)*197);
 assert(((area.y&3)==(r.y&3)));
 for(int y=0;y<1216;++y)for(int x=0;x<684;++x)
  if(x<area.x||x>=area.x+area.width||y<area.y||y>=area.y+area.height)
   assert(pixel(before,x,y)==pixel(fb,x,y));
 assert(pixel(fb,r.x+20,r.y+r.height-1)==240);
 s_pressed_control=-1;paint_list_cover_feedback(&ctx,i);assert(!memcmp(before,fb,684*1216/2));
 free(before);free(s_covers[i].gray);}
 for(int row=0;row<3;++row)for(int yy=0;yy<2;++yy)assert(pixel(fb,300,184+row*197+196+yy)<128);
 assert(pixel(fb,36,260)<128&&pixel(fb,37,260)<128&&pixel(fb,38,260)>=240);
 ui_text_fixed_vc(fb,342,1060,17,"15本书 · 01/04",EPD_DRAW_ALIGN_CENTER,false);
 ui_text_fixed_vc(fb,226,1060,22,"<",EPD_DRAW_ALIGN_CENTER,false);ui_text_fixed_vc(fb,458,1060,22,">",EPD_DRAW_ALIGN_CENTER,false);
 epd_draw_hline(0,1096,684,0x90,fb);epd_fill_rect((EpdRect){224,1097,54,4},0,fb);
 const char *nav[]={"首页","书架","文件管理","设置"};for(int i=0;i<4;++i){ui_draw_round_rect(fb,(EpdRect){65+i*171,1132,35,34},3,0);ui_text_fixed_vc(fb,85+i*171,1190,17,nav[i],EPD_DRAW_ALIGN_CENTER,false);}
 save(argv[3],fb);
 // 同一占位帧不因某张封面先解码而变化，点按回弹也不提前露出单张封面。
 uint8_t *placeholder=malloc(684*1216/2);assert(placeholder);
 memset(fb,255,684*1216/2);s_cover_pending_mask=8;
 s_covers[0].gray=NULL;draw_shelf_cover(fb,row_rect(0),0,items[0].name,items[0].favorite);
 memcpy(placeholder,fb,684*1216/2);
 s_covers[0].width=176;s_covers[0].height=240;s_covers[0].gray=calloc(176*240,1);assert(s_covers[0].gray);
 draw_shelf_cover(fb,row_rect(0),0,items[0].name,items[0].favorite);assert(!memcmp(placeholder,fb,684*1216/2));
 paint_list_cover_feedback(&ctx,0);assert(!memcmp(placeholder,fb,684*1216/2));
 s_cover_pending_mask=0;draw_shelf_cover(fb,row_rect(0),0,items[0].name,items[0].favorite);
 assert(memcmp(placeholder,fb,684*1216/2));free(s_covers[0].gray);s_covers[0].gray=NULL;
 free(placeholder);
 // 长标题强制双行且仅触碰右侧文字区，避免作者和进度被覆盖。
 // A long title stays in the two-line text region without touching author/progress.
 memset(fb,255,684*1216/2);draw_list_title(fb,"名字很长名字很长名字很长名字很长名字很长名字很长名字很长",258);
 for(int y=0;y<1216;++y)for(int x=0;x<684;++x)if(pixel(fb,x,y)!=240)assert(x>=244&&x<622&&y>=237&&y<313);
 free(fb);ttf_font_unload();puts("PASS: native list painting and bounded bounce/settle with no extra frame; uniform boxes, aspect-fill crop, 4 rows, favorites, progress, two-line UTF-8 bounds");
}
'''
 (p/'test.c').write_text(unit)
 flags=['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-variable','-Wno-unused-function','-Wno-missing-field-initializers','-Wno-error=format-truncation','-fsanitize=address,undefined','-ffunction-sections','-fdata-sections','-I'+str(p),'-I'+str(ROOT/'main/ui'),'-I'+str(ROOT/'main/font'),'-I'+str(ROOT/'main')]
 subprocess.run(flags+['-c',str(ROOT/'main/font/ttf_font.c'),'-o',str(p/'ttf.o')],check=True)
 subprocess.run(flags+[str(p/'test.c'),str(p/'assets.c'),str(p/'ttf.o'),str(ROOT/'main/ui/ui_gesture.c'),str(ROOT/'main/ui/ui_image_dither.c'),('-Wl,-dead_strip' if platform.system()=='Darwin' else '-Wl,--gc-sections'),'-lm','-o',str(p/'test')],check=True)
 if args.covers or args.output:
  from PIL import Image
 for i in range(4):
  if args.covers:
   img=Image.open(args.covers/f'{i+1:02}.png').convert('L');img.thumbnail((176,240),Image.Resampling.LANCZOS)
  import struct
  if args.covers: dims=img.size; pixels=img.tobytes()
  else: dims=((176,80,150,176)[i],(120,240,150,240)[i]); pixels=bytes([80+i*45])*(dims[0]*dims[1])
  (p/f'{i}.raw').write_bytes(struct.pack('<II',*dims)+pixels)
 subprocess.run([str(p/'test'),str(args.font.resolve()),str(p),str(p/'list.raw')],check=True)
 if args.output:
  args.output.parent.mkdir(parents=True,exist_ok=True)
  Image.frombytes('L',(684,1216),(p/'list.raw').read_bytes()).save(args.output)
