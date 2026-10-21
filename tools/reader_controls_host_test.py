"""Actual font AA control and invariant bottom-edge tools gesture.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import re,subprocess,tempfile
from home_cover_cache_host_test import function
ROOT=Path(__file__).resolve().parents[1]
book=ROOT/'main/apps/app_book.c'
s=book.read_text()
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {bool consumed,user_activity;int64_t now_ms;} app_ctx_t;
typedef enum {UI_GESTURE_SWIPE_U,UI_GESTURE_SWIPE_D,UI_GESTURE_SWIPE_L,UI_GESTURE_SWIPE_R,UI_GESTURE_TAP,UI_GESTURE_CANCEL,UI_GESTURE_PRESS,UI_GESTURE_MOVE} ui_gesture_type_t;
typedef struct {ui_gesture_type_t type;unsigned x0,y0,x,y;} ui_gesture_event_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_PAGE} app_redraw_t;
enum {READING,SHELF,READER_PANEL_NONE,READER_PANEL_TOOLS,READER_PANEL_FONT_SETTINGS,READER_PANEL_LAYOUT_SETTINGS,READER_PANEL_BACKGROUND_PREVIEW};
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
static int s_view=READING,s_reader_panel=READER_PANEL_NONE,s_reader_slider=3,s_pressed_control=2;
static char *s_text="text";
static bool s_clear_confirm,s_scan_pending;
static struct {bool pending;} s_reader_tap;
static int64_t s_stats_activity_ms,s_reader_notice_until;
static unsigned invalidations;
static void invalidate_prep(void){++invalidations;}
static bool locked,aa=true,raster_aa=true,save_fail;
static void lock_draw(void){assert(!locked);locked=true;}
static void unlock_draw(void){assert(locked);locked=false;}
static bool app_settings_book_antialias(void){return aa;}
static bool vertical_book;
static bool app_settings_book_vertical(void){return vertical_book;}
static bool app_settings_set_book_antialias(bool on){assert(locked);if(save_fail)return false;aa=on;return true;}
static void ttf_set_antialias(bool on){assert(locked);raster_aa=on;}
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
static char s_reader_notice[64];
static void copy_text(char *to,size_t cap,const char *from){snprintf(to,cap,"%s",from);}
static int64_t esp_timer_get_time(void){return 1000000;}
'''
unit+='static unsigned font_scale=100;\nstatic unsigned app_settings_system_font_size(void){return font_scale;}\n'
unit+=function('reader_ui_extra',book)+'\n'+function('reader_font_card',book)+'\n'
for name in ['s_font_card','s_weight_card','s_bookfont_card','s_layout_card','s_rule_card','s_antialias_card']:
 unit+=re.search(r'^#define '+name+r' .*',s,re.M)[0]+'\n'
unit+=function('reader_tools_swipe',book)+'\n'+function('reader_swipe_direction',book)+'\n'
action=function('reader_panel_action',book)
a=action.index('        if (ui_rect_hit(s_antialias_card, x, y)) {')
b=action.index('        if (ui_rect_hit(layout, x, y)) {',a)
unit+='static app_redraw_t tap_aa(int x,int y){\n'+action[a:b]+'return APP_REDRAW_NONE;}\n'
unit+=r'''
static void reset(app_ctx_t *ctx){
 s_view=READING;s_text="text";s_reader_panel=READER_PANEL_NONE;s_clear_confirm=s_scan_pending=false;
 s_reader_tap.pending=true;s_reader_slider=3;s_pressed_control=2;invalidations=0;
 *ctx=(app_ctx_t){.now_ms=2200};
}
int main(void){
 app_ctx_t ctx;ui_gesture_event_t e={.type=UI_GESTURE_SWIPE_U,.x0=342,.y0=1160,.x=342,.y=980};
 for(int vertical=0;vertical<2;++vertical)for(int immersive=0;immersive<2;++immersive){
  (void)immersive;reset(&ctx);assert(reader_tools_swipe(&ctx,&e));
  assert(s_reader_panel==READER_PANEL_TOOLS&&!s_reader_tap.pending&&s_reader_slider==-1&&s_pressed_control==-1);
  assert(ctx.user_activity&&s_stats_activity_ms==2200&&invalidations==1);
  assert(!reader_tools_swipe(&ctx,&e)&&invalidations==1); // Already open stays open.
  assert(reader_swipe_direction(UI_GESTURE_SWIPE_U,vertical)==(vertical?1:0));
  assert(reader_swipe_direction(UI_GESTURE_SWIPE_L,vertical)==1);
 }
 for(unsigned y=1000;y<1220;++y){reset(&ctx);e.y0=y;bool allowed=y>=1096&&y<1216;assert(reader_tools_swipe(&ctx,&e)==allowed);if(!allowed)assert(s_reader_tap.pending&&!invalidations);}
 e.y0=1160;
 for(unsigned x=0;x<=684;++x){reset(&ctx);e.x0=x;assert(reader_tools_swipe(&ctx,&e)==(x<684));}e.x0=342;
 for(int type=UI_GESTURE_SWIPE_D;type<=UI_GESTURE_MOVE;++type){reset(&ctx);e.type=type;assert(!reader_tools_swipe(&ctx,&e));}e.type=UI_GESTURE_SWIPE_U;
 for(int panel=READER_PANEL_TOOLS;panel<=READER_PANEL_BACKGROUND_PREVIEW;++panel){reset(&ctx);s_reader_panel=panel;assert(!reader_tools_swipe(&ctx,&e));}
 reset(&ctx);ctx.consumed=true;assert(!reader_tools_swipe(&ctx,&e));
 reset(&ctx);s_clear_confirm=true;assert(!reader_tools_swipe(&ctx,&e));
 reset(&ctx);s_scan_pending=true;assert(!reader_tools_swipe(&ctx,&e));
 reset(&ctx);s_view=SHELF;assert(!reader_tools_swipe(&ctx,&e));
 reset(&ctx);s_text=NULL;assert(!reader_tools_swipe(&ctx,&e));
 const EpdRect cards[]={s_font_card,s_weight_card,s_bookfont_card,s_layout_card,s_rule_card,s_antialias_card};
 for(unsigned i=0;i<6;++i){EpdRect r=cards[i];assert(r.x>=36&&r.y>=580&&r.x+r.width<=648&&r.y+r.height<1141);
  for(unsigned j=0;j<i;++j){EpdRect q=cards[j];assert(r.x+r.width<=q.x||q.x+q.width<=r.x||r.y+r.height<=q.y||q.y+q.height<=r.y);}}
 assert(aa&&raster_aa);assert(tap_aa(400,1090)==APP_REDRAW_PAGE&&!aa&&!raster_aa&&!locked);
 save_fail=true;assert(tap_aa(400,1090)==APP_REDRAW_PAGE&&!aa&&!raster_aa&&!locked&&s_reader_notice_until==2800);
 save_fail=false;assert(tap_aa(400,1090)==APP_REDRAW_PAGE&&aa&&raster_aa&&!locked);
 assert(tap_aa(341,1090)==APP_REDRAW_NONE&&tap_aa(650,1090)==APP_REDRAW_NONE);
 puts("PASS: AA row hit bounds/save failures/cache invalidation, six nonoverlapping paired controls, bottom120px tools priority for either turn mode, already-open/modal/input cancellation and physical-key boundary");
}
'''
assert 'if (reader_tools_swipe(ctx, ev)) return APP_REDRAW_PAGE;' in function('gesture_event',book)
with tempfile.TemporaryDirectory(dir=ROOT/'build') as folder:
 p=Path(folder);(p/'test.c').write_text(unit)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
