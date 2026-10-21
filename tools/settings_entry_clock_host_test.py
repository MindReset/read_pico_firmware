"""Actual settings row culling/arrow glyph independence and reader clock refresh.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
ROOT=Path(__file__).resolve().parents[1]
settings=ROOT/'main/apps/app_device_settings.c'; book=ROOT/'main/apps/app_book.c'
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#define UI_NAV_TOP 1096
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
#define UI_GRAY_WHITE 255
#define UI_GRAY_BLACK 0
#define SETTINGS_ROW_H settings_main_row_height()
#define SETTINGS_ICON_PX 32
#define UI_ICON_CHEVRON_RIGHT 7
#define EPD_DRAW_ALIGN_RIGHT 1
#define EPD_DRAW_ALIGN_LEFT 0
#define EPD_DRAW_SUCCESS 0
enum EpdDrawMode {MODE_GL16=1};
#define E0470_WAVEFORM 1
#define ESP_LOGI(...) ((void)0)
typedef struct {int x,y,width,height;}EpdRect;
typedef int app_redraw_t;
enum {APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE,APP_REDRAW_FULL};
enum {READING,SHELF};
typedef struct {bool touched;}touch_t;
typedef struct {touch_t *touch;uint8_t *fb;void *hl;}app_ctx_t;
static unsigned scale=100,text_calls,icons,arrows,measures,profile_calls;
static int s_main_scroll,last_label_y;
static unsigned pairs;
static int ui_text_effective_px(int px){return (px+2)*scale/100;}
static int ui_text_fixed_width_px(int px,const char *value){++measures;return (int)strlen(value)*px/3;}
static void ui_text(uint8_t *fb,int x,int y,int px,const char *text,int align,bool inv){(void)fb;(void)x;(void)y;(void)px;(void)align;(void)inv;assert(!strstr(text,"›"));++text_calls;}
static void ui_text_vc(uint8_t *fb,int x,int y,int px,const char *text,int align,bool inv){if(x==100)last_label_y=y;if(x==594){assert(y==last_label_y);++pairs;}ui_text(fb,x,y,px,text,align,inv);}
static void ui_draw_icon(uint8_t *fb,int x,int y,int px,int icon,int gray){(void)fb;(void)x;(void)y;(void)px;(void)gray;assert(icon==UI_ICON_CHEVRON_RIGHT);++arrows;}
static void setting_icon(uint8_t *fb,int index,int x,int y){(void)fb;(void)index;(void)x;(void)y;++icons;}
static void settings_card(uint8_t *fb,EpdRect r,int radius,int fill,int edge){(void)fb;(void)r;(void)radius;(void)fill;(void)edge;}
static void settings_divider(uint8_t *fb,int y,int x,int width){(void)fb;(void)y;(void)x;(void)width;}
'''
for name in ('settings_main_row_height','settings_fit_text','fit_value','settings_value','setting_group'):
 unit+=function(name,settings)+'\n'
unit+=r'''
static int s_view=READING;
static char *s_text="book";
static bool s_reader_fullscreen,immersive,s_reader_clock_pending,wallpaper;
static int64_t s_reader_clock_minute=-1,s_reader_clock_painted_minute=-1,minute=100;
enum {READER_PANEL_NONE,READER_PANEL_BACKGROUND_PREVIEW};
static int s_reader_panel;
static uint8_t fb[684*1216],next_fb[684*1216];
static bool locked;
static unsigned status_paints,background_paints,display_calls,failed;
static int64_t ui_nav_clock_minute(void){return minute;}
static bool app_settings_reader_immersive(void){return immersive;}
static const char *app_settings_book_background_path(void){return wallpaper?"/sdcard/pictures/bg.jpg":"";}
static void lock_draw(void){assert(!locked);locked=true;}
static void unlock_draw(void){assert(locked);locked=false;}
static void epd_fill_rect(EpdRect r,int color,uint8_t *frame){assert(r.x==0&&r.y==0&&r.width==684&&r.height==80);memset(frame,color,684*80);}
static void ui_nav_status(uint8_t *frame){++status_paints;frame[20*684+36]=(uint8_t)minute;}
static void ui_reader_background_paint(uint8_t *frame,EpdRect r,bool blend){assert(r.height==80&&blend);++background_paints;frame[0]=29;}
enum EpdDrawError {OK=0,FAULT=1};
static enum EpdDrawError update_display_area_diff_with(void *h,const void *wave,int mode,EpdRect area){(void)h;(void)wave;assert(!locked&&mode==MODE_GL16&&area.y==0&&area.height==80);++display_calls;return failed?FAULT:OK;}
static void guard_draw_result(void *h,int e){(void)h;(void)e;}
'''
# &E0470_WAVEFORM requires a real object, preserving the actual refresh call.
unit=unit.replace('#define E0470_WAVEFORM 1','static const int E0470_WAVEFORM=1;')
for name in ('reader_status_area','reader_status_visible','reader_status_paint','reader_clock_tick'):
 unit+=function(name,book)+'\n'
present=function('present',book)
branch=present[present.index('    if (s_reader_clock_pending) {'):present.index('    // 输入不准备')]
unit+='static bool clock_present(app_ctx_t *ctx,app_redraw_t redraw){\n'+branch+'\nreturn false;}\n'
# Actual prefetched-page path; freshness is checked after copying, before any push.
unit+=r'''
static bool s_water_turn_pending,s_reader_turn_pending,s_reader_image_refresh_pending,s_toolbar,s_clear_confirm,s_reader_footer_pending;
static uint8_t *s_next_fb=next_fb,*s_page_images;
static int s_next_page=2,s_page=2,s_mode;
static EpdRect s_area;
static int64_t esp_timer_get_time(void){return 1000;}
static bool ui_reader_background_prepare(const char *p){(void)p;return true;}
static void prepare_inline_image(void){}
static bool app_settings_reader_hide_images(void){return false;}
static int book_layout_page_image_count(int p){(void)p;return 0;}
static size_t fb_bytes(void){return sizeof(fb);}
static void draw_reader(uint8_t *f,int p){(void)f;(void)p;assert(false);}
static EpdRect reader_area(void){return (EpdRect){0,160,684,900};}
'''
unit+=function('paint_reading',book)+'\n'
files=ROOT/'main/apps/app_files.c'
unit+=r'''
typedef struct {bool mounted,space_valid;uint64_t filesystem_bytes,free_bytes,capacity_bytes;}read_pico_sd_info_t;
'''
for name in ('storage_space_valid','storage_amount','storage_usage'):unit+=function(name,files)+'\n'
unit+=r'''
int main(void){
 uint8_t pixels[1]={0};const int marks[]={0,1,2,3,4,5};
 const char *labels[]={"WiFi","系统字体","系统对比度","主页刷新模式","自动休眠锁屏","关机睡眠"};
 const char *values[]={"很长的网络名称","用户导入的非常长的字体名称","100%","水波纹","10 分钟","先浅后深"};
 for(scale=100;scale<=200;scale+=10){
  assert(SETTINGS_ROW_H==82);pairs=0;
  text_calls=icons=arrows=measures=0;s_main_scroll=0;
  setting_group(pixels,1600,"hidden",1649,marks,labels,values,6);
  assert(!text_calls&&!icons&&!arrows&&!measures);
  setting_group(pixels,300,"visible",349,marks,labels,values,6);
  unsigned visible=(1096-349+SETTINGS_ROW_H-1)/SETTINGS_ROW_H;
  if(visible>6)visible=6;
  assert(icons==visible&&arrows==visible&&text_calls==1+visible*2&&pairs==visible);
  s_main_scroll=1500;text_calls=icons=arrows=measures=0;
  setting_group(pixels,300,"hidden",349,marks,labels,values,6);
  assert(!text_calls&&!icons&&!arrows&&!measures);
 }
 scale=100;arrows=0;settings_value(pixels,627,200,21,"network  ›");assert(arrows==1);
 settings_value(pixels,627,200,21,"›");assert(arrows==2);
 settings_value(pixels,627,200,21,"network");assert(arrows==2);
 app_ctx_t ctx={.fb=fb};memset(fb,77,sizeof(fb));
 assert(reader_clock_tick(&ctx)==APP_REDRAW_AREA&&s_reader_clock_pending);
 assert(fb[80*684]==77&&fb[20*684+36]==100);
 assert(clock_present(&ctx,APP_REDRAW_AREA)&&display_calls==1&&s_reader_clock_minute==100);
 assert(reader_clock_tick(&ctx)==APP_REDRAW_NONE&&display_calls==1);
 ++minute;touch_t touch={true};ctx.touch=&touch;
 assert(reader_clock_tick(&ctx)==APP_REDRAW_NONE);ctx.touch=NULL;
 s_reader_fullscreen=immersive=true;assert(reader_clock_tick(&ctx)==APP_REDRAW_NONE);
 immersive=false;wallpaper=true;
 assert(reader_clock_tick(&ctx)==APP_REDRAW_AREA&&background_paints==1&&fb[0]==29);
 failed=1;assert(clock_present(&ctx,APP_REDRAW_AREA)&&s_reader_clock_minute==100);
 assert(reader_clock_tick(&ctx)==APP_REDRAW_AREA);failed=0;
 assert(clock_present(&ctx,APP_REDRAW_AREA)&&s_reader_clock_minute==101);
 minute=102;assert(reader_clock_tick(&ctx)==APP_REDRAW_AREA);++minute;
 assert(clock_present(&ctx,APP_REDRAW_AREA)&&s_reader_clock_minute==102);
 assert(reader_clock_tick(&ctx)==APP_REDRAW_AREA);assert(clock_present(&ctx,APP_REDRAW_AREA)&&s_reader_clock_minute==103);
 minute=90;assert(reader_clock_tick(&ctx)==APP_REDRAW_AREA);assert(clock_present(&ctx,APP_REDRAW_AREA)&&s_reader_clock_minute==90);
 memset(next_fb,15,sizeof(next_fb));next_fb[20*684+36]=1;minute=110;
 assert(paint_reading(&ctx,MODE_GL16)==APP_REDRAW_AREA);
 assert(fb[20*684+36]==110&&fb[80*684]==15&&s_area.y==160);
 s_view=SHELF;++minute;assert(reader_clock_tick(&ctx)==APP_REDRAW_NONE);
 read_pico_sd_info_t info={.mounted=true,.space_valid=true,.filesystem_bytes=128ULL*1024*1024*1024,.free_bytes=100ULL*1024*1024*1024,.capacity_bytes=256ULL*1024*1024*1024};
 char total[32],usage[96];storage_usage(&info,total,sizeof(total),usage,sizeof(usage));
 assert(!strcmp(total,"128.00 GB")&&strstr(usage,"28.00 GB")&&strstr(usage,"100.00 GB"));
 info.free_bytes=info.filesystem_bytes-1024*1024;
 storage_usage(&info,total,sizeof(total),usage,sizeof(usage));assert(strstr(usage,"1.0 MB"));
 info.free_bytes=info.filesystem_bytes+1;assert(!storage_space_valid(&info));
 info.space_valid=false;storage_usage(&info,total,sizeof(total),usage,sizeof(usage));assert(strstr(usage,"暂不可用")&&!strstr(usage,"256.00 GB"));
 info.space_valid=true;info.free_bytes=0;assert(storage_space_valid(&info));
 puts("PASS: visible-only settings glyph work, missing-chevron fonts, 11 scales, status-minute locality/immersive touch deferral/background/error retry/time correction, fresh prefetch, large partition/small-file/invalid-space display");
}
'''
# Render may not decode/read NVS, and clock prefix may not prepare/cache whole pages.
s=function('render',settings)
assert 'ui_wallpaper_draw_rounded' not in s and 'read_pico_transfer_get_saved_wifi' not in s
assert 'prepare_inline_image' not in branch and 'kick_prep' not in branch
with tempfile.TemporaryDirectory(dir=ROOT/'build') as folder:
 d=Path(folder);c=d/'test.c';exe=d/'test';c.write_text(unit)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-variable','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
