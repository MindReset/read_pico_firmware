"""Actual storage-card hit handling: classification, confirmation, cancellation and rescan.
中文：真实触摸分支验证格式化确认与取消。/ English: Exercise the real gesture branch.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
ROOT=Path(__file__).resolve().parents[1]
source=ROOT/'main/apps/app_files.c'
s=source.read_text()
a=s.index('    if (!long_press && sd_needs_format() && ui_rect_hit(storage_card_rect()')
b=s.index('    for (int i = 0; !long_press && i < 4;',a)
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {bool present,mounted,needs_format;} read_pico_sd_info_t;
typedef struct {int x0,y0;} ui_gesture_event_t;
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NOT_FINISHED 3
static bool avatar_ok=true;
static bool book_cover_jobs_quiesce(unsigned ms){assert(ms==2000);return true;}
static bool ui_avatar_quiesce(unsigned ms){assert(ms==2000);return avatar_ok;}
#define APP_REDRAW_NONE 0
#define APP_REDRAW_PAGE 1
static read_pico_sd_info_t s_sd;
static bool s_sd_format_confirm,s_scan_pending;
static char s_sd_notice[64];
static int formats,format_error;
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
static esp_err_t read_pico_sd_format(void){++formats;if(!format_error)s_sd=(read_pico_sd_info_t){true,true,false};return format_error;}
static void read_pico_sd_get_info(read_pico_sd_info_t *out){*out=s_sd;}
static const char *esp_err_to_name(esp_err_t err){(void)err;return "ERROR";}
'''
unit+=function('storage_card_rect',source)+'\n'+function('sd_needs_format',source)+'\n'
unit+='static int tap(int x,int y,bool long_press){ui_gesture_event_t event={x,y};const ui_gesture_event_t *ev=&event;\n'+s[a:b]+'return format_cancelled ? APP_REDRAW_PAGE : APP_REDRAW_NONE;\n}\n'
unit+=r'''
int main(void){
 s_sd=(read_pico_sd_info_t){false,false,false};assert(!tap(100,200,false)&&!formats);
 s_sd=(read_pico_sd_info_t){true,false,false};assert(!tap(100,200,false)&&!formats);
 s_sd.needs_format=true;assert(tap(100,200,false)==1&&s_sd_format_confirm&&!formats);
 assert(tap(20,300,false)==1&&!s_sd_format_confirm&&!formats);
 assert(!tap(100,200,true)&&!formats);
 assert(tap(100,200,false)==1&&s_sd_format_confirm);
 assert(tap(100,200,false)==1&&!s_sd_format_confirm&&formats==1&&s_scan_pending&&s_sd.mounted);
 s_sd=(read_pico_sd_info_t){true,false,true};format_error=-1;s_scan_pending=false;
 tap(100,200,false);assert(!s_sd_notice[0]);tap(100,200,false);
 assert(formats==2&&!s_sd_format_confirm&&!s_scan_pending&&strstr(s_sd_notice,"ERROR"));
 avatar_ok=false;format_error=0;tap(100,200,false);tap(100,200,false);assert(formats==2&&!s_sd_format_confirm&&!s_scan_pending);
 puts("PASS: absent/contact/timeout cards cannot format; confirmation/cancel/hold guards, error and success rescan");
}
'''
assert '将清空卡内全部数据，再点确认' in s
with tempfile.TemporaryDirectory(dir=ROOT/'build') as folder:
 c=Path(folder)/'test.c';exe=Path(folder)/'test';c.write_text(unit)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
