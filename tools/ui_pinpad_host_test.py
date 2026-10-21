#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 中文：真实TTF、键盘与EPD绘图的内存/取消/局部像素测试，可输出原生预览。
# English: Real TTF/pad/EPD memory, cancellation and local-pixel tests, with optional native preview.
import argparse
from pathlib import Path
import subprocess
import tempfile
import platform
from home_cover_cache_host_test import function
ROOT=Path(__file__).resolve().parents[1]
a=argparse.ArgumentParser();a.add_argument('--font',type=Path,default=ROOT/'main/assets/builtin.ttf');a.add_argument('--background',type=Path);a.add_argument('--output',type=Path);args=a.parse_args()
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
 (p/'app.h').write_text((ROOT/'tools/ui_gesture_stubs/app.h').read_text())
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
// r93 起 pinpad 的锁形图标走 ui_kit 的图标库（4bpp 掩模 + 类型在此）；
// Since r93 the pad's lock icon goes through ui_kit's icon set (4bpp masks and the type live here).
#include "assets/ui_icons.h"
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
// r93 起 ui_kit 的圆角矩形与 pinpad 的图标/底栏引用这两个绘制内核（4bpp packed
// 填充 + 图标掩模渲染）；本测试不链接 ui_kit.c，按既有抽取模式取真实实现（见下方
// function() 抽取列表），此处只放声明——签名与 ui_kit.h 逐字一致，晚到的 ui_kit.h
// 不会冲突。
// Since r93 the rounded rect and the pad's icon/underline reference these two drawing
// kernels (4bpp packed fill + icon-mask rendering); this test does not link ui_kit.c,
// so the real implementations are extracted below and only declarations live here --
// byte-identical to ui_kit.h, so the later include cannot conflict.
void ui_fill_rect_fast(EpdRect rect, uint8_t gray, uint8_t *framebuffer);
EpdRect ui_rotate_rect_to_fb(EpdRect rect);
void ui_draw_icon(uint8_t* framebuffer, int cx, int cy, int size, ui_icon_t icon, uint8_t gray);
'''
 for n in ('_rotate','epd_draw_pixel','epd_get_pixel','epd_draw_hline','epd_draw_vline','epd_fill_rect','epd_draw_circle','epd_fill_circle_helper','epd_fill_circle'):
  unit+=function(n,ROOT/'components/epdiy/src/epdiy.c')+'\n'
 # r93 抽取表新增五个真实内核：读像素/掩模取样/矩形旋转/4bpp 快填/图标渲染
# （ui_draw_round_rect 与 pinpad 的图标、底栏都调用它们；依赖的 epd_draw_pixel、
# epd_get_pixel 已在上方 epdiy 抽取列表，ui_contrast_gray 为恒等桩）。
# Five real kernels added for r93: pixel read / mask sampling / rect rotation /
# 4bpp fast fill / icon rendering (called by ui_draw_round_rect and the pad's
# icon and underline; their deps epd_draw_pixel/epd_get_pixel are already in the
# epdiy list above and ui_contrast_gray is the identity stub).
 for n in ('clamp_radius','draw_arc','ui_draw_round_rect','ui_read_pixel','icon_nibble','ui_rotate_rect_to_fb','ui_fill_rect_fast','ui_draw_icon'):unit+=function(n,ROOT/'main/ui/ui_kit.c')+'\n'
 unit+=r'''
int ui_font_title_px(int n,const char *v){(void)v;return n;}
void ui_font_measure_line_px(int n,const char *v,int *a,int *b){(void)v;*a=n;*b=0;}
void ui_font_draw_text_px(uint8_t *f,int x,int y,int n,const char *v,enum EpdFontFlags a,uint8_t fg,uint8_t bg,bool bw){(void)f;(void)x;(void)y;(void)n;(void)v;(void)a;(void)bw;assert(fg<16&&bg<16);++fallbacks;}
#define free host_free
#include "ui_pinpad.c"
#undef free
static ui_pin_result_t event(ui_pinpad_t *p,ui_gesture_type_t type,int x,int y,EpdRect *area){ui_gesture_event_t e={.type=type,.x=x,.y=y,.x0=x,.y0=y};return ui_pinpad_handle(p,&e,area);}
static void save(const char *path,uint8_t *frame){FILE *f=fopen(path,"wb");assert(f);for(int y=0;y<1216;++y)for(int x=0;x<684;++x){uint8_t v=pixel(frame,x,y);assert(fwrite(&v,1,1,f)==1);}assert(!fclose(f));}
int main(int argc,char **argv){
 assert(argc==6);assert(ttf_font_open(argv[1])==ESP_OK);
 uint8_t *fb=malloc(684*1216/2),*old=malloc(684*1216/2),*bg=malloc(684*1216/2);assert(fb&&old&&bg);
 memset(bg,255,684*1216/2);FILE *background=fopen(argv[2],"rb");if(background){for(int y=0;y<1216;++y)for(int x=0;x<684;++x){int v=fgetc(background);assert(v>=0);epd_draw_pixel(x,y,v,bg);}fclose(background);}else for(int y=0;y<1216;++y)for(int x=0;x<684;++x)epd_draw_pixel(x,y,((x/52+y/48)&1)?240:32,bg);
 ui_pinpad_t pad={0};ui_pinpad_begin(&pad,bg,"输入密码");assert(pad.background&&pad.pressed==-1&&!pad.count);
 ui_pinpad_paint(fb,&pad,ui_pinpad_full());save(argv[3],fb);
 // 同一完整密码页重画保持像素与缓存不变；输入只修改控件。/ Repainting the final PIN page retains pixels/cache; input updates controls only.
 memcpy(old,fb,684*1216/2);size_t before_entry=live;
 ui_pinpad_paint(fb,&pad,ui_pinpad_full());save(argv[5],fb);
 assert(!memcmp(old,fb,684*1216/2)&&live==before_entry);
 uint8_t *back=malloc(684*1216/2);memcpy(back,pad.background,684*1216/2);size_t resident=live;
 EpdRect dirty;memcpy(old,fb,684*1216/2);assert(event(&pad,UI_GESTURE_PRESS,342,703,&dirty)==UI_PIN_CHANGED&&pad.pressed==5&&pad.count==0);
 ui_pinpad_paint_input(fb,&pad);save(argv[4],fb);assert(live==resident&&!memcmp(back,pad.background,684*1216/2));
 for(int y=0;y<1216;++y)for(int x=0;x<684;++x)if(x<dirty.x||x>=dirty.x+dirty.width||y<dirty.y||y>=dirty.y+dirty.height)assert(pixel(old,x,y)==pixel(fb,x,y));
 assert(pixel(fb,342,637)==0&&pixel(fb,342,631)==0);assert(pixel(fb,241,401)==0);
 for(int y=dirty.y;y<dirty.y+dirty.height;++y)for(int x=dirty.x;x<dirty.x+dirty.width;++x)if(pixel(old,x,y)!=pixel(fb,x,y)){int dx=x-342,dy=y-703;assert((dx*dx+dy*dy<=68*68)|| (y>=389&&y<=413&&x>=229&&x<=253));assert(pixel(fb,x,y)==0||pixel(fb,x,y)==240);}
 assert(event(&pad,UI_GESTURE_TAP,342,703,&dirty)==UI_PIN_CHANGED&&pad.count==1&&!strcmp(pad.digits,"5"));ui_pinpad_paint_input(fb,&pad);
 assert(event(&pad,UI_GESTURE_PRESS,158,544,&dirty)==UI_PIN_CHANGED);assert(event(&pad,UI_GESTURE_CANCEL,158,544,&dirty)==UI_PIN_CHANGED&&pad.count==1&&pad.pressed==-1);
 assert(event(&pad,UI_GESTURE_TAP,158,544,&dirty)==UI_PIN_NONE);
 assert(event(&pad,UI_GESTURE_PRESS,158,544,&dirty)==UI_PIN_CHANGED);assert(event(&pad,UI_GESTURE_MOVE,0,0,&dirty)==UI_PIN_CHANGED);assert(event(&pad,UI_GESTURE_TAP,158,544,&dirty)==UI_PIN_NONE&&pad.count==1);
 assert(event(&pad,UI_GESTURE_PRESS,158,544,&dirty)==UI_PIN_CHANGED);assert(event(&pad,UI_GESTURE_LONG_PRESS,158,544,&dirty)==UI_PIN_CHANGED);assert(event(&pad,UI_GESTURE_TAP,158,544,&dirty)==UI_PIN_NONE);
 for(int i=0;i<3;++i){assert(event(&pad,UI_GESTURE_PRESS,342,1021,&dirty)==UI_PIN_CHANGED);assert(event(&pad,UI_GESTURE_TAP,342,1021,&dirty)==(i==2?UI_PIN_COMPLETE:UI_PIN_CHANGED));}assert(!strcmp(pad.digits,"5000"));assert(event(&pad,UI_GESTURE_PRESS,342,703,&dirty)==UI_PIN_NONE);
 ui_pinpad_reset(&pad,NULL,"");assert(!pad.count&&!pad.digits[0]);
 assert(event(&pad,UI_GESTURE_PRESS,158,544,&dirty)==UI_PIN_CHANGED);assert(event(&pad,UI_GESTURE_TAP,158,544,&dirty)==UI_PIN_CHANGED);
 assert(event(&pad,UI_GESTURE_PRESS,540,1132,&dirty)==UI_PIN_CHANGED);assert(event(&pad,UI_GESTURE_TAP,540,1132,&dirty)==UI_PIN_CHANGED&&!pad.count&&!pad.digits[0]);
 assert(event(&pad,UI_GESTURE_PRESS,144,1132,&dirty)==UI_PIN_CHANGED);assert(event(&pad,UI_GESTURE_TAP,144,1132,&dirty)==UI_PIN_CANCEL);
 pad.blocked=true;assert(event(&pad,UI_GESTURE_PRESS,158,544,&dirty)==UI_PIN_NONE);
 assert(event(&pad,UI_GESTURE_PRESS,144,1132,&dirty)==UI_PIN_CHANGED);assert(event(&pad,UI_GESTURE_TAP,144,1132,&dirty)==UI_PIN_CANCEL);
 int before=fallbacks;fail_after=0;ui_pinpad_paint(fb,&pad,ui_pinpad_full());assert(fallbacks>before);fail_after=-1;
 ui_pinpad_end(&pad);ttf_font_cache_clear();size_t base=live;
 for(int i=0;i<2;++i){fail_after=i;ui_pinpad_begin(&pad,bg,"输入密码");assert(!pad.background);ui_pinpad_end(&pad);fail_after=-1;assert(live==base);}
 capacity=live+1024*1024;ui_pinpad_begin(&pad,bg,"输入密码");assert(!pad.background);ui_pinpad_end(&pad);capacity=8*1024*1024;
 for(int rot=0;rot<4;++rot){display_rotation=rot;ui_pinpad_begin(&pad,bg,"输入密码");ui_pinpad_paint(fb,&pad,ui_pinpad_full());ui_pinpad_end(&pad);ttf_font_cache_clear();assert(live==base);}
 // 真实手势取消：多指、读错、长按不提交。/ Real recognizer cancellation: multitouch/read errors/holds never commit.
 ui_pinpad_begin(&pad,bg,"输入密码");ui_gesture_t gesture={0};cst836u_touch_t touch={.touched=true,.count=1,.x=158,.y=544};app_ctx_t ctx={.touch=&touch,.now_ms=100,.pressed=true};ui_gesture_event_t e;
 assert(ui_gesture_feed(&gesture,&ctx,&e));assert(ui_pinpad_handle(&pad,&e,&dirty)==UI_PIN_CHANGED);
 ctx.pressed=false;ctx.now_ms=110;touch.count=2;assert(ui_gesture_feed(&gesture,&ctx,&e)&&e.type==UI_GESTURE_CANCEL);assert(ui_pinpad_handle(&pad,&e,&dirty)==UI_PIN_CHANGED&&!pad.count);
 touch.count=1;ctx.pressed=true;ctx.now_ms=120;assert(ui_gesture_feed(&gesture,&ctx,&e));ui_pinpad_handle(&pad,&e,&dirty);ctx.consumed=true;ctx.pressed=false;ctx.now_ms=130;assert(ui_gesture_feed(&gesture,&ctx,&e)&&e.type==UI_GESTURE_CANCEL);ui_pinpad_handle(&pad,&e,&dirty);assert(!pad.count);
 ui_pinpad_end(&pad);free(back);free(bg);free(old);free(fb);ttf_font_unload();puts("PASS: real imported native glyphs/EPD paint; no plaintext, immediate press preview, exactly four release digits, delete/cancel/move/hold/multitouch/read-error; cached backdrop unchanged, local pixels, rotation and allocation failure cleanup");
}
'''
 (p/'test.c').write_text(unit)
 # ttf_font.c 的有界 snprintf 会被 gcc 判为可能截断；与 book_ui、lock_collage 等测试一样只降这一级。
 # gcc flags the bounded snprintf in ttf_font.c as possibly truncating; downgrade just that one
 # check, the same way the book_ui and lock_collage tests already do.
 flags=['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-variable','-Wno-unused-function','-Wno-missing-field-initializers','-Wno-error=format-truncation','-fsanitize=address,undefined','-ffunction-sections','-fdata-sections','-I'+str(p),'-I'+str(ROOT/'main/ui'),'-I'+str(ROOT/'main/font'),'-I'+str(ROOT/'main')]
 subprocess.run(flags+['-c',str(ROOT/'main/font/ttf_font.c'),'-o',str(p/'ttf.o')],check=True)
 subprocess.run(flags+[str(p/'test.c'),str(p/'assets.c'),str(p/'ttf.o'),str(ROOT/'main/ui/ui_gesture.c'),str(ROOT/'main/ui/ui_image_dither.c'),('-Wl,-dead_strip' if platform.system()=='Darwin' else '-Wl,--gc-sections'),'-lm','-o',str(p/'test')],check=True)
 if args.background:
  from PIL import Image
  b=Image.open(args.background).convert('L');assert b.size==(684,1216);(p/'bg.raw').write_bytes(b.tobytes())
 subprocess.run([str(p/'test'),str(args.font.resolve()),str(p/'bg.raw'),str(p/'idle.raw'),str(p/'pressed.raw'),str(p/'entry.raw')],check=True)
 if args.output:
  from PIL import Image
  args.output.mkdir(parents=True,exist_ok=True)
  # 原生帧读取值是灰阶编号<<4，PNG按编号*17显示真实白端点。
  # Native reads return level<<4; map PNG to level*17 for the actual white endpoint.
  for state in ('idle','pressed','entry'):Image.frombytes('L',(684,1216),(p/f'{state}.raw').read_bytes()).point(lambda v:(v>>4)*17).save(args.output/f'password-{state}.png')
