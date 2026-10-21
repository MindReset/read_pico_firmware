"""书架差分反馈与状态栏实际函数回归。/ Actual shelf feedback and status-bar regression.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]


def function(path, name):
    text = (root / path).read_text()
    match = re.search(r"^(?:static )?[^\n]+\b" + name + r"\([^;{}]*?\)\s*\{", text, re.M)
    assert match, name
    start, at, depth, quote, escape = match.start(), match.end(), 1, None, False
    while depth:
        c = text[at]
        if quote:
            if escape:
                escape = False
            elif c == "\\":
                escape = True
            elif c == quote:
                quote = None
        elif c in "\"'":
            quote = c
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
        at += 1
    return text[start:at]


shelf = r'''
// 本测试自带按旋转换算的尺寸替身，必须在任何头被拉进来之前关掉 stub 的 static inline 版，
// 否则后到的 include 会先定义它、与下面的实现重定义。
// This test ships its own rotation-aware size shim, so the static inline one has to be
// switched off before any header is pulled in, or a later include defines it first.
#define EPDIY_STUB_NO_ROTATED_SIZE
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdatomic.h>
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {void *hl;uint8_t *fb;int64_t now_ms;unsigned leaf;void *request_app;} app_ctx_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_DONE,APP_REDRAW_AREA,APP_REDRAW_PAGE,APP_REDRAW_FULL} app_redraw_t;
enum {SHELF,MANAGE,READING,READER_PANEL_NONE,TOC,EDIT,SEARCH,READER_PANEL_DICT};
enum EpdDrawMode {MODE_GL16,MODE_DU,MODE_GC16};
enum EpdDrawError {EPD_DRAW_SUCCESS,EPD_DRAW_ERROR};
#define APP_PAGE_REFRESH_MODE MODE_GL16
#define SHELF_BOOK_LIFT_PX 16
#define portMAX_DELAY 0
static const int E0470_WAVEFORM=0,E0470_FULL_WAVEFORM=1,E0470_FOLLOW_WAVEFORM=2,E0470_TEXTTURN_WAVEFORM=3,E0470_SOFT_TEXTTURN_WAVEFORM=4;
static const int* display_reader_text_waveform(void){return &E0470_SOFT_TEXTTURN_WAVEFORM;}
static const char *TAG="test";
static int s_view,s_presented_view,s_reader_panel,s_pressed_control,s_reader_slider;
static bool s_shelf_page_pending,s_shelf_feedback_pending,s_reader_cleanup,s_reader_image_refresh_pending,s_toolbar,s_clear_confirm;
static bool s_reader_fullscreen,s_water_turn_pending,s_reader_footer_pending,s_reader_turn_pending,s_reader_text_frame;
static enum EpdDrawMode s_mode;
static EpdRect s_area,s_du_area;
static unsigned s_du_count;
static int64_t s_du_ms;
static int s_water_turn_dir;
static void *s_prep_done;
static atomic_bool s_prep_stop;
static void display_main_transition_shelf_page(void){}
static unsigned pushes;
static int route;
static enum EpdDrawMode pushed_mode;
static EpdRect pushed_area;
static int trace_route[256], trace_mode[256], trace_wave[256];
static EpdRect trace_area[256];
static unsigned trace_count;
enum {DIFF=1,AREA,FULL,WHOLE,FAST,WATER,READER,LOCAL_FULL};
static int test_style;static bool test_fast;
static int app_settings_shelf_style(void){return test_style;}
static bool app_settings_main_fast_refresh(void){return test_fast;}
static int shelf_rows(void){return test_style==5?4:9;}
static bool s_reader_clock_pending,s_cover_batch_present_pending;
static int64_t s_reader_clock_minute,s_reader_clock_painted_minute;
static unsigned s_cover_dirty_mask,s_cover_present_mask;
static bool reader_status_visible(void){return false;}
static EpdRect reader_status_area(void){return (EpdRect){24,18,156,44};}
static int64_t ui_nav_clock_minute(void){return 100;}
static void reader_status_paint(uint8_t *fb){(void)fb;}
static bool shelf_cover_frame_ready(void){return true;}
#define READER_PANEL_BACKGROUND_PREVIEW 98
static const char *app_settings_book_background_path(void){return "";}
static void ui_reader_background_prepare(const char *path){(void)path;}
static unsigned test_list_draws,test_prepares;
static EpdRect paint_list_cover_feedback(app_ctx_t *ctx,int row){(void)ctx;++test_list_draws;return (EpdRect){71,228+row*197,120,164};}
static EpdRect row_rect(int row){return (EpdRect){42+(row%3)*210,220+(row/3)*272,176,240};}
static const char *s_text;
static void prepare_inline_image(void){}
static bool test_check_cached;
static unsigned test_button_draws,test_body_draws;
static void draw_shelf_header_button(uint8_t *fb,EpdRect r,bool importing){(void)fb;assert(r.width==97&&r.height==54);assert(r.x==(importing?551:442));++test_button_draws;}
static unsigned test_prepare_order,test_render_order,test_step;
static void render(app_ctx_t *ctx,uint8_t *fb){(void)ctx;(void)fb;++test_body_draws;if(test_check_cached){assert(test_prepare_order);test_render_order=++test_step;}}
static void prepare_covers(app_ctx_t *ctx){(void)ctx;++test_prepares;if(test_check_cached)test_prepare_order=++test_step;}
/* r93 的 present 在书架路径调用 gather_cover_entry；产品函数以 s_cover_pending_mask 为门，
   本 harness 从不置位它，真实实现同样立即返回，空桩与之逐位等价。/ r93 present calls
   gather_cover_entry on the shelf path; the product gates on s_cover_pending_mask, which
   this harness never sets, so the real function returns immediately too. */
static void gather_cover_entry(app_ctx_t *ctx){(void)ctx;}
static bool kick_prep(void){return false;}
/* r93 epoch 预排依赖：本 harness 永不发起预排（kick_prep 恒假、s_next_fb 恒 NULL、
   s_next_page 恒 -1），故 resume 恒假、cached 分支不可达、generation 取惰性常数 0，
   与产品在这些前置条件下的行为一致。/ r93 epoch prefetch deps: this harness never
   starts a prefetch (kick_prep false, s_next_fb NULL, s_next_page -1), so resume stays
   false, the cached branch is unreachable and the generation is a lazy 0 — matching the
   product under the same preconditions. */
static uint32_t book_layout_generation(void){return 0;}
static bool reader_prefetch_resume(size_t page){(void)page;return false;}
static void draw_reader_outer(uint8_t *fb,size_t page){(void)fb;(void)page;}
static int64_t esp_timer_get_time(void){return 1000000;}
static void test_log(const char *tag,const char *format,...){(void)tag;(void)format;}
#define ESP_LOGI test_log
static void xSemaphoreTake(void *done,int timeout){(void)done;(void)timeout;}
static void guard_draw_result(void *hl,int error){(void)hl;assert(error==EPD_DRAW_SUCCESS||error==EPD_DRAW_ERROR);}
static EpdRect reader_area(void){return (EpdRect){36,176,612,856};}
static EpdRect progress_rect(void){return (EpdRect){36,1040,612,42};}
static EpdRect reader_fullscreen_progress_area(void){return (EpdRect){0,1200,684,4};}
static unsigned percent(unsigned page){return page;}
static unsigned s_page,s_chapter;
static bool test_hide_images,test_load_failed,s_save_failed,s_input_settle;
static int test_types[2][2],test_push_error,test_effect;
static uint8_t test_full_pages;
static unsigned s_turns,s_unsaved,s_stats_pending_turns,s_session_turns;
static size_t s_jump_offset;
static int64_t s_last_turn_ms,s_stats_activity_ms;
static uint8_t *s_next_fb;
static int s_next_page=-1;
static uint32_t s_next_generation;
static struct {uint8_t *gray;} test_image={.gray=(uint8_t*)"gray"},*s_page_images=&test_image;
#define BOOK_TOC_ROWS 12
#define E0470_TURN_RTL 1
#define E0470_TURN_LTR -1
#define E0470_TURN_FAST_TICK_US 14000
static int test_tick_us=12000,test_tick_sets;
static int e0470_page_turn_tick_us(void){return test_tick_us;}
static void e0470_page_turn_set_tick_us(int us){test_tick_us=us;++test_tick_sets;}
static int book_layout_page_image_count(unsigned page){return !test_hide_images&&test_types[s_chapter][page]?1:0;}
static unsigned book_layout_page_count(void){return 2;}
static unsigned book_chapter_count(void){return 2;}
/* P0-1 视觉页地图身份桩：单栏下视觉页恒等于逻辑页，与生产退化路径一致；真实地图
   由 tools/reader_page_map_host_test.py 覆盖。/ P0-1 visual page map identity stub:
   in single column a visual page equals its logical page, matching the production
   degenerate path; the real map is covered by tools/reader_page_map_host_test.py. */
static size_t reader_visual_count(void){return book_layout_page_count();}
static size_t reader_visual_index(size_t logical){return logical;}
static size_t reader_visual_start(size_t visual){return visual;}
static bool app_settings_reader_hide_images(void){return test_hide_images;}
static uint8_t app_settings_reader_full_pages(void){return test_full_pages;}
static int app_settings_reader_turn_effect(void){return test_effect;}
static void save_progress(void){}
static void skip_hidden_image_pages(app_ctx_t *ctx,int dir){(void)ctx;(void)dir;}
static bool load_chapter(app_ctx_t *ctx,unsigned chapter,int off,bool last){(void)ctx;(void)off;if(test_load_failed)return false;s_chapter=chapter;s_page=last?1:0;return true;}
static void set_reader_view(int view){s_view=view;}
static void lock_draw(void){}
static void unlock_draw(void){}
static void invalidate_prep(void){}
static app_redraw_t toggle_reader_fullscreen(app_ctx_t *ctx){(void)ctx;s_reader_fullscreen=!s_reader_fullscreen;return APP_REDRAW_PAGE;}
static size_t fb_bytes(void){return 1;}
static void draw_reader(uint8_t *fb,unsigned page){*fb=(uint8_t)page;}

static EpdRect ui_rect_union(EpdRect a,EpdRect b){(void)b;return a;}
static enum EpdDrawError record(int kind,enum EpdDrawMode mode,EpdRect area){assert(trace_count<256);trace_route[trace_count]=kind;trace_mode[trace_count]=mode;trace_area[trace_count++]=area;++pushes;route=kind;pushed_mode=mode;pushed_area=area;return (enum EpdDrawError)test_push_error;}
static enum EpdDrawError update_display_area_diff_with(void *hl,const int *wave,enum EpdDrawMode mode,EpdRect area){(void)hl;trace_wave[trace_count]=*wave;return record(DIFF,mode,area);}
static enum EpdDrawError update_display_area_with(void *hl,const int *wave,enum EpdDrawMode mode,EpdRect area){(void)hl;trace_wave[trace_count]=*wave;return record(AREA,mode,area);}
static enum EpdDrawError update_display_area_full_with(void *hl,const int *wave,enum EpdDrawMode mode,EpdRect area){(void)hl;trace_wave[trace_count]=*wave;return record(LOCAL_FULL,mode,area);}
static enum EpdDrawError update_display_with(void *hl,const int *wave,enum EpdDrawMode mode){(void)hl;(void)wave;return record(WHOLE,mode,(EpdRect){0});}
// r94 自我回滚删除了 update_display_reader_cleanup 调用分支（rc91~93 的物理擦白+GL16 补刷），
// 周期性清理与手动全刷同走 update_display_full；该 trace 注入随之删除，产品无调用方。
// The r94 self-rollback dropped the update_display_reader_cleanup call branch (the rc91-93
// physical wipe plus GL16 second pass); periodic cleanup now shares update_display_full with
// manual full refresh, so this trace injection goes with it — the product has no caller left.
static enum EpdDrawError update_display_full(void *hl){(void)hl;return record(FULL,MODE_GC16,(EpdRect){0});}
static enum EpdDrawError update_display_fast_page(void *hl){(void)hl;return record(FAST,MODE_GL16,(EpdRect){0});}
static enum EpdDrawError update_display_mode_diff(void *hl,enum EpdDrawMode mode){(void)hl;return record(READER,mode,(EpdRect){0});}
static enum EpdDrawError update_display_water_turn(void *hl,EpdRect area,int dir){(void)hl;(void)dir;assert(test_tick_us==E0470_TURN_FAST_TICK_US);return record(WATER,MODE_GL16,area);}
'''
shelf += function("main/apps/app_book.c", "paint_control") + "\n"
# 词典弹层：翻页时会关闭它，这里只需空实现。/ Dictionary popup: turns close it; a no-op suffices here.
shelf += "static void dict_popup_reset(void){}\n"
# r93 把预排收口搬进 present：结束函数取产品真实实现（started 恒假时立即返回，与旧桩等价），
# 并让 xSemaphoreTake/s_prep_done 两个既有替身重新被引用。/ r93 folds prefetch teardown
# into present: take the product's real teardown (it returns at once when started is false,
# like the old shim) and keep the existing semaphore doubles referenced.
shelf += function("main/apps/app_book.c", "reader_prefetch_end") + "\n"
for name in ("present", "paint_reading", "reader_turn_chrome", "turn_page"):
    shelf += function("main/apps/app_book.c", name) + "\n"
shelf += r'''
int main(void){
 uint8_t fb=0;app_ctx_t ctx={.fb=&fb};s_view=s_presented_view=SHELF;
 // 缓存封面在合成前就绪；进入书架只有一次输出，不再等待框架先显示。
 // Cached covers are ready before composition; shelf entry uses one output without waiting for a furniture-first frame.
 test_check_cached=true;s_presented_view=-1;
 assert(present(&ctx,APP_REDRAW_PAGE));
 assert(test_prepare_order==1&&test_render_order==2&&pushes==1&&trace_count==1&&route==FAST);
 test_check_cached=false;pushes=trace_count=0;
 unsigned body_before=test_body_draws;
 assert(paint_control(&ctx,(EpdRect){442,94,97,54})==APP_REDRAW_AREA);
 assert(paint_control(&ctx,(EpdRect){551,94,97,54})==APP_REDRAW_AREA);
 assert(test_button_draws==2&&test_body_draws==body_before);
 for(int repeat=0;repeat<80;++repeat){
   int row=repeat%9;EpdRect original=row_rect(row);s_pressed_control=repeat%2?row:-1;
   s_reader_fullscreen=true;
   assert(paint_control(&ctx,original)==APP_REDRAW_AREA&&s_shelf_feedback_pending);
   assert(s_area.x==original.x&&s_area.y==original.y-16&&s_area.width==original.width&&s_area.height==original.height+16);
   unsigned before=pushes;assert(present(&ctx,APP_REDRAW_AREA));
   assert(pushes==before+1&&route==DIFF&&pushed_mode==MODE_GL16&&!s_shelf_feedback_pending);
   assert(pushed_area.y==original.y-16&&pushed_area.height==original.height+16);
 }
 // 列表仅绘制封面；三模式差分局部输出，缓存不重读，不累计整页清理。
 // List repaint is cover-only; all modes use local differentials without rereading covers or accruing whole-page cleanup.
 body_before=test_body_draws;unsigned prepares_before=test_prepares;
 for(int fast=0;fast<2;++fast)for(int repeat=0;repeat<80;++repeat){
   test_style=5;test_fast=fast;s_pressed_control=repeat%2?repeat%4:-1;
   s_view=s_presented_view=SHELF;s_reader_fullscreen=true;s_du_count=5;
   EpdRect original=row_rect(repeat%4);
   assert(paint_control(&ctx,original)==APP_REDRAW_AREA&&s_shelf_feedback_pending);
   unsigned before=pushes;assert(present(&ctx,APP_REDRAW_AREA));
   assert(pushes==before+1&&route==DIFF&&pushed_mode==(fast?MODE_DU:MODE_GL16));
   assert(trace_wave[trace_count-1]==(fast?E0470_FOLLOW_WAVEFORM:E0470_WAVEFORM));
   assert(pushed_area.x==71&&pushed_area.width==120&&pushed_area.height==164&&!s_du_count);
   if(trace_count>240)trace_count=0;
 }
 assert(test_body_draws==body_before&&test_prepares==prepares_before&&test_list_draws==160);
 test_style=0;test_fast=false;
 puts("PASS: 160 list bounces/settles use cover-only differentials and reuse caches; gray/fast modes retain their waveforms and navigation");
 s_reader_fullscreen=false;assert(paint_control(&ctx,(EpdRect){500,90,90,54})==APP_REDRAW_AREA&&!s_shelf_feedback_pending);
 assert(present(&ctx,APP_REDRAW_AREA)&&route==AREA&&pushed_mode==MODE_DU);
 assert(paint_control(&ctx,row_rect(0))==APP_REDRAW_AREA);s_view=READING;
 assert(present(&ctx,APP_REDRAW_AREA)&&route==READER&&!s_shelf_feedback_pending);
 s_view=s_presented_view=SHELF;paint_control(&ctx,row_rect(0));
 assert(present(&ctx,APP_REDRAW_FULL)&&route==FULL&&pushed_mode==MODE_GC16&&!s_shelf_feedback_pending);
 puts("PASS: 80 cover lift/cancel feedbacks use one grayscale differential, preserve the union and never force cleanup; buttons, view transitions and manual refresh retain their paths");
 // 阅读翻页只有一次正文灰阶推屏；页脚、清残影、图片、水波纹与设置面板保持原路由。
 // One grayscale body commit per text turn; footer, cleanup, images, water and settings keep their routes.
 s_view=s_presented_view=READING;s_text="text";s_reader_panel=READER_PANEL_NONE;
 s_area=reader_area();s_mode=MODE_GL16;s_reader_turn_pending=s_reader_footer_pending=true;
 trace_count=0;assert(present(&ctx,APP_REDRAW_AREA));
 assert(trace_count==2&&trace_route[0]==DIFF&&trace_mode[0]==MODE_GL16&&trace_wave[0]==E0470_SOFT_TEXTTURN_WAVEFORM);
 assert(trace_area[0].y==reader_area().y&&trace_area[0].height==reader_area().height);
 assert(trace_route[1]==AREA&&trace_mode[1]==MODE_DU&&trace_wave[1]==E0470_FOLLOW_WAVEFORM);
 assert(trace_area[1].y==progress_rect().y&&!s_reader_turn_pending);
 for(int full=0;full<2;++full){
   s_reader_fullscreen=full;s_reader_turn_pending=true;s_reader_cleanup=true;
   s_reader_footer_pending=!full;trace_count=0;
   // r94 回滚后周期性清理与手动全刷同路：一次整屏 GC16 目标帧，没有第二条 GL16 补刷路由。
   // Since the r94 rollback periodic cleanup shares the manual full-refresh path: one whole-screen GC16 target, no second GL16 pass.
   assert(present(&ctx,APP_REDRAW_AREA)&&trace_count==1&&route==FULL&&pushed_mode==MODE_GC16);
   s_reader_turn_pending=true;trace_count=0;
   assert(present(&ctx,APP_REDRAW_FULL)&&trace_count==1&&route==FULL);
 }
 s_reader_fullscreen=false;s_reader_turn_pending=true;s_reader_image_refresh_pending=true;
 trace_count=0;assert(present(&ctx,APP_REDRAW_AREA));
 assert(trace_count==1&&route==AREA&&pushed_mode==MODE_GC16&&trace_wave[0]==E0470_FULL_WAVEFORM);
 s_reader_turn_pending=s_water_turn_pending=true;trace_count=0;
 assert(present(&ctx,APP_REDRAW_AREA)&&trace_count==1&&route==WATER);
 // 翻页标志已消费，下一次设置绘制不能误走翻页差分。
 // The consumed turn flag must not affect the next settings repaint.
 s_reader_panel=99;trace_count=0;
 assert(present(&ctx,APP_REDRAW_AREA)&&trace_count==1&&route==AREA&&pushed_mode==MODE_GL16&&trace_wave[0]==E0470_WAVEFORM);
 s_reader_panel=READER_PANEL_NONE;s_reader_fullscreen=true;s_reader_turn_pending=true;trace_count=0;
 assert(present(&ctx,APP_REDRAW_AREA)&&trace_count==2&&trace_route[0]==DIFF&&trace_route[1]==DIFF&&trace_wave[0]==E0470_SOFT_TEXTTURN_WAVEFORM&&trace_wave[1]==E0470_WAVEFORM);
 // 快水波仅接管实际动画输出；成功、失败及全屏都恢复主页节拍，优先级分支不改节拍。
 // Fast ripple scopes pacing to actual animation output; success, failure and full screen restore main pacing, while priority branches never change it.
 for(int full=0;full<2;++full)for(int error=0;error<2;++error){
   s_reader_fullscreen=full;s_reader_footer_pending=false;s_water_turn_pending=true;
   test_push_error=error;test_tick_us=full?19000:12000;int prior=test_tick_us,sets=test_tick_sets;
   trace_count=0;assert(present(&ctx,APP_REDRAW_AREA));
   assert(trace_route[0]==WATER&&test_tick_us==prior&&test_tick_sets==sets+2&&!s_water_turn_pending);
 }
 test_push_error=0;test_tick_us=12000;s_reader_fullscreen=false;
 for(int priority=0;priority<3;++priority){
   s_water_turn_pending=true;s_reader_cleanup=priority==1;s_reader_image_refresh_pending=priority==2;
   int sets=test_tick_sets;trace_count=0;
   assert(present(&ctx,priority==0?APP_REDRAW_FULL:APP_REDRAW_AREA));
   assert(trace_route[0]!=(int)WATER&&test_tick_sets==sets&&test_tick_us==12000&&!s_water_turn_pending);
 }
 puts("PASS: text turn/body/footer, periodic/manual cleanup, grayscale image, water effect, settings and full-screen routes");
 for(int input_view=EDIT;input_view<=SEARCH;input_view++)for(int repeat=0;repeat<80;repeat++){
   s_view=input_view;s_reader_fullscreen=true;s_reader_footer_pending=true;
   s_area=(EpdRect){36,243,612,82};s_mode=repeat==0?MODE_GL16:MODE_DU;s_du_count=10;
   trace_count=0;assert(present(&ctx,APP_REDRAW_AREA));
   assert(trace_count==1&&route==DIFF&&trace_wave[0]==(repeat==0?E0470_WAVEFORM:E0470_FOLLOW_WAVEFORM));
   assert(pushed_area.y==243&&pushed_area.height==82&&!s_du_count);
 }
 for(int input_view=EDIT;input_view<=SEARCH;input_view++){
   s_view=input_view;s_reader_footer_pending=true;s_input_settle=true;
   s_area=(EpdRect){24,612,636,114};s_mode=MODE_DU;s_du_count=10;
   trace_count=0;assert(present(&ctx,APP_REDRAW_AREA));
   assert(trace_count==1&&route==LOCAL_FULL&&pushed_mode==MODE_GL16&&trace_wave[0]==E0470_WAVEFORM);
   assert(pushed_area.x==24&&pushed_area.y==612&&pushed_area.width==636&&pushed_area.height==114);
   assert(!s_input_settle&&!s_du_count);
 }
 s_view=s_presented_view=READING;s_reader_fullscreen=false;s_reader_footer_pending=false;
 puts("PASS: 160 search/title updates keep a single local push; idle settles drive only the candidate strip with full-pixel GL16 and never refresh the footer or whole page");

 // 实际翻页、绘制和推屏函数共用页类型；混排与跨章不靠纯图标志判定。
 // Actual turn/paint/present functions share page types; mixed and cross-chapter guards do not rely on the pure-image flag.
 for(int full=0;full<2;++full)for(int hidden=0;hidden<2;++hidden)
 for(int from=0;from<3;++from)for(int to=0;to<3;++to){
   s_reader_fullscreen=full;test_hide_images=hidden;test_types[0][0]=from;test_types[0][1]=to;
   s_view=s_presented_view=READING;s_reader_panel=READER_PANEL_NONE;s_chapter=s_page=0;
   trace_count=0;present(&ctx,paint_reading(&ctx,MODE_GL16));
   assert(s_reader_text_frame==(hidden||from==0));
   app_redraw_t redraw=turn_page(&ctx,1);
   bool eligible=hidden||(from==0&&to==0);
   assert(s_reader_fullscreen && s_reader_panel==READER_PANEL_NONE);
   if (!full) {assert(redraw==APP_REDRAW_PAGE&&!s_reader_turn_pending);trace_count=0;present(&ctx,redraw);assert(trace_route[0]==(to&&!hidden?WHOLE:READER));continue;}
   assert(redraw==APP_REDRAW_AREA&&s_reader_turn_pending==eligible);
   trace_count=0;present(&ctx,redraw);
   if(eligible)assert(trace_route[0]==DIFF&&trace_wave[0]==E0470_SOFT_TEXTTURN_WAVEFORM);
   else if(to)assert(trace_mode[0]==MODE_GC16&&trace_wave[0]==E0470_FULL_WAVEFORM);
   else assert(trace_wave[0]==E0470_WAVEFORM&&trace_route[0]==(full?DIFF:AREA));
   assert(!s_reader_turn_pending&&s_reader_text_frame==(hidden||to==0));
 }
 s_reader_fullscreen=true;test_hide_images=false;
 for(int from=0;from<3;++from)for(int to=0;to<3;++to)for(int dir=-1;dir<=1;dir+=2){
   int old=dir>0?0:1,new=1-old;
   test_types[old][dir>0?1:0]=from;test_types[new][dir>0?0:1]=to;
   s_chapter=old;s_page=dir>0?1:0;trace_count=0;
   present(&ctx,paint_reading(&ctx,MODE_GL16));
   app_redraw_t redraw=turn_page(&ctx,dir);
   assert(s_chapter==(unsigned)new&&s_reader_turn_pending==(from==0&&to==0));
   trace_count=0;present(&ctx,redraw);
   assert((trace_wave[0]!=E0470_TEXTTURN_WAVEFORM&&trace_wave[0]!=E0470_SOFT_TEXTTURN_WAVEFORM)||(from==0&&to==0));
 }
 // 失败推屏、面板及离开阅读会失效旧基准；下一次正常绘制后恢复。
 // Failed presents, panels and leaving reading invalidate history; a successful body restores it.
 memset(test_types,0,sizeof(test_types));s_chapter=s_page=0;
 test_push_error=EPD_DRAW_ERROR;trace_count=0;present(&ctx,paint_reading(&ctx,MODE_GL16));
 assert(!s_reader_text_frame);test_push_error=0;
 assert(turn_page(&ctx,1)==APP_REDRAW_AREA&&!s_reader_turn_pending);
 trace_count=0;present(&ctx,APP_REDRAW_AREA);assert(s_reader_text_frame);
 s_reader_panel=99;trace_count=0;present(&ctx,APP_REDRAW_PAGE);assert(!s_reader_text_frame);
 s_reader_panel=READER_PANEL_NONE;s_page=0;
 assert(turn_page(&ctx,1)==APP_REDRAW_AREA&&!s_reader_turn_pending);
 trace_count=0;present(&ctx,APP_REDRAW_AREA);assert(s_reader_text_frame);
 s_view=SHELF;trace_count=0;present(&ctx,APP_REDRAW_PAGE);assert(!s_reader_text_frame);
 s_view=s_presented_view=READING;s_chapter=0;s_page=1;test_load_failed=true;
 assert(turn_page(&ctx,1)==APP_REDRAW_PAGE&&!s_reader_turn_pending&&s_view==TOC);
 test_load_failed=false;s_view=s_presented_view=READING;s_page=0;
 for(int policy=0;policy<2;++policy){
   s_page=0;trace_count=0;present(&ctx,paint_reading(&ctx,MODE_GL16));
   test_full_pages=policy?1:0;test_effect=policy?0:1;s_turns=0;
   app_redraw_t redraw=turn_page(&ctx,1);trace_count=0;present(&ctx,redraw);
   assert(trace_route[0]==(policy?FULL:WATER));
 }
 test_full_pages=0;test_effect=0;s_page=0;test_types[0][0]=1;test_image.gray=NULL;
 trace_count=0;present(&ctx,paint_reading(&ctx,MODE_GL16));assert(!s_reader_text_frame);
 assert(turn_page(&ctx,1)==APP_REDRAW_AREA&&!s_reader_turn_pending);
 puts("PASS: 36 text/mixed/image/hidden/full-screen combinations, 18 bidirectional chapter transitions, failed image/display/load guards and actual cleanup/water turns");
}
'''

nav = (root / "main/ui/ui_nav.c").read_text()
defines = "\n".join(re.findall(r"^#define (?:UI_NAV_(?:NETWORK|BOLT)_ICON_PX|UI_STATUS_\w+_PX) .+$", nav, re.M))
status = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
typedef struct {int x,y,width,height;} EpdRect;
enum EpdFontFlags {EPD_DRAW_ALIGN_LEFT,EPD_DRAW_ALIGN_CENTER,EPD_DRAW_ALIGN_RIGHT};
enum {UI_ICON_WIFI,UI_ICON_BLUETOOTH,UI_ICON_ZAP};
#define UI_LOCK_WIDTH 684
#define UI_GRAY_BLACK 0
#define UI_GRAY_WHITE 255
#define UI_INK_BLACK 0
#define UI_INK_WHITE 15
#define READ_PICO_TRANSFER_MODE_STA 1
typedef struct {bool time_synced;time_t unix_sec;} pmu_snapshot_t;
typedef struct {bool network_ready;int mode;} read_pico_transfer_status_t;
static pmu_snapshot_t pmu;
static int pct;
static bool wifi,bluetooth,s_system_ttf;
#define TTF_FONT_PATH_MAX 160
static bool chosen_system,reading_context;
static const char *app_settings_system_font_path(void){return chosen_system?"system.ttf":"";}
static const char *ttf_font_path(void){return reading_context?"reader.ttf":chosen_system?"system.ttf":"builtin";}
static bool ttf_font_is_builtin(void){return false;}
static char signature[96];
static const pmu_snapshot_t *read_pico_pmu_get(void){return &pmu;}
static int pmu_battery_percent(const pmu_snapshot_t *snapshot){(void)snapshot;return pct;}
static bool pmu_battery_charging(const pmu_snapshot_t *snapshot){(void)snapshot;return false;}
static void read_pico_transfer_get_status(read_pico_transfer_status_t *status){*status=(read_pico_transfer_status_t){wifi,READ_PICO_TRANSFER_MODE_STA};}
static bool app_settings_ble_turner(void){return bluetooth;}
static const char *app_settings_status_signature(void){return signature;}
static int letters(const char *s){int n=0;for(;*s;++s)if(((unsigned char)*s&0xc0)!=0x80)++n;return n;}
static bool ui_font_has_text(const char *s){(void)s;return true;}
static int ui_font_text_width_px(int px,const char *s){return letters(s)*px/2;}
static int ttf_text_width_px(int px,const char *s){return letters(s)*px*3/4;}
static void ui_font_measure_line_px(int px,const char *s,int *above,int *below){(void)s;*above=px*3/4;*below=1;}
static void ttf_measure_line_px(int px,const char *s,int *above,int *below){(void)s;*above=px*2/3;*below=px/5;}
/* 屏幕宽度入口：我方 ui_nav.c 把右侧集群改成按实际宽度锚定（ui_w()），竖屏 684 时
   与原硬编码 595/606/648/610 逐像素一致。/ Our ui_nav.c anchors the right cluster to the
   real width; 684 in portrait reproduces the old constants pixel for pixel. */
static int ui_w(void){return 684;}
typedef struct {int left,right,center,px,align;char text[96];} text_t;
static text_t texts[3];static int text_count,network_left;
static void draw_text(uint8_t *fb,int x,int baseline,int px,const char *s,enum EpdFontFlags align,int fg,int bg){
 (void)fb;(void)fg;(void)bg;assert(text_count<3);
 int above,below,width=s_system_ttf?ttf_text_width_px(px,s):ui_font_text_width_px(px,s);
 if(s_system_ttf)ttf_measure_line_px(px,s,&above,&below);else ui_font_measure_line_px(px,s,&above,&below);
 text_t *t=&texts[text_count++];t->left=x-(align==EPD_DRAW_ALIGN_RIGHT?width:align==EPD_DRAW_ALIGN_CENTER?width/2:0);
 t->right=t->left+width;t->center=baseline+(below-above)/2;t->px=px;t->align=align;snprintf(t->text,sizeof(t->text),"%s",s);
}
static void ui_font_draw_text_px(uint8_t *f,int x,int y,int px,const char *s,enum EpdFontFlags a,int fg,int bg,bool bw){(void)bw;draw_text(f,x,y,px,s,a,fg,bg);}
static void ttf_draw_text_px(uint8_t *f,int x,int y,int px,const char *s,enum EpdFontFlags a,int fg,int bg){draw_text(f,x,y,px,s,a,fg,bg);}
static void ui_draw_icon(uint8_t *fb,int x,int y,int px,int icon,int gray){(void)fb;(void)gray;assert(y==40);if(icon==UI_ICON_ZAP)return;if(x-px/2<network_left)network_left=x-px/2;}
static void ui_nav_wifi_icon(uint8_t *fb,int x,int y,int px,int gray){ui_draw_icon(fb,x,y,px,UI_ICON_WIFI,gray);}
static void ui_draw_round_rect(uint8_t *fb,EpdRect box,int radius,int gray){(void)fb;(void)radius;(void)gray;assert(box.y+box.height/2==40);}
static void ui_fill_round_rect(uint8_t *fb,EpdRect box,int radius,int gray){(void)fb;(void)box;(void)radius;(void)gray;}
'''
status += defines + "\n"
for path, name in (("main/ui/ui_kit.c", "ui_text_fixed_context_width_px"),
                   ("main/ui/ui_kit.c", "ui_text_fixed_width_px"),
                   ("main/ui/ui_kit.c", "ui_text_fixed_context_vc"),
                   ("main/ui/ui_kit.c", "ui_text_fixed_vc")):
    status += function(path, name) + "\n"
status += nav[nav.index('static bool s_status_reading;'):nav.index('static const char *const labels[]')]
for name in ('status_time','clock_text','ui_nav_status'):
    status += function('main/ui/ui_nav.c',name) + '\n'
status += r'''
int main(void){
 uint8_t fb=0;const int percentages[]={0,9,78,100,-1};
 for(int font=0;font<2;++font)for(int mask=0;mask<4;++mask)for(unsigned p=0;p<5;++p){
   reading_context=false;chosen_system=s_system_ttf=font;wifi=mask&1;bluetooth=mask&2;pct=percentages[p];text_count=0;network_left=1000;
   snprintf(signature,sizeof(signature),"时间统一 · 正在阅读我的收藏书籍，保持热爱 ABCabc");
   ui_nav_status_font_context(false,true);
   ui_nav_status(&fb);assert(text_count==3&&texts[0].px==28&&texts[1].px==26&&texts[2].px==28);
   for(int i=0;i<3;++i)assert(texts[i].center>=39&&texts[i].center<=41);
   assert(texts[0].right+12<=texts[1].left);
   assert(texts[1].right+12<=(mask?network_left:texts[2].left));
   assert(texts[2].right==595&&texts[1].align==EPD_DRAW_ALIGN_CENTER);
   assert(texts[1].left+texts[1].right>=683&&texts[1].left+texts[1].right<=685);
   size_t length=strlen(texts[1].text);assert(length&&((unsigned char)texts[1].text[length-1]&0xc0)!=0xc0);
   char system_prefix[96];snprintf(system_prefix,sizeof(system_prefix),"%s",texts[1].text);
   int system_network_left=network_left;
   // 阅读字体变化不重算系统签名宽度；不同字体仍显示同一UTF8前缀并保持图标间距。
   // Reader face changes do not recompute system signature widths: retain the same UTF8 prefix and icon spacing.
   reading_context=true;s_system_ttf=false;ui_nav_status_font_context(true,true);text_count=0;network_left=1000;
   ui_nav_status(&fb);assert(text_count==3&&!strcmp(texts[1].text,system_prefix)&&network_left==system_network_left);
   assert(texts[0].right+12<=texts[1].left&&texts[1].right+12<=(mask?network_left:texts[2].left));
 }
 reading_context=false;ui_nav_status_font_context(false,false);
 signature[0]=0;text_count=0;ui_nav_status(&fb);assert(text_count==2);
 puts("PASS: larger status text shares the icon centerline with builtin/custom metrics; simultaneous radios, 100% and long UTF-8 signatures do not overlap");
}
'''

navigation = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "main/assets/ui_icons.h"
#include "main/ui/ui_nav_layout.h"
typedef struct {int x,y,width,height;} EpdRect;
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
#define UI_GRAY_BLACK 0
#define UI_GRAY_WHITE 255
#define UI_GRAY_LIGHT 224
#define EPD_DRAW_ALIGN_CENTER 1
#define UI_NAV_TAB_ICON_PX 44
enum {UI_CLICK_NAV};
static void ui_click_feedback_register(uint8_t *fb,EpdRect r,int kind,ui_icon_t icon){(void)fb;(void)icon;assert(kind==UI_CLICK_NAV&&r.width==60&&r.height==60);}
static const char *const labels[]={"首页","书架","文件管理","设置"};
enum {APP_MAIN_REFRESH_NORMAL,APP_MAIN_REFRESH_FAST,APP_MAIN_REFRESH_WATER};
static int main_choice;
static int app_settings_main_refresh_mode(void){return main_choice;}
static int first_ink, markers, tracks, texts, guards;
static uint8_t ui_read_pixel(const uint8_t *fb,int x,int y){(void)fb;(void)x;(void)y;return 14;}
static uint8_t ui_contrast_gray(uint8_t value){return value&240;}
static void epd_draw_pixel(int x,int y,uint8_t value,uint8_t *fb){(void)x;(void)value;(void)fb;if(y<first_ink)first_ink=y;}
static void epd_fill_rect(EpdRect area,int gray,uint8_t *fb){
 (void)fb;
 if(area.y==UI_NAV_TOP+1){assert(area.height==5);if(gray==UI_GRAY_BLACK){assert(area.width==56);++markers;}
 else {assert(gray==UI_GRAY_WHITE&&area.width==UI_LOCK_WIDTH);++tracks;}}
 if(area.y==UI_NAV_REFRESH_END){assert(area.height==UI_NAV_TOP-UI_NAV_REFRESH_END&&area.width==UI_LOCK_WIDTH&&gray==0xf0);++guards;}
}
static void ui_hairline(uint8_t *fb,int y,int x,int width,int gray){(void)fb;(void)x;(void)width;(void)gray;assert(y==UI_NAV_TOP);}
static void ui_text(uint8_t *fb,int x,int y,int px,const char *label,int align,bool wrap){(void)fb;(void)x;(void)label;(void)align;(void)wrap;assert(y==UI_NAV_TOP+74&&px==17);++texts;}
'''
navigation += 'static void ui_fill_rect_fast(EpdRect r,uint8_t gray,uint8_t *fb){epd_fill_rect(r,gray,fb);}'
for path, name in (("main/ui/ui_kit.c", "icon_nibble"), ("main/ui/ui_kit.c", "ui_draw_icon"),
                   ("main/ui/ui_nav.c", "icon"), ("main/ui/ui_nav.c", "ui_nav_draw"), ("main/ui/ui_nav.c", "ui_nav_hit")):
    navigation += function(path, name) + "\n"
navigation += r'''
int main(void){
 uint8_t fb=0;
 for(int x=0;x<684;++x){
   assert(ui_nav_hit(x,UI_NAV_TOUCH_TOP-1)==-1);
   for(int y=UI_NAV_TOUCH_TOP;y<1216;++y)assert(ui_nav_hit(x,y)==x/171);
   assert(ui_nav_hit(x,1216)==-1);
 }
 assert(ui_nav_hit(684,1141)==-1&&ui_nav_hit(UINT16_MAX,1141)==-1);
 assert(UI_NAV_TOUCH_TOP==1080&&UI_NAV_TOP==1096&&UI_NAV_REFRESH_END==1088);
 for(main_choice=0;main_choice<3;++main_choice)for(int active=0;active<4;++active){
   first_ink=2000;markers=tracks=texts=guards=0;ui_nav_draw(&fb,active);
   assert(markers==1&&texts==4&&tracks==(main_choice!=0)&&guards==1);
   // 验证真实图标掩模的第一笔，单次刷新32列对齐后不能触碰图标。
   // Check actual mask ink: the 32-column-expanded single update must not reach an icon.
   assert(first_ink>=UI_NAV_MARKER_SCAN_END);
   assert(UI_NAV_REFRESH_END%32==0&&UI_NAV_REFRESH_END<UI_NAV_TOP+1);
 }
 puts("PASS: aligned content refresh stops above marker and actual navigation masks; blank guard and production layout retained");
}
'''

image_bw = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "main/ui/ui_image_dither.h"
typedef struct {int x,y,width,height;} EpdRect;
enum {EPD_ROT_LANDSCAPE,EPD_ROT_PORTRAIT,EPD_ROT_INVERTED_LANDSCAPE,EPD_ROT_INVERTED_PORTRAIT};
static int rotation;
static int epd_width(void){return 16;}
static int epd_height(void){return 12;}
static int epd_get_rotation(void){return rotation;}
static int epd_rotated_display_width(void){return rotation%2?12:16;}
static int epd_rotated_display_height(void){return rotation%2?16:12;}
static uint8_t epd_get_pixel(int x,int y,int w,int h,const uint8_t* fb){
 assert(w==16&&h==12&&x>=0&&x<w&&y>=0&&y<h);
 int index=y*w+x;return ((fb[index/2]>>((index&1)*4))&15)<<4;
}
static void epd_draw_pixel(int x,int y,uint8_t gray,uint8_t* fb){
 int px=x,py=y;
 if(rotation==EPD_ROT_PORTRAIT){px=15-y;py=x;}
 if(rotation==EPD_ROT_INVERTED_LANDSCAPE){px=15-x;py=11-y;}
 if(rotation==EPD_ROT_INVERTED_PORTRAIT){px=y;py=11-x;}
 assert(px>=0&&px<16&&py>=0&&py<12);
 int index=py*16+px,shift=(index&1)*4;
 fb[index/2]=(fb[index/2]&~(15<<shift))|((gray>>4)<<shift);
}
'''
for name in ("ui_read_pixel", "ui_image_bw_rect"):
    image_bw += function("main/ui/ui_kit.c", name) + "\n"
image_bw += r'''
int main(void){
 uint8_t fb[96],original[96];
 for(rotation=0;rotation<4;++rotation){
   memset(fb,0x77,sizeof(fb));memcpy(original,fb,sizeof(fb));
   ui_image_bw_rect(fb,(EpdRect){-1,-1,6,6});
   for(int y=0;y<epd_rotated_display_height();++y)for(int x=0;x<epd_rotated_display_width();++x){
     int value=ui_read_pixel(fb,x,y);
     if(x<5&&y<5)assert(value==0||value==15);else assert(value==7);
   }
   memcpy(original,fb,sizeof(fb));ui_image_bw_rect(fb,(EpdRect){-1,-1,6,6});
   assert(!memcmp(fb,original,sizeof(fb)));
   ui_image_bw_rect(fb,(EpdRect){50,50,82,82});assert(!memcmp(fb,original,sizeof(fb)));
   ui_image_bw_rect(NULL,(EpdRect){0,0,82,82});
 }
 puts("PASS: avatar BW dots stay bounded and stable across four framebuffer rotations, including clipped and rounded white edges");
}
'''

with tempfile.TemporaryDirectory() as folder:
    for name, unit in (("shelf", shelf), ("status", status),
                       ("nav-modes", navigation), ("image-bw", image_bw)):
        source, binary = Path(folder) / (name + ".c"), Path(folder) / name
        source.write_text(unit)
        extras = [str(root / "main/ui/ui_image_dither.c")] if name == "image-bw" else []
        subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-error=format-truncation",
                    "-fsanitize=address,undefined",
                        "-I" + str(root),
                        str(source), *extras, "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
