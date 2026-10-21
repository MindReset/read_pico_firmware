"""Actual settings geometry/navigation and opt-in reading-font synchronization.

SPDX-License-Identifier: Apache-2.0
中文：所有系统字号检查布局/触摸边界，确认弹窗不穿透、不提前修改阅读字体。
English: Check every scale, layout/touch bounds, and modal isolation without eagerly changing the reading face.
"""
from pathlib import Path
import re, subprocess, tempfile

root=Path(__file__).resolve().parents[1]
s=(root/'main/apps/app_device_settings.c').read_text()
tokens=r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
scan=re.sub(tokens,lambda m:' '*len(m[0]) if m[0].startswith(('//','/*')) else m[0],s)
def block(start):
    at=scan.index('{',start)+1;depth=1;quote=None;escape=False
    while depth:
        c=scan[at]
        if quote:
            if escape:escape=False
            elif c=='\\':escape=True
            elif c==quote:quote=None
        elif c in '\"\'':quote=c
        elif c=='{':depth+=1
        elif c=='}':depth-=1
        at+=1
    return s[start:at]
def fn(name):
    m=re.search(r'^static [^\n]+\b'+name+r'\([^;{}]*\)\s*\{',s,re.M)
    assert m,name
    return block(m.start())
start=s.index('static app_redraw_t on_gesture(')
modal=block(s.index('    if (s_page==SETTINGS_SYSTEM_FONT && s_font_sync_confirm)',start))
choose=block(s.index('    if (s_page == SETTINGS_SYSTEM_FONT) {',start))
main=s[s.index('    if (y<160 || y>=UI_NAV_TOP || ev->x0<36',start):s.index('\nstatic app_redraw_t on_key(',start)].rstrip()
assert main.endswith('}')
main=main[:-1]
enum=s[s.index('typedef enum { SETTINGS_MAIN'):s.index('} settings_page_t;')+len('} settings_page_t;')]
macros='\n'.join(re.findall(r'^#define SETTINGS_(?:ROW_H|WIRELESS_ROWS|DISPLAY_ROWS|PERSONAL_ROWS|WIRELESS_Y|DISPLAY_Y|PERSONAL_Y|MAINTENANCE_Y)[^\n]*',s,re.M))
layout=s[s.index('typedef struct {\n    EpdRect size, preview;'):s.index('static system_font_layout_t system_font_layout(')]
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct{int x,y,width,height;}EpdRect;
typedef enum{APP_REDRAW_NONE,APP_REDRAW_PAGE}app_redraw_t;
typedef struct{int type,x0,y0;}ui_gesture_event_t;
enum{UI_GESTURE_TAP,UI_GESTURE_SWIPE_U};
typedef struct{int unused;}app_desc_t;
typedef struct{const app_desc_t*request_app;}app_ctx_t;
const app_desc_t app_transfer={0};
typedef struct{int unused;}ble_pt_raw_t;
typedef struct{const char *path;}ttf_font_item_t;
static const ttf_font_item_t fonts[]={{"/sdcard/fonts/A.ttf"},{"/sdcard/fonts/B.ttf"},{"/sdcard/fonts/C.ttf"}};
static int ttf_font_count(void){return 3;}
static const ttf_font_item_t*ttf_font_item(int i){return i>=0&&i<3?&fonts[i]:NULL;}
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
static uint8_t scale=100;
static bool write_fail;
static uint8_t app_settings_system_font_size(void){return scale;}
static void app_settings_set_system_font_size(uint8_t n){if(!write_fail)scale=n;}
static char system_path[192]="/sdcard/fonts/Old.ttf",reader_path[192]="/sdcard/fonts/Reader.ttf";
static unsigned reader_writes,system_activations;
static const char*app_settings_system_font_path(void){return system_path;}
static const char*app_settings_font_path(void){return reader_path;}
static void app_settings_set_system_font_path(const char*p){if(!write_fail)snprintf(system_path,sizeof(system_path),"%s",p);}
static void app_settings_set_font_path(const char*p){++reader_writes;if(!write_fail)snprintf(reader_path,sizeof(reader_path),"%s",p);}
static void app_font_activate_system(void){++system_activations;}
static void ui_text_set_system_scale(bool b){assert(b);}
static int s_font_page,s_main_scroll,s_style_scroll,s_ble_scroll,s_ble_learning;
static bool s_font_sync_confirm,s_config_confirm,s_boot_pending;
static char s_font_sync_path[192],s_notice[96],s_ble_notice[96],s_ble_feedback[96];
static int font_scans,profile_edits,wifi_requests;
static bool home_full;
static void ttf_font_scan(void){++font_scans;}
static void app_transfer_request_wifi_setup(void){++wifi_requests;}
static bool ble_pt_pop_raw(ble_pt_raw_t*p){(void)p;return false;}
static bool app_settings_home_full_refresh(void){return home_full;}
static void app_settings_set_home_full_refresh(bool v){home_full=v;}
static void profile_editor_open(bool signature){assert(signature);++profile_edits;}
#define UI_NAV_TOP 1096
'''+enum+'\nstatic settings_page_t s_page;\n'+macros+'''
#define SETTINGS_SCROLL_MAX (SETTINGS_MAINTENANCE_Y+3*SETTINGS_ROW_H-UI_NAV_TOP+12)
static void upgrade_open(void){s_page=SETTINGS_UPGRADE;}
// 归因：我方无线组第 3 行是飞行模式，派发里多一个 flight_mode_toggle() 调用（真机侧
// 射频/传书/蓝牙的联动由专门测试覆盖，这里只记录次数）。
// Attribution: our wireless group's third row is flight mode, adding one flight_mode_toggle()
// call (its radio/transfer/BLE side effects are covered elsewhere; just count the calls).
static int flight_toggles;
static void flight_mode_toggle(void){++flight_toggles;}
'''+fn('settings_stacked')+'\n'+fn('settings_profile_height')+'\n'+fn('settings_main_row_height')+'\n'+layout+fn('system_font_layout')+'\n'+fn('system_font_item_rect')+'\n'+fn('system_font_sync_button')+'\n'
unit+='static app_redraw_t font_tap(int x,int y){ui_gesture_event_t e={UI_GESTURE_TAP,x,y};const ui_gesture_event_t*ev=&e;'+modal+choose+'return APP_REDRAW_NONE;}\n'
unit+='static app_redraw_t modal_swipe(void){ui_gesture_event_t e={UI_GESTURE_SWIPE_U,400,700};const ui_gesture_event_t*ev=&e;'+modal+'return APP_REDRAW_NONE;}\n'
unit+='static app_redraw_t main_tap(app_ctx_t*ctx,int x,int y){ui_gesture_event_t e={UI_GESTURE_TAP,x,y};const ui_gesture_event_t*ev=&e;'+main+'}\n'
unit+=r'''
int main(void){
 app_ctx_t ctx={0};
 for(int percent=100;percent<=200;percent+=10){
   scale=percent;s_font_sync_confirm=false;
   system_font_layout_t l=system_font_layout();
   assert(l.size.y+l.size.height<l.preview_title&&l.preview.y+l.preview.height<l.font_title);
   assert(l.font_title<l.first&&l.first+l.slots*l.row_height<UI_NAV_TOP);
   for(int i=0;i<l.slots;++i){EpdRect r=system_font_item_rect(i);assert(r.y+r.height<UI_NAV_TOP);if(i)assert(system_font_item_rect(i-1).y+system_font_item_rect(i-1).height<r.y);}
   assert(SETTINGS_WIRELESS_Y>=180+settings_profile_height()+70);
   assert(SETTINGS_DISPLAY_Y>SETTINGS_WIRELESS_Y+3*SETTINGS_ROW_H);
   assert(SETTINGS_PERSONAL_Y>SETTINGS_DISPLAY_Y+6*SETTINGS_ROW_H);
   const int tops[]={SETTINGS_WIRELESS_Y,SETTINGS_DISPLAY_Y,SETTINGS_PERSONAL_Y,SETTINGS_MAINTENANCE_Y};
   const int counts[]={4,6,4,3};
   // 归因：无线组从我方 3 行变 4 行——第 3 行飞行模式（-4），「日期与时间」顺延到第 4 行。
   // Attribution: the wireless group grows from 3 to 4 rows on our side — row 3 is flight
   // mode (-4) and 日期与时间 shifts down to row 4.
   const int destinations[][6]={{-1,SETTINGS_BLUETOOTH,-4,SETTINGS_TIME},
     {SETTINGS_SYSTEM_FONT,SETTINGS_SYSTEM_CONTRAST,SETTINGS_MAIN_REFRESH,-2,SETTINGS_AUTO_LOCK,SETTINGS_POWER_SLEEP},
     {SETTINGS_SHELF_STYLE,SETTINGS_LOCK_STYLE,SETTINGS_LOCK_PIN,-3},
     {SETTINGS_UPGRADE,SETTINGS_CONFIG,SETTINGS_BOOT}};
   for(int group=0;group<4;++group)for(int i=0;i<counts[group];++i){
     s_page=SETTINGS_MAIN;int center=tops[group]+i*SETTINGS_ROW_H+SETTINGS_ROW_H/2;s_main_scroll=center-500;ctx.request_app=NULL;
     int flights=flight_toggles;
     main_tap(&ctx,300,500);
     if(destinations[group][i]>=0)assert((int)s_page==destinations[group][i]);
     else if(destinations[group][i]==-1)assert(ctx.request_app==&app_transfer);
     else if(destinations[group][i]==-2)assert(s_page==SETTINGS_MAIN);
     else if(destinations[group][i]==-4)assert(flight_toggles==flights+1);
     else assert(profile_edits>0);
     s_page=SETTINGS_MAIN;assert(main_tap(&ctx,10,500)==APP_REDRAW_NONE&&s_page==SETTINGS_MAIN);
   }
   s_main_scroll=SETTINGS_SCROLL_MAX;s_page=SETTINGS_MAIN;
   int last=SETTINGS_MAINTENANCE_Y+3*SETTINGS_ROW_H-1-s_main_scroll;
   assert(last<UI_NAV_TOP&&main_tap(&ctx,300,last)==APP_REDRAW_PAGE&&s_page==SETTINGS_BOOT);
 }
 scale=100;s_page=SETTINGS_SYSTEM_FONT;s_font_page=0;s_main_scroll=0;s_font_sync_confirm=false;
 for(int i=0;i<11;++i){EpdRect r=system_font_layout().size;assert(font_tap(300,r.y+r.height/2)==APP_REDRAW_PAGE);assert(scale==((i+1)%11)*10+100);}
 EpdRect r=system_font_item_rect(1);
 assert(font_tap(300,r.y+r.height/2)==APP_REDRAW_PAGE&&s_font_sync_confirm);
 assert(!strcmp(system_path,fonts[0].path)&&!strcmp(reader_path,"/sdcard/fonts/Reader.ttf")&&!reader_writes&&system_activations==1);
 assert(modal_swipe()==APP_REDRAW_NONE&&s_font_sync_confirm);
 assert(font_tap(300,220)==APP_REDRAW_NONE&&s_font_sync_confirm&&scale==100);
 assert(font_tap(202,720)==APP_REDRAW_PAGE&&!s_font_sync_confirm&&!reader_writes);
 assert(font_tap(300,r.y+r.height/2)==APP_REDRAW_PAGE&&s_font_sync_confirm);
 write_fail=true;assert(font_tap(481,720)==APP_REDRAW_PAGE&&s_font_sync_confirm&&s_notice[0]&&reader_writes==1);
 write_fail=false;assert(font_tap(481,720)==APP_REDRAW_PAGE&&!s_font_sync_confirm&&reader_writes==2&&!strcmp(reader_path,system_path));
 assert(font_tap(300,r.y+r.height/2)==APP_REDRAW_PAGE&&!s_font_sync_confirm);
 write_fail=true;unsigned before=system_activations;r=system_font_item_rect(2);
 assert(font_tap(300,r.y+r.height/2)==APP_REDRAW_PAGE&&!s_font_sync_confirm&&system_activations==before&&!strcmp(system_path,fonts[0].path));
 write_fail=false;assert(font_tap(300,r.y+r.height+7)==APP_REDRAW_NONE);
 puts("PASS: 11 scales, all 17 destinations, reachable final row, font-size cycle, modal isolation, opt-in sync, no reading-font load and failed-save retry");
}
'''
with tempfile.TemporaryDirectory() as folder:
    c,b=Path(folder)/'test.c',Path(folder)/'test'
    c.write_text(unit)
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(c),'-o',str(b)],check=True)
    subprocess.run([str(b)],check=True)
