"""中文：真实设置页恢复分段滑动后的手势、三模式刷新与边界回归。
English: Real discrete settings gestures, three-mode presentation and boundary regression.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'main/apps/app_device_settings.c').read_text()

def block(text, start):
    at = text.index('{', start)
    depth, quote, comment, escape = 0, None, None, False
    while at < len(text):
        char, pair = text[at], text[at:at+2]
        if comment == 'line':
            if char == '\n': comment = None
        elif comment == 'block':
            if pair == '*/': comment = None; at += 1
        elif quote:
            if escape: escape = False
            elif char == '\\': escape = True
            elif char == quote: quote = None
        elif pair == '//': comment = 'line'; at += 1
        elif pair == '/*': comment = 'block'; at += 1
        elif char in "\"'": quote = char
        elif char == '{': depth += 1
        elif char == '}':
            depth -= 1
            if not depth: return text[start:at+1]
        at += 1
    raise AssertionError('Unclosed function')

def function(name):
    match = re.search(r'^static [^\n;{}]+\b'+name+r'\([^;{}]*\)\s*\{', source, re.M)
    assert match, name
    return block(source, match.start())

assert not any(word in source for word in ('ui_follow_', 'display_follow_', 's_scroll_cache', 'coasting', 'lighten_text'))
assert 'scroll_gesture(' not in function('on_tick')
start = source.index('if (s_page == SETTINGS_MAIN || s_page == SETTINGS_BLUETOOTH || s_page == SETTINGS_BLE_SCAN) {', source.index('static app_redraw_t on_gesture'))
event_block = block(source, start)
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "epdiy.h"
#include "app.h"
#include "ui_gesture.h"
typedef enum {APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE,APP_REDRAW_FULL} app_redraw_t;
enum {SETTINGS_MAIN,SETTINGS_BLUETOOTH,SETTINGS_BLE_SCAN,SETTINGS_UPGRADE,SETTINGS_SHELF_STYLE,
      SETTINGS_SYSTEM_FONT,SETTINGS_WALLPAPER,SETTINGS_AVATAR,SETTINGS_TEXT_EDIT,SETTINGS_PIN_ENTRY};
enum EpdDrawMode {MODE_GC16,MODE_GL16,MODE_DU};
#define UI_NAV_TOP 1096
#define UI_NAV_REFRESH_END 1088
#define UI_LOCK_WIDTH 684
#define SETTINGS_SCROLL_MAX 833
static const int E0470_WAVEFORM=0,E0470_FOLLOW_WAVEFORM=1;
static int s_page,s_main_scroll,s_ble_scroll,selected_mode,paint_mode;
static bool s_scroll_drag_consumed,s_scroll_present_pending,s_input_layout,s_input_settle;
static EpdRect s_input_area;
static int paints,pushes,row_taps;
static EpdRect pushed;
static enum EpdDrawMode pushed_mode;
static void render(app_ctx_t *ctx,uint8_t *fb){(void)ctx;(void)fb;++paints;paint_mode=selected_mode;}
static int update_display_area_diff_with(void *hl,const int *wave,enum EpdDrawMode mode,EpdRect area){
 (void)hl;assert(wave==&E0470_WAVEFORM||wave==&E0470_FOLLOW_WAVEFORM);++pushes;pushed=area;pushed_mode=mode;return 0;
}
static int update_display_area_full_with(void *hl,const int *wave,enum EpdDrawMode mode,EpdRect area){return update_display_area_diff_with(hl,wave,mode,area);}
static void guard_draw_result(void *hl,int status){(void)hl;assert(!status);}
static void display_main_transition_cancel(void){}
static EpdRect ui_pinpad_full(void){return(EpdRect){0,0,684,1216};}
static EpdRect upgrade_progress_area(void){return(EpdRect){52,468,580,64};}
typedef struct {int content_bottom;} layout_t;
static layout_t ble_layout(void){return(layout_t){2000-s_ble_scroll};}
static layout_t ble_scan_layout(void){return(layout_t){2000};}
''' + function('scroll_gesture') + '\n' + function('settings_present')
unit += '\nstatic app_redraw_t event(app_ctx_t *ctx,const ui_gesture_event_t *ev){(void)ctx;'+event_block+'if(ev->type==UI_GESTURE_TAP)++row_taps;return APP_REDRAW_NONE;}\n'
unit += r'''
static ui_gesture_t recognizer;
static cst836u_touch_t touch;
static app_ctx_t ctx;
static app_redraw_t feed(int x,int y,bool down,bool press,int64_t now){
 touch=(cst836u_touch_t){.count=down?1:0,.touched=down,.x=x,.y=y};
 ctx=(app_ctx_t){.touch=&touch,.pressed=press,.released=!down,.now_ms=now};
 ui_gesture_event_t gesture;
 if(!ui_gesture_feed(&recognizer,&ctx,&gesture))return APP_REDRAW_NONE;
 app_redraw_t redraw=event(&ctx,&gesture);
 if(redraw==APP_REDRAW_AREA)assert(settings_present(&ctx,redraw));
 return redraw;
}
static void reset(int page,int offset){
 s_page=page;s_main_scroll=s_ble_scroll=offset;s_scroll_drag_consumed=s_scroll_present_pending=false;ui_gesture_reset(&recognizer);
}
int main(void){
 for(int mode=0;mode<3;++mode){
  selected_mode=mode;reset(SETTINGS_MAIN,0);int before=pushes,taps=row_taps;
  assert(feed(300,800,true,true,0)==APP_REDRAW_NONE);
  assert(feed(300,693,true,false,100)==APP_REDRAW_NONE&&s_main_scroll==0);
  assert(feed(300,692,true,false,110)==APP_REDRAW_AREA&&s_main_scroll==204);
  assert(pushes==before+1&&paints==pushes&&paint_mode==mode&&pushed_mode==MODE_GL16);
  assert(pushed.y==160&&pushed.y+pushed.height==UI_NAV_REFRESH_END);
  for(int i=1;i<=30;++i)assert(feed(300,692-i*10,true,false,110+i*10)==APP_REDRAW_NONE&&s_main_scroll==204);
  assert(feed(300,392,false,false,430)==APP_REDRAW_NONE&&s_main_scroll==204&&pushes==before+1&&row_taps==taps);
  for(int i=1;i<=30;++i)assert(feed(300,392,false,false,430+i*100)==APP_REDRAW_NONE&&s_main_scroll==204&&pushes==before+1);
  assert(feed(300,700,true,true,4000)==APP_REDRAW_NONE);
  assert(feed(300,700,false,false,4010)==APP_REDRAW_NONE&&row_taps==taps+1);
  // 短拖/横向/题头/底栏均不滚动；上下阈值、最大步长和上下边界保持原行为。
  // Short/horizontal/chrome drags do not scroll; retain thresholds, maximum step and both boundaries.
  reset(SETTINGS_MAIN,100);before=pushes;
  feed(300,800,true,true,0);feed(300,770,true,false,20);feed(300,770,false,false,40);assert(s_main_scroll==100&&pushes==before);
  reset(SETTINGS_MAIN,100);feed(300,800,true,true,0);feed(450,680,true,false,20);feed(450,680,false,false,40);assert(s_main_scroll==100);
  reset(SETTINGS_MAIN,100);feed(300,150,true,true,0);feed(300,20,true,false,20);feed(300,20,false,false,40);assert(s_main_scroll==100);
  reset(SETTINGS_MAIN,100);feed(300,1100,true,true,0);feed(300,900,true,false,20);feed(300,900,false,false,40);assert(s_main_scroll==100);
  reset(SETTINGS_MAIN,500);feed(300,300,true,true,0);assert(feed(300,410,true,false,20)==APP_REDRAW_AREA&&s_main_scroll==296);feed(300,410,false,false,40);
  reset(SETTINGS_MAIN,10);feed(300,300,true,true,0);feed(300,410,true,false,20);assert(s_main_scroll==0);feed(300,410,false,false,40);
  reset(SETTINGS_MAIN,10);feed(300,800,true,true,0);feed(300,200,true,false,20);assert(s_main_scroll==460);feed(300,200,false,false,40);
  reset(SETTINGS_MAIN,800);feed(300,800,true,true,0);feed(300,680,true,false,20);assert(s_main_scroll==833);feed(300,680,false,false,40);
  // 多指/故障取消后无额外推进，正常点击恢复；连续多次滑动不产生定稿/周期全刷。
  // Multitouch/fault cancellation adds no motion, taps recover, and repeated swipes add no settling/periodic full refresh.
  reset(SETTINGS_MAIN,0);feed(300,800,true,true,0);touch.count=2;ctx.pressed=false;ui_gesture_event_t canceled;
  assert(ui_gesture_feed(&recognizer,&ctx,&canceled)&&canceled.type==UI_GESTURE_CANCEL);event(&ctx,&canceled);assert(!s_main_scroll);
  for(int i=0;i<80;++i){reset(SETTINGS_MAIN,400);before=pushes;feed(300,800,true,true,0);feed(300,680,true,false,20);feed(300,680,false,false,40);assert(pushes==before+1&&pushed_mode==MODE_GL16);}
  for(int page=SETTINGS_BLUETOOTH;page<=SETTINGS_BLE_SCAN;++page){
   reset(page,0);feed(300,800,true,true,0);assert(feed(300,680,true,false,20)==APP_REDRAW_AREA&&s_ble_scroll==204);
   assert(pushed.y==190&&pushed.y+pushed.height==1088);feed(300,680,false,false,40);
  }
  const int pages[]={SETTINGS_SHELF_STYLE,SETTINGS_SYSTEM_FONT,SETTINGS_WALLPAPER,SETTINGS_AVATAR};
  for(unsigned i=0;i<sizeof(pages)/sizeof(pages[0]);++i){
   s_page=pages[i];s_scroll_present_pending=true;assert(settings_present(&ctx,APP_REDRAW_AREA));
   assert(pushed_mode==MODE_GL16&&pushed.y==(s_page==SETTINGS_SHELF_STYLE?242:190)&&pushed.y+pushed.height==1088);
  }
 }
 s_page=SETTINGS_UPGRADE;assert(settings_present(&ctx,APP_REDRAW_AREA)&&pushed.y==468&&pushed.height==64);
 assert(!settings_present(&ctx,APP_REDRAW_NONE));
 puts("PASS: actual generic gestures + restored settings handler/presenter: all three modes use one discrete step and one changed-gray update per swipe; no tracking, momentum, motion ink, extra release/idle frames or periodic full flash; 1088 crop preserves chrome; selectors and OTA remain");
}
'''
with tempfile.TemporaryDirectory(prefix='kiikoread-settings-rollback-') as directory:
    path=Path(directory)/'settings.c';binary=path.with_suffix('');path.write_text(unit)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    '-I'+str(root/'tools/ui_gesture_stubs'),'-I'+str(root/'main/ui'),str(path),str(root/'main/ui/ui_gesture.c'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
