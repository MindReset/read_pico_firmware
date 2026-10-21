"""Production Home minute presentation, clock-face bounds and midnight rollover.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1];s=(root/'main/apps/app_dashboard.c').read_text()
def function(name):
 start=s.index('static ',s.index(name)-32);brace=s.index('{',s.index(name));i=brace+1;depth=1
 while depth:
  depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[start:i]
helpers='\n'.join(function(n) for n in ['int64_t date_stamp(', 'ui_home_clock_style_t clock_style(', 'EpdRect time_clock_bounds(', 'EpdRect component_clock_bounds(', 'EpdRect component_clock_graphics(', 'bool visible_motion_clock(', 'enum EpdDrawError present_clock_area(', 'enum EpdDrawError present_clock_motion('])
present=function('bool present(')
tick=function('app_redraw_t on_tick(')
media_start=tick.index('    if(s_view==HOME_ROOT||folder_preview){');brace=tick.index('{',media_start);media_end=brace+1;depth=1
while depth:
 depth+=(tick[media_end]=='{')-(tick[media_end]=='}');media_end+=1
tick=tick[:media_start]+tick[media_end:];tick=tick[:tick.index('    home_book_t *books[3]')]+'return APP_REDRAW_NONE;}\n'
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include "app.h"
typedef struct{int x,y,width,height;}EpdRect;
enum EpdDrawError{EPD_DRAW_SUCCESS,EPD_DRAW_ERROR};
enum{APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE,APP_REDRAW_FULL,MODE_GL16,MODE_DU,HOME_ROOT,HOME_GOAL,HOME_COMPONENT,HOME_TIME};
typedef int app_redraw_t;
typedef enum{APP_HOME_FOLDER,APP_HOME_CLOCK,APP_HOME_ASTERISK=2,APP_HOME_MOON=4,APP_HOME_KEHAI=5,APP_HOME_DALI=6,APP_HOME_CAT=7,APP_HOME_HOUSE=8,APP_HOME_COMPONENT_COUNT}app_home_component_t;
typedef enum{UI_HOME_CLOCK_PS1995,UI_HOME_CLOCK_ASTERISK,UI_HOME_CLOCK_MOON,UI_HOME_CLOCK_KEHAI,UI_HOME_CLOCK_DALI,UI_HOME_CLOCK_CAT,UI_HOME_CLOCK_HOUSE,UI_HOME_CLOCK_COUNT}ui_home_clock_style_t;
static app_home_component_t s_component_browse;
static bool s_component_pending;
static unsigned s_widget_phase,s_widget_phase_shown,s_time_phase,s_time_phase_shown,subphase;
static int64_t s_motion_ms=-100;
static unsigned clock_phase_now(struct tm *d){return (unsigned)d->tm_sec*10+subphase;}
static bool clock_motion_due(app_ctx_t *ctx){return ctx->now_ms<=0||ctx->now_ms<s_motion_ms||ctx->now_ms-s_motion_ms>=100;}
static EpdRect ui_rect_union(EpdRect a,EpdRect b){if(!a.width)return b;if(!b.width)return a;int x=a.x<b.x?a.x:b.x,y=a.y<b.y?a.y:b.y,right=a.x+a.width>b.x+b.width?a.x+a.width:b.x+b.width,bottom=a.y+a.height>b.y+b.height?a.y+a.height:b.y+b.height;return(EpdRect){x,y,right-x,bottom-y};}
static const app_home_component_t s_components[]={APP_HOME_MOON,APP_HOME_CLOCK,APP_HOME_KEHAI,APP_HOME_CAT,APP_HOME_DALI,APP_HOME_ASTERISK,APP_HOME_HOUSE,APP_HOME_FOLDER};
#define HOME_COMPONENT_OPTIONS (sizeof(s_components)/sizeof(s_components[0]))
static EpdRect s_pressed;
static int picker_bodies,picker_controls;
static void render_components_body(uint8_t *fb){(void)fb;++picker_bodies;}
static void render_component_controls(uint8_t *fb){(void)fb;++picker_controls;}
static void ui_draw_selected_round_rect(uint8_t *fb,EpdRect r,int radius){(void)fb;assert(r.width>0&&radius>0);}
static bool s_widget_pending,s_time_pending;
static struct tm s_widget_date,s_widget_shown,s_time_date;
static int64_t s_widget_stamp=-1,s_time_stamp=-1;
static int time_paints;
static int E0470_WAVEFORM,E0470_FOLLOW_WAVEFORM,widget=APP_HOME_FOLDER,s_view=HOME_ROOT;
static const int hold_gray=1,hold_binary=2;
static const int *pushed_wave;
static int pushed_mode;
static const int *display_main_gray_waveform(void){return &hold_gray;}
static const int *display_main_bw_waveform(void){return &hold_binary;}
static EpdRect s_area,pushed;
static bool s_clock_pending,s_home_force_full_once,s_press_pending,fail,s_reading_days_valid=true;
static int64_t s_clock_minute=-2,minute=100;
static int paints,clock_paints,pushes,fulls,clock_faces,record_reads;
static struct tm s_date={.tm_year=126,.tm_yday=285},date={.tm_year=126,.tm_yday=285};
static int app_settings_home_component(void){return widget;}
static bool fast;
static bool app_settings_main_fast_refresh(void){return fast;}
static int64_t ui_nav_clock_minute(void){return minute;}
static bool date_now(struct tm *out){*out=date;return minute>=0;}
static void refresh_days(void){s_date=date;++record_reads;}
static void epd_fill_rect(EpdRect area,int gray,uint8_t *fb){(void)fb;assert(gray==255&&area.x>=0&&area.y>=0&&area.x+area.width<=684&&area.y+area.height<=1216);}
static void ui_fill_rect_fast(EpdRect area,int gray,uint8_t *fb){epd_fill_rect(area,gray,fb);}
static void clock_base_prepare(void){}
static void ui_nav_clock_draw(uint8_t *fb){(void)fb;++clock_paints;}
static void ui_home_clock_style_draw_region(uint8_t *fb,EpdRect rect,const struct tm *d,bool binary,ui_home_clock_style_t style,EpdRect crop){(void)fb;(void)d;(void)style;assert(binary==fast);assert(rect.width==320||rect.width==420||rect.width==460);assert(crop.width<=rect.width&&crop.height<=rect.height);++clock_faces;}
static EpdRect ui_home_clock_phase_area(EpdRect rect,unsigned a,unsigned b){(void)a;(void)b;return(EpdRect){rect.x+80,rect.y+40,42,48};}
static bool ui_home_clock_has_motion(ui_home_clock_style_t v){return v==UI_HOME_CLOCK_KEHAI||v==UI_HOME_CLOCK_CAT;}
static EpdRect ui_home_clock_motion_area(EpdRect b,unsigned a,unsigned z,ui_home_clock_style_t v){(void)v;return ui_home_clock_phase_area(b,a,z);}
static void ui_home_clock_phase_draw_region(uint8_t *fb,EpdRect rect,const struct tm *d,bool binary,ui_home_clock_style_t style,EpdRect crop,unsigned phase){(void)phase;ui_home_clock_style_draw_region(fb,rect,d,binary,style,crop);}
static void clock_draw_region(uint8_t *fb,EpdRect r,const struct tm *d,bool b,ui_home_clock_style_t style,EpdRect clip,unsigned phase){ui_home_clock_phase_draw_region(fb,r,d,b,style,clip,phase);}
static void render_time_value(uint8_t *fb,const struct tm *d){(void)fb;(void)d;++time_paints;}
static enum EpdDrawError update_display_area_diff_with(void *hl,const int *wave,int mode,EpdRect area){(void)hl;assert(mode==MODE_GL16||mode==MODE_DU);pushed_wave=wave;pushed_mode=mode;pushed=area;++pushes;return fail?EPD_DRAW_ERROR:EPD_DRAW_SUCCESS;}
static void guard_draw_result(void *hl,enum EpdDrawError result){(void)hl;(void)result;}
static enum EpdDrawError update_display_full(void *hl){(void)hl;++fulls;return EPD_DRAW_SUCCESS;}
static enum EpdDrawError update_display_fast_page(void *hl){(void)hl;return EPD_DRAW_SUCCESS;}
static void render(app_ctx_t *ctx,uint8_t *fb){(void)ctx;(void)fb;++paints;}
'''+helpers+'\n'+present+'\n'+tick+r'''
int main(void){
 app_ctx_t ctx={0};present(&ctx,APP_REDRAW_FULL);assert(s_clock_minute==100&&paints==1&&fulls==1);
 for(int i=0;i<100;++i)assert(on_tick(&ctx)==APP_REDRAW_NONE);
 minute=101;ctx.consumed=true;assert(on_tick(&ctx)==APP_REDRAW_NONE);ctx.consumed=false;
 cst836u_touch_t touch={.touched=true};ctx.touch=&touch;assert(on_tick(&ctx)==APP_REDRAW_NONE);touch.touched=false;
 assert(on_tick(&ctx)==APP_REDRAW_AREA);present(&ctx,APP_REDRAW_AREA);
 assert(paints==1&&clock_paints==1&&pushes==1&&pushed.x+pushed.width==180&&pushed.y+pushed.height==62&&s_clock_minute==101);
 minute=102;fail=true;on_tick(&ctx);present(&ctx,APP_REDRAW_AREA);
 assert(s_clock_minute==101&&on_tick(&ctx)==APP_REDRAW_AREA);fail=false;present(&ctx,APP_REDRAW_AREA);assert(s_clock_minute==102);
 widget=APP_HOME_CLOCK;minute=103;on_tick(&ctx);present(&ctx,APP_REDRAW_AREA);
 assert(clock_faces==1&&paints==1&&pushed.x==78&&pushed.y==684&&pushed.width==198&&pushed.height==198);
 s_view=HOME_GOAL;minute=104;on_tick(&ctx);present(&ctx,APP_REDRAW_AREA);assert(clock_faces==1);
 s_view=HOME_ROOT;minute=105;date.tm_yday++;assert(on_tick(&ctx)==APP_REDRAW_PAGE&&record_reads==1);present(&ctx,APP_REDRAW_PAGE);
 fast=true;minute=106;on_tick(&ctx);present(&ctx,APP_REDRAW_AREA);assert(clock_faces==2&&pushed.width==198&&pushed.height==198);
 s_date=date;widget=APP_HOME_KEHAI;s_view=HOME_ROOT;present(&ctx,APP_REDRAW_PAGE);int old_paints=paints,old_faces=clock_faces;
 date.tm_sec=1;assert(on_tick(&ctx)==APP_REDRAW_AREA);present(&ctx,APP_REDRAW_AREA);assert(paints==old_paints&&clock_faces==old_faces+1&&pushed.width==42&&pushed.height==48);
 assert(pushed_wave==&E0470_FOLLOW_WAVEFORM&&pushed_mode==MODE_DU);
 date.tm_sec=2;fail=true;on_tick(&ctx);present(&ctx,APP_REDRAW_AREA);assert(s_widget_stamp!=date_stamp(&date));fail=false;assert(on_tick(&ctx)==APP_REDRAW_AREA);present(&ctx,APP_REDRAW_AREA);assert(s_widget_stamp==date_stamp(&date));
 s_view=HOME_COMPONENT;s_component_browse=APP_HOME_KEHAI;present(&ctx,APP_REDRAW_PAGE);date.tm_sec=3;assert(on_tick(&ctx)==APP_REDRAW_AREA);present(&ctx,APP_REDRAW_AREA);assert(pushed.width==42);
 assert(pushed_wave==&E0470_FOLLOW_WAVEFORM&&pushed_mode==MODE_DU);
 fast=false;date.tm_sec=4;on_tick(&ctx);present(&ctx,APP_REDRAW_AREA);assert(pushed_wave==&E0470_FOLLOW_WAVEFORM&&pushed_mode==MODE_DU);
 s_view=HOME_GOAL;date.tm_sec=4;assert(on_tick(&ctx)==APP_REDRAW_NONE);
 s_view=HOME_TIME;s_date=date;present(&ctx,APP_REDRAW_PAGE);old_paints=paints;date.tm_sec=5;assert(on_tick(&ctx)==APP_REDRAW_AREA);present(&ctx,APP_REDRAW_AREA);assert(paints==old_paints&&time_paints==1&&pushed.y<=840&&pushed.y+pushed.height==938);
 assert(pushed_wave==&E0470_FOLLOW_WAVEFORM&&pushed_mode==MODE_DU);
 widget=APP_HOME_CLOCK;date.tm_sec=6;assert(on_tick(&ctx)==APP_REDRAW_AREA);present(&ctx,APP_REDRAW_AREA);assert(time_paints==2&&pushed.x==72&&pushed.y==840&&pushed.width==540&&pushed.height==98);
 date.tm_sec=7;fail=true;on_tick(&ctx);present(&ctx,APP_REDRAW_AREA);assert(s_time_stamp!=date_stamp(&date));fail=false;assert(on_tick(&ctx)==APP_REDRAW_AREA);present(&ctx,APP_REDRAW_AREA);assert(s_time_stamp==date_stamp(&date));
 date.tm_yday++;assert(on_tick(&ctx)==APP_REDRAW_PAGE&&record_reads==2);present(&ctx,APP_REDRAW_PAGE);s_view=HOME_ROOT;
 minute=-1;assert(on_tick(&ctx)==APP_REDRAW_AREA);present(&ctx,APP_REDRAW_AREA);assert(on_tick(&ctx)==APP_REDRAW_NONE);
 assert(record_reads==2);
 minute=107;date.tm_min++;s_date=date;s_view=HOME_COMPONENT;s_component_browse=APP_HOME_MOON;s_component_pending=true;old_paints=paints;
 fail=true;assert(on_tick(&ctx)==APP_REDRAW_AREA);present(&ctx,APP_REDRAW_AREA);assert(s_component_pending&&paints==old_paints&&picker_bodies==1);
 fail=false;assert(on_tick(&ctx)==APP_REDRAW_AREA);present(&ctx,APP_REDRAW_AREA);assert(!s_component_pending&&paints==old_paints&&picker_bodies==2&&pushed.y==260&&pushed.height==876&&pushed_wave==&hold_gray);
 s_press_pending=true;s_pressed=s_area=(EpdRect){36,472,64,64};int faces=clock_faces;present(&ctx,APP_REDRAW_AREA);assert(picker_controls==1&&clock_faces==faces&&paints==old_paints);s_press_pending=false;
 s_view=HOME_ROOT;widget=APP_HOME_KEHAI;ctx.now_ms=1000;s_date=date;present(&ctx,APP_REDRAW_PAGE);subphase=1;ctx.now_ms=1050;assert(on_tick(&ctx)==APP_REDRAW_NONE);ctx.now_ms=1100;assert(on_tick(&ctx)==APP_REDRAW_AREA);present(&ctx,APP_REDRAW_AREA);assert(s_widget_phase_shown==(unsigned)date.tm_sec*10+1&&pushed_wave==&E0470_FOLLOW_WAVEFORM);subphase=7;ctx.now_ms=1500;assert(on_tick(&ctx)==APP_REDRAW_AREA);present(&ctx,APP_REDRAW_AREA);assert(s_widget_phase_shown==(unsigned)date.tm_sec*10+7);ctx.now_ms=0;subphase=0;
 for(int binary=0;binary<2;++binary)for(unsigned i=0;i<HOME_COMPONENT_OPTIONS-1;++i){
  fast=binary;widget=s_components[i];s_view=HOME_ROOT;s_date=date;present(&ctx,APP_REDRAW_PAGE);minute++;date.tm_min++;
  assert(on_tick(&ctx)==APP_REDRAW_AREA);present(&ctx,APP_REDRAW_AREA);
  if(!ui_home_clock_has_motion(clock_style(widget)))assert(pushed_wave==(binary?&hold_binary:&hold_gray)&&pushed_mode==(binary?MODE_DU:MODE_GL16));
 }

 puts("PASS: minute status/face bounds, visible-only Home/picker Kehai seconds, time-page digit crops, failed-output retry, touch deferral, midnight records unchanged-face hold waveforms in gray/binary modes and no full-frame second paints");
}
'''
with tempfile.TemporaryDirectory() as directory:
 c=Path(directory)/'clock.c';binary=c.with_suffix('');c.write_text(unit)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root/'tools/ui_gesture_stubs'),str(c),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
