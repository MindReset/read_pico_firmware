"""中文：验证更新 UI 收尾、追加进度、先反馈后下载和局部刷新边界。
English: Test update cleanup, additive progress, presentation-before-download and refresh bounds.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
settings = root / "main/apps/app_device_settings.c"

def function(name, source=settings):
    text = source.read_text()
    match = re.search(r"^static [^\n]+\b" + name + r"\([^;{}]*?\)\s*\{", text, re.M)
    assert match, name
    start, at, depth, quote, escape = match.start(), match.end(), 1, None, False
    while depth:
        c = text[at]
        if quote:
            if escape: escape = False
            elif c == "\\": escape = True
            elif c == quote: quote = None
        elif text.startswith("//", at):
            at = text.find("\n", at)
            assert at != -1
            continue
        elif text.startswith("/*", at):
            at = text.index("*/", at + 2) + 2
            continue
        elif c in "\"'": quote = c
        elif c == "{": depth += 1
        elif c == "}": depth -= 1
        at += 1
    return text[start:at]

unit = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ota_online.h"
#define ESP_ERR_NO_MEM 0x101
typedef enum {APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE,APP_REDRAW_FULL} app_redraw_t;
typedef struct {int x,y,width,height;} EpdRect;
static pico_update_status_t s_update;
static uint32_t s_update_drawn_percent,s_update_drawn_at;
static pico_update_state_t s_update_drawn_state;
static bool s_update_drawn_busy,s_upgrade_offer_pending,s_upgrade_confirm,s_upgrade_online;
typedef enum { UPGRADE_JOB_NONE, UPGRADE_JOB_CHECK, UPGRADE_JOB_DOWNLOAD } upgrade_job_t;
static upgrade_job_t s_upgrade_job;
static int s_upgrade_bar_width;
static char s_upgrade_notice[96];
static uint8_t pixels[1216][684];
static bool append_only;
static unsigned erased_completed,paint_calls;
#define UI_GRAY_BLACK 0
#define UI_GRAY_WHITE 255
#define UI_GRAY_LIGHT 192
#define UI_LOCK_WIDTH 684
#define EPD_DRAW_ALIGN_LEFT 0
#define EPD_DRAW_ALIGN_RIGHT 1
#define EPD_DRAW_ALIGN_CENTER 2
static int ui_text_last_percent;
/* r93 的 draw_upgrade_percent 由 epd_fill_rect 改调 ui_fill_rect_fast；替身跟随新产品入口，
   像素记录与 paint_calls 计数语义不变。/ r93 draw_upgrade_percent switched from
   epd_fill_rect to ui_fill_rect_fast; the double follows the new entry with identical
   pixel recording and paint_calls semantics. */
static void ui_fill_rect_fast(EpdRect r,uint8_t color,uint8_t *fb){
 (void)fb;assert(r.x>=0&&r.y>=0&&r.x+r.width<=684&&r.y+r.height<=1216);++paint_calls;
 for(int y=r.y;y<r.y+r.height;++y)for(int x=r.x;x<r.x+r.width;++x){
  if(append_only&&color==UI_GRAY_WHITE&&y>=519&&y<527&&x>=61&&x<61+s_upgrade_bar_width)++erased_completed;
  pixels[y][x]=color;
 }
}
static void ui_image_bw_rect(uint8_t *fb,EpdRect r){(void)fb;assert(r.x==496&&r.y==472&&r.width==132&&r.height==42);}
static void assert_progress(int width){
 for(int y=519;y<527;++y)for(int x=61;x<623;++x)
  assert(pixels[y][x]==(x<61+width?UI_GRAY_BLACK:UI_GRAY_WHITE));
 assert(s_upgrade_bar_width==width&&!erased_completed);
}
static char s_book_title[128]="海边的信",s_title[128]="chapter";
static bool s_reader_favorite;
static int back_center,title_center,favorite_center;
static void ui_nav_back(uint8_t *fb,int x,int y){(void)fb;(void)x;back_center=y+35;}
static void ui_text_title_vc(uint8_t *fb,int x,int y,int px,const char *text,const char *sample,int align){(void)fb;assert(x==342&&px==24&&align==EPD_DRAW_ALIGN_CENTER);assert(!strcmp(text,sample));title_center=y;}
static void draw_favorite_icon(uint8_t *fb,int x,int y,int w,int h,bool favorite,int gray){(void)fb;(void)x;(void)w;(void)favorite;(void)gray;favorite_center=y+h/2;}
// 横屏阅读替身：宿主测试固定竖屏 684 宽，收藏键 x 与线上竖屏值一致。
// Landscape-reading shims: host tests pin portrait 684 width, with the favorite x
// matching the shipped portrait value.
static int ui_w(void){return 684;}
static int reader_favorite_x(void){return 605;}
static void ui_hairline(uint8_t *fb,int y,int x,int width,int gray){(void)fb;(void)y;(void)x;(void)width;(void)gray;}
static void copy_text(char *out,size_t cap,const char *text){snprintf(out,cap,"%s",text);}
static int ui_text_title_fit(char *text,int px,int width,const char *sample){assert(px==32&&width==450&&!strcmp(text,sample));return 24;}
static char note_lines[6][384];
static int note_count;
static int ui_text_fixed_width_px(int px,const char *text){
 int width=0;for(const unsigned char *p=(const unsigned char *)text;*p;++p)
  if((*p&0xc0)!=0x80)width+=*p<128?px/2:px;
 return width;
}
static void ui_text_fixed(uint8_t *fb,int x,int y,int px,const char *text,int align,bool inverted){
 (void)fb;(void)align;(void)inverted;
 if(y==477){assert(x==624&&px==26&&align==EPD_DRAW_ALIGN_RIGHT);ui_text_last_percent=atoi(text);return;}
 assert(note_count<6&&x==60&&y==530+note_count*38&&px==26);
 assert(ui_text_fixed_width_px(px,text)<=540);
 snprintf(note_lines[note_count++],384,"%s",text);
}
static bool hit(EpdRect r,int x,int y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
static unsigned cache_releases,starts;
static bool feedback_presented;
static esp_err_t start_error;
static pico_update_status_t backend_status;
static void app_home_cover_mode_changed(void){assert(!cache_releases);++cache_releases;}
static void app_book_cover_mode_changed(void){assert(cache_releases==1);++cache_releases;}
esp_err_t pico_online_check(void){assert(feedback_presented&&cache_releases==2);++starts;return start_error;}
esp_err_t pico_online_download(void){assert(feedback_presented&&cache_releases==2);++starts;return start_error;}
void pico_online_get_status(pico_update_status_t *out){*out=backend_status;}
#include "ui/ui_nav_layout.h"
typedef struct {uint8_t *fb;void *hl;} app_ctx_t;
static enum {SETTINGS_MAIN,SETTINGS_TEXT_EDIT,SETTINGS_BLUETOOTH,SETTINGS_BLE_SCAN,SETTINGS_SHELF_STYLE,
 SETTINGS_SYSTEM_FONT,SETTINGS_WALLPAPER,SETTINGS_AVATAR,SETTINGS_UPGRADE,SETTINGS_PIN_ENTRY} s_page;
static bool s_input_settle,s_input_layout,s_scroll_present_pending;
static EpdRect s_input_area;
typedef struct {int kind;} EpdWaveform;
static const EpdWaveform E0470_WAVEFORM={0},E0470_FOLLOW_WAVEFORM={1};
enum EpdDrawMode {MODE_DU,MODE_GL16};
enum EpdDrawError {EPD_DRAW_SUCCESS};
static int diff_pushes,full_pushes,render_calls;
static enum EpdDrawMode pushed_mode;
static EpdRect pushed_area;
static const EpdWaveform *pushed_waveform;
static void render(app_ctx_t *ctx,uint8_t *fb){(void)ctx;(void)fb;++render_calls;feedback_presented=true;}
static void display_main_transition_cancel(void){}
static void guard_draw_result(void *hl,enum EpdDrawError error){(void)hl;assert(error==EPD_DRAW_SUCCESS);}
static enum EpdDrawError update_display_area_diff_with(void *hl,const EpdWaveform *waveform,enum EpdDrawMode mode,EpdRect area){
 (void)hl;++diff_pushes;pushed_mode=mode;pushed_area=area;pushed_waveform=waveform;return EPD_DRAW_SUCCESS;
}
static enum EpdDrawError update_display_area_full_with(void *hl,const EpdWaveform *waveform,enum EpdDrawMode mode,EpdRect area){
 ++full_pushes;return update_display_area_diff_with(hl,waveform,mode,area);
}
'''
unit += "static EpdRect ui_pinpad_full(void){return (EpdRect){0,0,684,1216};}\n"
for name in ("upgrade_percent", "upgrade_remember", "upgrade_status_redraw", "upgrade_progress_area", "draw_upgrade_percent", "draw_upgrade_progress",
             "draw_upgrade_progress_delta", "upgrade_schedule", "upgrade_start_pending", "settings_present",
             "upgrade_note_line", "draw_upgrade_notes", "upgrade_confirm_button"):
    unit += function(name) + "\n"
unit += function("draw_reader_header", root / "main/apps/app_book.c") + "\n"
unit += r'''
int main(void){
 s_update=(pico_update_status_t){.state=PICO_UPDATE_CHECKING,.busy=true};upgrade_remember(0);s_upgrade_offer_pending=true;
 s_update.state=PICO_UPDATE_AVAILABLE;assert(upgrade_status_redraw(100)==APP_REDRAW_NONE&&!s_upgrade_confirm);
 s_update.busy=false;assert(upgrade_status_redraw(200)==APP_REDRAW_PAGE&&s_upgrade_confirm&&s_upgrade_online&&!s_upgrade_offer_pending);
 s_upgrade_confirm=false;assert(upgrade_status_redraw(6000)==APP_REDRAW_NONE&&!s_upgrade_confirm);
 s_update=(pico_update_status_t){.state=PICO_UPDATE_CHECKING,.busy=true};upgrade_remember(0);s_upgrade_offer_pending=true;
 s_update.state=PICO_UPDATE_FAILED;assert(upgrade_status_redraw(100)==APP_REDRAW_NONE);
 s_update.busy=false;assert(upgrade_status_redraw(200)==APP_REDRAW_PAGE&&!s_upgrade_offer_pending);
 s_update=(pico_update_status_t){.state=PICO_UPDATE_DOWNLOADING,.busy=true,.release={.size=1000}};upgrade_remember(100);
 memset(pixels,73,sizeof(pixels));draw_upgrade_progress(NULL);assert_progress(0);
 s_update.received=40;assert(upgrade_status_redraw(10000)==APP_REDRAW_NONE);
 s_update.received=50;assert(upgrade_status_redraw(4999)==APP_REDRAW_NONE);assert(upgrade_status_redraw(5100)==APP_REDRAW_AREA);
 append_only=true;draw_upgrade_progress_delta(NULL);assert(ui_text_last_percent==5);assert_progress(28);
 for(uint32_t now=5101;now<10100;++now){s_update.received=500;assert(upgrade_status_redraw(now)==APP_REDRAW_NONE);}
 assert(upgrade_status_redraw(10100)==APP_REDRAW_AREA);draw_upgrade_progress_delta(NULL);assert(ui_text_last_percent==50);assert_progress(281);
 s_update.received=1000;assert(upgrade_status_redraw(15100)==APP_REDRAW_AREA);draw_upgrade_progress_delta(NULL);assert(ui_text_last_percent==100);assert_progress(562);
 unsigned painted=paint_calls;s_update.received=800;draw_upgrade_progress_delta(NULL);assert(paint_calls==painted);assert_progress(562);
 for(int y=0;y<1216;++y)for(int x=0;x<684;++x)if(x<52||x>=632||y<468||y>=532)assert(pixels[y][x]==73);
 append_only=false;
 s_update.state=PICO_UPDATE_READY;assert(upgrade_status_redraw(15101)==APP_REDRAW_NONE);s_update.busy=false;assert(upgrade_status_redraw(15102)==APP_REDRAW_PAGE);
 s_update.state=PICO_UPDATE_DOWNLOADING;s_update.busy=true;s_update.received=0;upgrade_remember(UINT32_MAX-2000);s_update.received=60;assert(upgrade_status_redraw(2999)==APP_REDRAW_AREA);
 app_ctx_t ctx={0};s_page=SETTINGS_UPGRADE;
 assert(!settings_present(&ctx,APP_REDRAW_NONE));
 assert(settings_present(&ctx,APP_REDRAW_PAGE)&&diff_pushes==1&&full_pushes==0&&render_calls==1);
 assert(pushed_mode==MODE_GL16&&pushed_waveform==&E0470_WAVEFORM&&pushed_area.y==0&&pushed_area.height==UI_NAV_REFRESH_END);
 assert(settings_present(&ctx,APP_REDRAW_AREA)&&diff_pushes==2&&full_pushes==0&&render_calls==1);
 assert(pushed_mode==MODE_DU&&pushed_waveform==&E0470_FOLLOW_WAVEFORM&&pushed_area.y==468&&pushed_area.height==64);
 assert(!settings_present(&ctx,APP_REDRAW_FULL));
 for(int job=0;job<2;++job){
  feedback_presented=false;cache_releases=0;unsigned started=starts;
  s_update=(pico_update_status_t){.state=PICO_UPDATE_AVAILABLE};upgrade_schedule(job!=0,42);
  assert(starts==started&&!cache_releases&&s_upgrade_job==(job?UPGRADE_JOB_DOWNLOAD:UPGRADE_JOB_CHECK)&&s_update.busy);
  assert(settings_present(&ctx,APP_REDRAW_PAGE));backend_status=s_update;
  assert(upgrade_start_pending()==APP_REDRAW_NONE&&starts==started+1&&s_upgrade_job==UPGRADE_JOB_NONE&&cache_releases==2);
 }
 cache_releases=0;start_error=ESP_ERR_NO_MEM;backend_status.state=PICO_UPDATE_FAILED;
 snprintf(backend_status.message,sizeof(backend_status.message),"内存不足，请退出传书后重试");
 upgrade_schedule(true,50);assert(upgrade_start_pending()==APP_REDRAW_PAGE&&!s_update.busy&&s_update.state==PICO_UPDATE_FAILED);
 assert(!strcmp(s_upgrade_notice,"内存不足，请退出传书后重试"));
 cache_releases=0;backend_status.state=PICO_UPDATE_AVAILABLE;snprintf(backend_status.message,sizeof(backend_status.message),"发现新版本，可下载安装");
 upgrade_schedule(true,60);assert(upgrade_start_pending()==APP_REDRAW_PAGE);
 assert(!strcmp(s_upgrade_notice,"无法开始升级，请退出传书后重试"));
 draw_reader_header(NULL);assert(back_center==title_center&&favorite_center==title_center);
 s_reader_favorite=true;draw_reader_header(NULL);assert(back_center==title_center&&favorite_center==title_center);
 snprintf(s_update.release.notes,sizeof(s_update.release.notes),"1. 优化图文混排\n2. 支持滑动翻页\n3. 新增自动休眠\n4. 优化蓝牙翻页器\n5. 优化图标与界面\n6. 优化文件管理排版");
 draw_upgrade_notes(NULL);assert(note_count==6&&!strcmp(note_lines[0],"1. 优化图文混排")&&!strcmp(note_lines[5],"6. 优化文件管理排版"));
 note_count=0;s_update.release.notes[0]=0;draw_upgrade_notes(NULL);assert(note_count==1&&!strcmp(note_lines[0],"本次更新暂无说明"));
 const char *cursor="\n\r;；  一；二;三\n四";char line[384];
 for(int i=0;i<4;++i){assert(upgrade_note_line(&cursor,line,sizeof(line)));assert(strlen(line)==3);}
 assert(!upgrade_note_line(&cursor,line,sizeof(line)));
 cursor="中文😀AB";char short_line[5];
 assert(upgrade_note_line(&cursor,short_line,sizeof(short_line))&&!strcmp(short_line,"中"));
 assert(upgrade_note_line(&cursor,short_line,sizeof(short_line))&&!strcmp(short_line,"文"));
 assert(upgrade_note_line(&cursor,short_line,sizeof(short_line))&&!strcmp(short_line,"😀"));
 assert(upgrade_note_line(&cursor,short_line,sizeof(short_line))&&!strcmp(short_line,"AB"));
 cursor="中";assert(!upgrade_note_line(&cursor,line,1));
 note_count=0;memset(s_update.release.notes,'A',sizeof(s_update.release.notes)-1);s_update.release.notes[sizeof(s_update.release.notes)-1]=0;
 draw_upgrade_notes(NULL);assert(note_count==6&&strstr(note_lines[5],"…"));
 EpdRect online_yes=upgrade_confirm_button(true,true),online_no=upgrade_confirm_button(true,false);
 assert(hit(online_yes,500,870)&&hit(online_no,150,870)&&!hit(online_yes,500,640));
 assert(!hit(online_yes,624,870)&&!hit(online_yes,500,906)&&!hit(online_no,364,870));
 assert(hit(upgrade_confirm_button(false,true),500,640)&&!hit(upgrade_confirm_button(false,true),500,870));
 puts("PASS: additive progress never erases completed pixels; no drawing outside the widget; local DU only; render before worker; failures remain actionable");
 puts("PASS: worker cleanup and busy transition, direct one-shot update offer, five-second/5-percent area-only progress, timer wrap and aligned reader header");
 puts("PASS: six release notes, measured UTF-8 wrapping and overflow, empty notes fallback, online and TF confirmation hit regions");
}
'''
# 联网 OTA 停用后：那一带不再有按钮，误触也不能排任务。
# With online OTA disabled: no button in that band and a stray tap must not schedule a job.
source_text = settings.read_text()
band = re.search(r"y >= 550 && y < 612\s*\)\s*\{(.{0,400})", source_text, re.S)
assert band, "the upgrade page lost its online-OTA button band"
assert "PICO_ONLINE_UPDATES_DISABLED" in band.group(1), \
    "tapping the online-OTA band must stay gated on PICO_ONLINE_UPDATES_DISABLED"


# 升级页的开关分支：按钮只许在「启用」那一支里画，TF 卡卡片必须在开关之外。
# The switch branch: the button belongs to the enabled arm only; the TF card must stay outside it.
def close_brace(open_at):
    depth, at = 0, open_at
    while at < len(source_text):
        if source_text[at] == "{":
            depth += 1
        elif source_text[at] == "}":
            depth -= 1
            if not depth:
                return at
        at += 1
    return -1


switch = source_text.find("if (PICO_ONLINE_UPDATES_DISABLED) {")
assert switch != -1, "the upgrade page lost its PICO_ONLINE_UPDATES_DISABLED switch"
disabled_end = close_brace(source_text.index("{", switch))
assert disabled_end != -1 and source_text[disabled_end:].lstrip().startswith("} else {"), \
    "the disabled online-OTA card needs an else arm holding the original card"
enabled_end = close_brace(source_text.index("{", source_text.index("} else {", disabled_end)))
assert "检查更新" in source_text[disabled_end:enabled_end], \
    "the 检查更新 button must live inside the enabled arm only"
tf_card = source_text.find("settings_card(fb, (EpdRect){36, 662, 612, 262}")
assert tf_card != -1 and tf_card > enabled_end, \
    "the TF-card card must be drawn outside the switch; it is the only update path left when OTA is off"
# 绘制回调只绘图，后台工作不能在 render 中触发。/ Render is paint-only and cannot start background work.
render_body=function("render")
assert not any(name in render_body for name in ("pico_online_check(", "pico_online_download(", "upgrade_start_pending("))
with tempfile.TemporaryDirectory() as folder:
    source, binary = Path(folder)/"test.c", Path(folder)/"test"
    source.write_text(unit)
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-I"+str(root/"tools/ota_stubs"), "-I"+str(root/"main"), str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
