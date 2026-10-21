#!/usr/bin/env python3
"""中文：验证 rc90 阅读全刷与保留的 rc92 普通翻页路由。/ English: Verify rc90 reader full refresh and preserved rc92 ordinary-turn routing.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
root=Path(__file__).resolve().parents[1]
release='4bd2b9e808c214c7606cb4c31e606af146d48bce'
full_release='v0.3.3-rc90'
with tempfile.TemporaryDirectory(dir='/private/tmp') as folder:
    original=Path(folder)/'book.c'
    original.write_bytes(subprocess.check_output(['git','show',release+':main/apps/app_book.c'],cwd=root))
    for name in ('present','turn_page','reader_area','reader_fullscreen_progress_area','reader_clock_tick','reader_manual_refresh'):
        current=function(name,root/'main/apps/app_book.c');baseline=function(name,original)
        if name=='present':
            # 只允许撤回自动清屏的两段输出，其他普通翻页路由仍与正式基线相同。
            # Only remove the two-stage automatic cleanup; retain the release baseline's other turn paths.
            begin=baseline.index('    else if(s_reader_cleanup&&s_reader_turn_pending')
            end=baseline.index('    else if (redraw == APP_REDRAW_FULL || s_reader_cleanup)',begin)
            baseline=baseline[:begin]+baseline[end:]
            current=current.replace("        !(redraw == APP_REDRAW_AREA && (s_shelf_feedback_pending || s_cover_batch_present_pending))) {\n        prepare_covers(ctx);\n        gather_cover_entry(ctx);\n    }", "        !(redraw == APP_REDRAW_AREA && (s_shelf_feedback_pending || s_cover_batch_present_pending))) prepare_covers(ctx);")
            current=current.replace('    reader_prefetch_end(prep);',
                                    '    if (prep) xSemaphoreTake(s_prep_done, portMAX_DELAY);')
        assert current==baseline,name
    original.write_bytes(subprocess.check_output(['git','show',full_release+':main/display.c'],cwd=root))
    current=function('update_display_full_unlocked',root/'main/display.c')
    for line in ('    s_full_refresh_requested = false;\n','    display_main_transition_cancel();\n','    epd_hl_waveform(hl, &E0470_WAVEFORM);\n'):
        current=current.replace(line,'')
    assert current==function('update_display_full_unlocked',original)
for rel in ('main/display_reader_waveform.c','main/display_reader_waveform.h'):
    assert (root/rel).read_bytes()==subprocess.check_output(['git','show',release+':'+rel],cwd=root),rel
assert not subprocess.check_output(['git','diff',release,'--','components/e0470_page_turn','components/e0470_epaper_waveform'],cwd=root)
assert 'update_display_reader_gc16' not in (root/'main/display.c').read_text()
assert 'update_display_reader_cleanup' not in (root/'main/display.c').read_text()
print('PASS: full refresh matches rc90 original GC16; ordinary rc92 turn regions, clock, prefetch, reader waveform and panel/ripple components preserved',flush=True)
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {void *hl;uint8_t *fb;bool request_app;} app_ctx_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE,APP_REDRAW_FULL,APP_REDRAW_DONE} app_redraw_t;
enum EpdDrawError {EPD_DRAW_SUCCESS,EPD_DRAW_OTHER_ERROR};
enum {READING,SHELF,MANAGE,EDIT,SEARCH,READER_PANEL_NONE,READER_PANEL_BACKGROUND_PREVIEW};
enum {MODE_DU,MODE_GL16,MODE_GC16,APP_PAGE_REFRESH_MODE=MODE_GL16};
#define TAG "test"
#define ESP_LOGI(...) ((void)0)
#define portMAX_DELAY 0
#define E0470_TURN_FAST_TICK_US 11090
static int E0470_WAVEFORM,E0470_FULL_WAVEFORM,E0470_FOLLOW_WAVEFORM;
static bool s_reader_clock_pending,s_input_settle,s_shelf_feedback_pending,s_cover_batch_present_pending,s_shelf_page_pending;
static bool s_reader_image_refresh_pending,s_reader_cleanup,s_reader_turn_pending,s_water_turn_pending,s_clear_confirm,s_reader_fullscreen,s_reader_footer_pending,s_reader_text_frame;
static bool s_toolbar;
static int s_view=READING,s_reader_panel,s_mode=MODE_GL16,s_presented_view=READING,s_water_turn_dir,s_page,s_du_count;
static unsigned s_cover_dirty_mask,s_cover_present_mask;
static int64_t s_reader_clock_minute,s_reader_clock_painted_minute,s_du_ms;
static const char *s_text="text";
static EpdRect s_area={0,0,684,1216},s_du_area;
static void reader_prefetch_end(bool started){assert(!started);}
static int calls,selected,fail,rendered,prepares,images,tick;
static bool background;
static enum EpdDrawError push(int tag){++calls;selected=tag;return fail?EPD_DRAW_OTHER_ERROR:EPD_DRAW_SUCCESS;}
static bool reader_status_visible(void){return !s_reader_fullscreen;}
static int64_t ui_nav_clock_minute(void){return 123;}
static int64_t esp_timer_get_time(void){return 1000000;}
static bool kick_prep(void){return false;}

static const char *app_settings_book_background_path(void){return background?"wallpaper":"";}
static void render(app_ctx_t *ctx,uint8_t *fb){(void)ctx;(void)fb;++rendered;}
static void prepare_covers(app_ctx_t *ctx){(void)ctx;assert(s_view!=READING);}
static void gather_cover_entry(app_ctx_t *ctx){(void)ctx;assert(s_view!=READING);}
static void prepare_inline_image(void){++prepares;}
static bool ui_reader_background_prepare(const char *p){(void)p;return true;}
static unsigned book_layout_page_image_count(int page){(void)page;return images;}
static bool shelf_cover_frame_ready(void){return true;}
static EpdRect ui_rect_union(EpdRect a,EpdRect b){(void)b;return a;}
static int app_settings_reader_turn_effect(void){return tick;}
static void guard_draw_result(void *hl,enum EpdDrawError err){(void)hl;(void)err;}
static int e0470_page_turn_tick_us(void){return 11090;}
static void e0470_page_turn_set_tick_us(int t){assert(t==11090);}
#define display_main_transition_shelf_page() ((void)0)
#define update_display_full(...) push(12)
#define update_display_area_diff_with(...) push(13)
#define update_display_area_full_with(...) push(14)
#define update_display_area_with(...) push(15)
#define update_display_with(...) push(16)
#define update_display_mode_diff(...) push(17)
#define update_display_water_turn(...) push(18)
#define update_display_fast_page(...) push(19)
'''
unit+=function('present',root/'main/apps/app_book.c')
unit+=r'''
static void reset(void){
 calls=selected=rendered=prepares=fail=images=0;background=false;s_view=s_presented_view=READING;
 s_reader_panel=READER_PANEL_NONE;s_mode=MODE_GL16;s_reader_fullscreen=true;
 s_reader_footer_pending=true;s_reader_cleanup=false;s_water_turn_pending=false;s_reader_turn_pending=true;
 s_toolbar=s_clear_confirm=s_reader_image_refresh_pending=s_reader_clock_pending=false;s_reader_text_frame=false;
}
int main(void){
 app_ctx_t ctx={0};
 for(int full=0;full<2;++full){
  reset();s_reader_fullscreen=full;s_reader_footer_pending=!full;
  assert(present(&ctx,APP_REDRAW_AREA));
  assert(calls==2&&selected==(full?13:15)&&s_reader_text_frame);
  assert(!s_reader_footer_pending&&!s_reader_cleanup);
 }
 for(int full=0;full<2;++full){
  reset();s_reader_fullscreen=full;s_reader_cleanup=true;
  assert(present(&ctx,APP_REDRAW_AREA));assert(calls==1&&selected==12&&s_reader_text_frame);
  assert(!s_reader_cleanup&&!s_reader_turn_pending);
 }
 reset();s_reader_turn_pending=false;present(&ctx,APP_REDRAW_FULL);assert(calls==1&&selected==12&&rendered==1);
 reset();s_reader_turn_pending=false;present(&ctx,APP_REDRAW_PAGE);assert(calls==1&&selected==17&&rendered==1);
 reset();fail=1;s_reader_footer_pending=false;
 present(&ctx,APP_REDRAW_AREA);assert(calls==1&&selected==13&&!s_reader_text_frame);
 reset();s_water_turn_pending=true;s_reader_footer_pending=false;
 present(&ctx,APP_REDRAW_AREA);assert(selected==13&&calls==2&&!s_water_turn_pending);
 reset();images=1;s_reader_image_refresh_pending=true;s_reader_footer_pending=false;
 present(&ctx,APP_REDRAW_AREA);assert(selected==13&&calls==2&&!s_reader_text_frame);
 reset();images=1;s_reader_image_refresh_pending=true;s_reader_footer_pending=false;tick=1;
 present(&ctx,APP_REDRAW_AREA);assert(selected==13&&calls==2&&!s_reader_text_frame);tick=0;
 reset();s_reader_clock_pending=true;s_reader_fullscreen=false;s_reader_turn_pending=false;
 s_reader_clock_painted_minute=321;
 present(&ctx,APP_REDRAW_AREA);assert(calls==1&&selected==13&&s_reader_clock_minute==321&&!s_reader_clock_pending);
 reset();ctx.request_app=true;present(&ctx,APP_REDRAW_AREA);assert(calls==0);ctx.request_app=false;
 puts("PASS: manual/periodic original GC16 each use one full output and no footer replay; ordinary turns, ripple, images, clock and cancellation unchanged");
}
'''
with tempfile.TemporaryDirectory(prefix='kiikoread-reader-release-',dir='/private/tmp') as folder:
 c=Path(folder)/'test.c';binary=Path(folder)/'test';c.write_text(unit)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-variable','-g','-fsanitize=address,undefined',str(c),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
