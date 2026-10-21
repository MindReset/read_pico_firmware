/* SPDX-License-Identifier: Apache-2.0
 * 中文：运行真实主循环，用确定性触摸和任务延时退出验证调度。
 * English: Run the real loop with deterministic touch and task-delay exit.
 */
#include "app_loop.h"
#include "ui_gesture.h"
#include "ui_menu.h"
#include "ui_power_dialog.h"
#include "ui_quick_menu.h"
#include "boot_state.h"
uint8_t app_settings_system_contrast(void) {return 100;}
void pico_boot_ready(void) {}
void app_book_cover_mode_changed(void) {}
void app_home_cover_mode_changed(void) {}
esp_err_t pico_boot_save_resume(const pico_resume_t *resume) { (void)resume; return ESP_OK; }
void pico_boot_clear_resume(void) {}
bool app_book_resume_context(char *p,size_t c,bool *f) { (void)p;(void)c;(void)f;return false; }
#include "read_pico_transfer.h"
#include "ble_page_turner.h"
#include "ui_nav_layout.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

// 横屏旋转替身：默认竖屏 INVERTED_PORTRAIT（设备实际方向），用例可切换；逻辑帧
// 尺寸随方向变（横屏 1216x684、竖屏 684x1216），与 epdiy 的 epd_width/epd_height
// 语义一致，app_loop 的横屏映射直接依赖这两个尺寸。
// Rotation shim: portrait INVERTED_PORTRAIT (the device's real orientation) by
// default; cases may switch it. Logical frame sizes follow the rotation
// (landscape 1216x684, portrait 684x1216), matching epdiy semantics, which
// app_loop's landscape remap relies on.
enum EpdRotation host_rotation = EPD_ROT_INVERTED_PORTRAIT;
enum EpdRotation epd_get_rotation(void) { return host_rotation; }
// 与 epdiy 真实语义一致：纯状态设置。/ Matches the real epdiy semantics: a pure
// state set. Lets lock/exit fallback cases drive and observe the reset.
void epd_set_rotation(enum EpdRotation rotation) { host_rotation = rotation; }
int epd_width(void) { return host_rotation == EPD_ROT_LANDSCAPE ? 1216 : 684; }
int epd_height(void) { return host_rotation == EPD_ROT_LANDSCAPE ? 684 : 1216; }

const app_desc_t app_book = {.title="book"};
const app_desc_t app_transfer = {.title="transfer"};
static int power_open_draws, power_controls_draws, power_final_draws;
static int power_trigger_step = -1, shutdowns, restarts;
static bool staged_shutdown;
EpdRect ui_power_dialog_rect(void) {return (EpdRect){0,0,684,1216};}
void ui_power_dialog_draw(uint8_t* fb,const ui_power_dialog_t *dialog) {(void)fb;(void)dialog;++power_open_draws;}
void ui_power_dialog_draw_controls(uint8_t* fb,const ui_power_dialog_t *dialog) {(void)fb;(void)dialog;++power_controls_draws;}
void ui_power_final_draw(uint8_t* fb,bool restarting) {(void)fb;(void)restarting;++power_final_draws;}

static jmp_buf done;
typedef struct { int x, y, count, error; } sample_t;
static sample_t samples[32];
static int sample_count, step, ticks, enters, exits, touch_calls, keys[3], events[16];
static int menus, highlights, restores, fulls, mode, tick_consumed[32];
static int long_keys;
static uint8_t idle_minutes;
static int idle_locks, before_locks, lock_feedback_calls, lock_badges, wake_clean_requests;
static bool feedback_enabled;
static bool online_busy;
static bool ble_enabled, saved_wifi, upload_busy, click_enabled, click_active;
static read_pico_transfer_status_t transfer_status;
static int quick_draws, quick_gl, quick_du, wifi_starts, wifi_stops, ble_resets, click_presses, click_restores;

static app_redraw_t long_key(app_ctx_t* c, int k) { assert(k==UI_KEY_2); ++long_keys; c->request_menu=true; return APP_REDRAW_NONE; }
static app_redraw_t response;
static bool request_on_touch, request_on_tick, menu_on_tick, menu_on_touch, cancel_clobber;
static bool home_on_touch, home_on_tick, home_on_key, start_second;
static int home_enters, home_renders, last_menu_leaf, rendered_leaf;
static const app_desc_t* menu_background;
static app_desc_t first, second;
int ui_nav_hit(uint16_t x,uint16_t y){return y>=UI_NAV_TOUCH_TOP&&y<1216&&x<684?x/171:-1;}
void ui_nav_request(app_ctx_t *ctx,int index){ctx->request_app=index==0?&first:&second;}
static bool full_tick, lock_due, font_due, shake_activity;
static int64_t time_offset, time_step;
static int du_areas, gl_areas;
static bool main_fast, main_water;
int app_settings_main_refresh_mode(void) {return main_water?APP_MAIN_REFRESH_WATER:main_fast?APP_MAIN_REFRESH_FAST:APP_MAIN_REFRESH_NORMAL;}
static int shelf_exit_arms;
static int nav_arms, nav_draws, nav_full_arms, nav_area_draws;
static const void *nav_armed;
static bool main_source_visible, main_target_visible;
static bool media_test, sd_font, saved_sd_font;
static bool mounted_steps[32], present_steps[32];
static int media_lost, media_ready, builtin_opens, font_opens, probes, loss_step;
int E0470_WAVEFORM;
int E0470_FOLLOW_WAVEFORM;
enum EpdDrawError update_display_area_diff_with(EpdiyHighlevelState*h,const void*w,int m,EpdRect a) {
    if (a.height==UI_QUICK_HEIGHT) {assert(a.x==0&&a.y==0&&a.width==684);++quick_du;}
    if (a.y==1120) assert(w==&E0470_FOLLOW_WAVEFORM&&m==MODE_DU&&a.height==48&&a.width==50);
    return update_display_area_with(h,w,m,a);
}
enum EpdDrawError update_display_area_full_with(EpdiyHighlevelState*h,const void*w,int m,EpdRect a) {
    assert(a.x==0&&a.y==0&&a.width==684&&a.height==UI_QUICK_HEIGHT);++quick_gl;
    return update_display_area_with(h,w,m,a);
}
void ui_quick_menu_draw(uint8_t *fb,bool wifi,bool bluetooth) {(void)fb;(void)wifi;(void)bluetooth;++quick_draws;}
bool app_settings_ble_turner(void) {return ble_enabled;}
void app_settings_set_ble_turner(bool enabled) {ble_enabled=enabled;}
void ble_pt_reset_failure(void) {++ble_resets;}
bool ble_pt_pop_key(ble_pt_event_t *e) {(void)e;return false;}
bool ble_pt_pop_raw(ble_pt_raw_t *e) {(void)e;return false;}
void read_pico_transfer_get_status(read_pico_transfer_status_t *s) {*s=transfer_status;}
int read_pico_transfer_get_saved_wifi(char *ssid,bool *configured) {strcpy(ssid,"saved");*configured=saved_wifi;return ESP_OK;}
int read_pico_transfer_start(const read_pico_transfer_cfg_t *cfg) {assert(cfg->mode==READ_PICO_TRANSFER_MODE_STA&&cfg->network_only);++wifi_starts;transfer_status.mode=cfg->mode;transfer_status.state=1;return ESP_OK;}
bool read_pico_transfer_try_stop_if_idle(void) {if(upload_busy)return false;++wifi_stops;transfer_status.state=0;return true;}
void ui_click_feedback_reset(void) {click_active=false;}
void ui_click_feedback_begin(uint8_t *fb) {(void)fb;ui_click_feedback_reset();}
bool ui_click_feedback_active(void) {return click_active;}
bool ui_click_feedback_press(uint8_t *fb,int x,int y,EpdRect *r) {(void)fb;if(!click_enabled||y!=1141)return false;(void)x;*r=(EpdRect){60,1120,50,48};click_active=true;++click_presses;return true;}
bool ui_click_feedback_release(uint8_t *fb,EpdRect *r) {(void)fb;if(!click_active)return false;click_active=false;++click_restores;*r=(EpdRect){60,1120,50,48};return true;}
bool ui_click_feedback_cancel_at(int x,int y) {return x>120||y!=1141;}
int cst836u_read(void* h, cst836u_touch_t* t) {
    (void)h;
    sample_t s = samples[step];
    *t = (cst836u_touch_t){ .touched=s.count>0, .count=s.count, .x=s.x, .y=s.y };
    t->points[0].active=s.count>0;
    return s.error;
}
int cst836u_get_info(void* h,cst836u_info_t* i) {(void)h;(void)i;return 0;}
const char* esp_err_to_name(int e) {(void)e;return "test";}
int64_t esp_timer_get_time(void) {return time_offset+(int64_t)(step+1)*time_step;}
void vTaskDelay(int ms) {(void)ms;if (++step>=sample_count) longjmp(done,1);}
bool continuous_du_init(void) {return true;}
void display_request_full_refresh(void) {++wake_clean_requests;}
void guard_draw_result(EpdiyHighlevelState* h,enum EpdDrawError e) {(void)h;assert(e==0);}
enum EpdDrawError update_display_area_with(EpdiyHighlevelState*h,const void*w,int m,EpdRect a) {(void)h;(void)w;(void)a;nav_area_draws+=nav_armed!=NULL;nav_armed=NULL;if(m==MODE_DU)du_areas++;else if(m==MODE_GL16)gl_areas++;return 0;}
enum EpdDrawError update_display_full(EpdiyHighlevelState*h) {(void)h;fulls++;nav_full_arms+=nav_armed!=NULL;nav_armed=NULL;return 0;}
enum EpdDrawError update_display_mode(EpdiyHighlevelState*h,int m) {(void)h;(void)m;mode++;return 0;}
enum EpdDrawError update_display_mode_diff(EpdiyHighlevelState*h,int m) {(void)h;(void)m;mode++;return 0;}
enum EpdDrawError update_display_fast_page(EpdiyHighlevelState*h) {(void)h;mode++;nav_draws+=nav_armed!=NULL;nav_armed=NULL;return 0;}
enum EpdDrawError update_display_white(EpdiyHighlevelState*h) {(void)h;return 0;}
bool display_take_white_exit(void) {return false;}
void display_main_transition_cancel(void) {nav_armed=NULL;}
void display_main_transition_arm(const void* owner, bool shelf_exit, bool changing_page) {(void)changing_page;display_main_transition_cancel();nav_armed=owner;++nav_arms;shelf_exit_arms+=shelf_exit;}
void display_main_transition_disarm(void) {nav_armed=NULL;}
void rails_idle_check(int64_t n) {(void)n;}
bool read_pico_pmu_ready(void) {return lock_due||step==power_trigger_step;}
read_pico_pmu_key_action_t read_pico_pmu_take_key_action(void) {return step==power_trigger_step?READ_PICO_PMU_KEY_LONG:READ_PICO_PMU_KEY_SHORT;}
void enter_lock_and_sleep(EpdiyHighlevelState*h,int64_t*t,void*a,void*tp,bool reader) {(void)tp;(void)h;(void)t;(void)a;(void)reader;assert(!nav_armed);++idle_locks;lock_due=false;time_offset+=1000000;}
void app_lock_feedback(EpdiyHighlevelState*h) {(void)h;++lock_feedback_calls;if(feedback_enabled){assert(!before_locks&&!idle_locks);++lock_badges;}}
void app_lock_wait_key_idle(int ms) {(void)ms;}
esp_err_t app_enter_host_sleep(app_sleep_mode_t mode) {assert(mode==APP_SLEEP_OFF);++shutdowns;longjmp(done,1);}
void app_restart_host(void) {++restarts;longjmp(done,1);}
void epd_poweroff(void) {}
const char* app_settings_font_path(void) {return "builtin";}
bool app_settings_reader_power_turn(void) {return false;}
bool app_settings_staged_shutdown(void) {return staged_shutdown;}
bool ttf_font_path_is_builtin(const char*p) {(void)p;return !font_due&&!saved_sd_font;}
bool ttf_font_ready(void) {return true;}
bool ttf_font_is_builtin(void) {return !sd_font;}
const char* ttf_font_path(void) {return "other";}
int ttf_font_open(const char*p) {(void)p;font_opens++;sd_font=true;return 0;}
int ttf_font_open_builtin(void) {assert(media_lost>0);builtin_opens++;sd_font=font_due=false;return 0;}
int read_pico_sd_get_info(read_pico_sd_info_t*i) {i->present=media_test?present_steps[step]:font_due;i->mounted=media_test?mounted_steps[step]:font_due;return media_test&&!i->mounted?ESP_ERR_INVALID_STATE:0;}
void read_pico_sd_start_probe(void) {probes++;}
static void lost(app_ctx_t*c) {(void)c;media_lost++;loss_step=step;}
static void ready(app_ctx_t*c) {(void)c;media_ready++;}
EpdRect ui_content_refresh_area(void) {return (EpdRect){0,0,684,1000};}
const app_desc_t* app_home_page(void) {return &first;}
const app_desc_t* app_at(int i) {return i==0?&first:i==1?&second:NULL;}
int app_index_of(const app_desc_t* app) {return app==&first?0:app==&second?1:-1;}
int ui_key_hit_test(uint16_t x,uint16_t y) {return y>=1300 && x<480?x/160:-1;}
bool ui_menu_handle_hit_test(uint16_t x,uint16_t y) {return x>600 && y>1100 && y<1300;}
int ui_menu_leaf_for_app(const app_desc_t*a) {return a==&second?1:0;}
int ui_menu_leaf_count(void) {return 2;}
void ui_draw_menu_page(uint8_t*f,const app_desc_t*a,int l) {(void)f;menu_background=a;last_menu_leaf=l;menus++;}
int ui_menu_hit_test(uint16_t x,uint16_t y,int l) {(void)l;if(x==550&&y==500)return UI_MENU_HIT_NEXT;return x<500&&y>=100&&y<300?(y-100)/100:-1;}
bool ui_menu_row_rect(int l,int r,EpdRect*out) {(void)l;*out=(EpdRect){0,r*100+100,500,100};return r>=0&&r<2;}
void ui_draw_menu_row_pressed(uint8_t*f,const app_desc_t*a,int l,int r,bool p) {(void)f;(void)a;(void)l;(void)r;if(p)highlights++;else restores++;}
static void enter(app_ctx_t*c) {(void)c;enters++;}
static void leave(app_ctx_t*c) {(void)c;exits++;}
static void render(app_ctx_t*c,uint8_t*f) {(void)c;(void)f;}
// 模拟 app_book render() 的旋转自设（app_book.c:3252 的 want 计算）：阅读视图
// 处于横屏态时把物理旋转设回横屏。守门用例二用它断言取消路径确实重走 render()
// 而不是帧缓冲区域恢复式（后者旋转停留竖屏而像素是横屏内容，触摸映射错乱）。
// Mimics app_book render()'s rotation self-set (the want computation at
// app_book.c:3252): a landscape reading view sets the panel rotation back to
// landscape. Gatekeeper 2 uses it to assert the cancel path really repaints
// through render() instead of restoring a framebuffer region (which would
// leave rotation portrait over landscape pixels and misalign touch).
static int landscape_renders;
static void landscape_render(app_ctx_t*c,uint8_t*f) {(void)c;(void)f;++landscape_renders;epd_set_rotation(EPD_ROT_LANDSCAPE);}
static void home_enter(app_ctx_t*c) {c->leaf=0;enters++;home_enters++;}
static void home_render(app_ctx_t*c,uint8_t*f) {(void)f;rendered_leaf=c->leaf;home_renders++;}
static app_redraw_t touch(app_ctx_t*c,const cst836u_touch_t*t) {
    (void)t;touch_calls++;assert(c->pressed&&c->consumed);
    if(request_on_touch)c->request_app=&second;
    if(menu_on_touch)c->request_menu=true;
    if(home_on_touch)c->request_return=true;
    return response;
}
static app_redraw_t gesture(app_ctx_t*c,const ui_gesture_event_t*e) {
    events[e->type]++;
    if(e->type==UI_GESTURE_CANCEL&&cancel_clobber) {c->request_app=NULL;c->request_menu=true;c->request_return=false;}
    if(e->type==UI_GESTURE_PRESS&&request_on_touch)c->request_app=&second;
    if(e->type==UI_GESTURE_PRESS&&home_on_touch)c->request_return=true;
    return response;
}
static app_redraw_t key(app_ctx_t*c,int k) {keys[k]++;if(home_on_key)c->request_return=true;return APP_REDRAW_NONE;}
// 记录 TAP 的逻辑坐标，供横屏旋转公式做端到端断言。
// Record TAP logical coords for end-to-end landscape rotation assertions.
static int tap_x0, tap_y0, tap_x, tap_y;
static app_redraw_t coord_gesture(app_ctx_t*c,const ui_gesture_event_t*e) {
    (void)c;
    events[e->type]++;
    if(e->type==UI_GESTURE_TAP){tap_x0=e->x0;tap_y0=e->y0;tap_x=e->x;tap_y=e->y;}
    return APP_REDRAW_NONE;
}
static app_redraw_t tick(app_ctx_t*c) {
    ticks++;tick_consumed[step]=c->consumed;
    if (shake_activity && step == 0) c->user_activity = true;
    if(request_on_tick)c->request_app=&second;
    if(menu_on_tick)c->request_menu=true;
    if(home_on_tick){c->request_return=true;home_on_tick=false;}
    return full_tick ? APP_REDRAW_FULL : APP_REDRAW_NONE;
}
static app_redraw_t consume_power(app_ctx_t*c) {(void)c;lock_due=false;return APP_REDRAW_DONE;}
static void reset(void) {
    power_open_draws=power_controls_draws=power_final_draws=shutdowns=restarts=0;power_trigger_step=-1;staged_shutdown=false;
    ble_enabled=saved_wifi=upload_busy=click_enabled=click_active=false;
    transfer_status=(read_pico_transfer_status_t){0};
    quick_draws=quick_gl=quick_du=wifi_starts=wifi_stops=ble_resets=click_presses=click_restores=0;
    main_fast=main_water=false;shelf_exit_arms=0;idle_minutes=0;idle_locks=before_locks=lock_feedback_calls=lock_badges=wake_clean_requests=0;feedback_enabled=false;online_busy=false;
    nav_arms=nav_draws=nav_full_arms=nav_area_draws=0;nav_armed=NULL;
    main_source_visible=main_target_visible=true;
    media_test=sd_font=saved_sd_font=false;media_lost=media_ready=builtin_opens=font_opens=probes=0;loss_step=-1;
    memset(mounted_steps,0,sizeof(mounted_steps));memset(present_steps,0,sizeof(present_steps));
    long_keys=0;
    landscape_renders=0;host_rotation=EPD_ROT_INVERTED_PORTRAIT;tap_x0=tap_y0=tap_x=tap_y=-1;
    home_on_touch=home_on_tick=home_on_key=start_second=false;
    home_enters=home_renders=0;rendered_leaf=-1;last_menu_leaf=-1;menu_background=NULL;
    full_tick=lock_due=font_due=shake_activity=false;time_offset=0;time_step=10000;du_areas=gl_areas=0;
    memset(samples,0,sizeof(samples));memset(keys,0,sizeof(keys));memset(events,0,sizeof(events));memset(tick_consumed,0,sizeof(tick_consumed));
    sample_count=step=ticks=enters=exits=touch_calls=menus=highlights=restores=fulls=mode=0;
    request_on_touch=request_on_tick=menu_on_tick=menu_on_touch=cancel_clobber=false;response=APP_REDRAW_NONE;
    first=(app_desc_t){.title="first",.render=render,.on_enter=enter,.on_exit=leave,.on_touch=touch,.on_tick=tick,.on_key=key};
    second=(app_desc_t){.title="second",.render=render,.on_enter=enter};
}
static void add(int x,int y,int count,int error) {samples[sample_count++]=(sample_t){x,y,count,error};}
static void run(void) {app_loop_config_t cfg={.first_app=start_second?&second:&first};if(!setjmp(done))app_loop_run(&cfg);}
static app_redraw_t consumed_return_tick(app_ctx_t*c) { if(step==0){c->leaf=7;c->request_app=&second;}else c->request_return=true;return APP_REDRAW_NONE; }
static app_redraw_t return_tick(app_ctx_t*c) { c->request_return=true; return APP_REDRAW_NONE; }
static app_redraw_t origin_tick(app_ctx_t*c) { c->leaf=7; if(step==0 && request_on_tick)c->request_app=&second; return APP_REDRAW_NONE; }
static void home_case(void) {
    reset();start_second=true;
    second=first;second.title="transfer";
    first.on_enter=home_enter;first.render=home_render;first.on_tick=NULL;first.on_touch=NULL;
}
static bool product_menu_disabled(app_ctx_t *ctx) {(void)ctx;return false;}
static void before_lock(app_ctx_t *ctx) {(void)ctx;++before_locks;}
static bool main_first(app_ctx_t* ctx) {(void)ctx;return main_source_visible;}
static bool main_second(app_ctx_t* ctx) {(void)ctx;return main_target_visible;}
static void enter_subview(app_ctx_t* ctx) {enter(ctx);main_target_visible=false;}
static void enable_main_tabs(void) {main_fast=true;first.main_page_visible=main_first;second.main_page_visible=main_second;}
static app_redraw_t nav_return_tick(app_ctx_t* ctx) {
    if(step==0)ctx->request_app=&second;
    else if(step==1)ctx->request_return=true;
    return APP_REDRAW_NONE;
}
static app_redraw_t main_late_tick(app_ctx_t* ctx) {(void)ctx;return step == 1 ? response : APP_REDRAW_NONE;}
int main(void) {
    // 第一帧报点立即切页，尚未抬手也能响应；同次停留和释放不重复。
    // The first contact switches before lift; its subsequent hold and release never repeat navigation.
    for(int policy=0;policy<3;++policy) {
        reset();enable_main_tabs();main_fast=policy==1;main_water=policy==2;
        add(200,UI_NAV_TOUCH_TOP,1,0);run();assert(nav_draws==1&&enters==2&&!touch_calls);
        reset();enable_main_tabs();main_fast=policy==1;main_water=policy==2;
        add(200,1080,1,0);add(232,1092,1,0);add(232,1092,1,0);add(0,0,0,0);run();
        assert(nav_draws==1&&enters==2&&!touch_calls);
        reset();enable_main_tabs();main_fast=policy==1;main_water=policy==2;
        add(200,1080,1,0);add(0,0,0,0);add(85,1080,1,0);add(0,0,0,0);run();
        assert(nav_draws==2&&enters==3&&!touch_calls);
    }
    reset();enable_main_tabs();add(85,1080,1,0);add(0,0,0,0);run();assert(!nav_draws&&enters==1);
    reset();enable_main_tabs();first.on_gesture=gesture;add(200,1080,2,0);add(0,0,0,0);run();assert(!nav_draws);
    reset();enable_main_tabs();add(200,1080,1,-1);add(0,0,0,0);run();assert(!nav_draws);
    reset();enable_main_tabs();main_source_visible=false;first.on_gesture=gesture;
    add(200,1080,1,0);run();assert(!nav_draws&&enters==1);
    puts("PASS: root tabs act on first contact in all modes, enlarged top edge, one-shot holds, rapid taps, active-tab no-op, subview/multitouch/read-error guards");
    reset();add(10,1141,1,0);
    app_ctx_t loading={.tp=(void*)1};
    assert(app_poll_main_navigation(&loading)&&loading.request_app==&first&&loading.user_activity);
    loading.request_app=NULL; samples[0]=(sample_t){.x=200,.y=1141,.count=1};
    assert(!app_poll_main_navigation(&loading)); // current shelf, not a cancellation
    samples[0]=(sample_t){.x=10,.y=1141,.count=2};assert(!app_poll_main_navigation(&loading));
    samples[0]=(sample_t){.x=10,.y=1141,.count=1,.error=-1};assert(!app_poll_main_navigation(&loading));

    reset();add(100,400,1,0);add(110,400,1,0);add(0,0,0,0);run();
    assert(touch_calls==1&&ticks==3&&!tick_consumed[0]);
    reset();response=APP_REDRAW_AREA;add(100,400,1,0);run();assert(touch_calls==1&&tick_consumed[0]);
    reset();first.on_gesture=gesture;response=APP_REDRAW_AREA;add(100,400,1,0);add(0,0,0,0);run();
    assert(!touch_calls&&events[UI_GESTURE_PRESS]==1&&events[UI_GESTURE_TAP]==1&&tick_consumed[0]);
    reset();first.on_gesture=gesture;add(100,400,1,0);add(0,0,0,9);add(100,400,1,0);add(0,0,0,0);run();
    assert(events[UI_GESTURE_PRESS]==1&&events[UI_GESTURE_CANCEL]==1&&!events[UI_GESTURE_TAP]);
    reset();request_on_touch=true;menu_on_touch=true;add(100,400,1,0);run();assert(enters==2&&exits==1&&ticks==0&&!menus);
    reset();request_on_tick=true;menu_on_tick=true;add(0,0,0,0);run();assert(enters==2&&exits==1&&ticks==1&&!menus);
    reset();first.on_gesture=gesture;request_on_touch=true;cancel_clobber=true;add(100,400,1,0);run();
    assert(enters==2&&events[UI_GESTURE_CANCEL]==1&&!menus);
    reset();menu_on_tick=true;add(0,0,0,0);add(0,0,0,0);run();assert(menus==1&&ticks==1);
    reset();first.owns_keys=true;for(int k=0;k<3;k++){add(k*160+80,1500,1,0);add(0,0,0,0);}run();
    assert(keys[0]==1&&keys[1]==1&&keys[2]==1&&!menus&&fulls==1);
    reset();add(240,1500,1,0);add(0,0,0,0);add(400,1500,1,0);run();assert(!keys[1]&&!keys[2]&&menus==1&&fulls==2);
    // 横屏条纹区例外：实体键条纹点（y>=UI_KEY_AREA_TOP）原样放行不旋转，屏内点按
    // lx=ty、ly=h-1-tx 旋转并钳位。条纹区坐标在两种方向下语义相同、旋转必须跳过，
    // 是这台设备触屏模型里最容易踩的坑——回归：曾把 (80,1500) 旋成屏内 (1215,603)，
    // 三个实体键与菜单全部失效。
    // Landscape strip exception: key-strip points (y>=UI_KEY_AREA_TOP) pass
    // through unrotated; on-screen points rotate by lx=ty, ly=h-1-tx and clamp.
    // Strip coords mean the same in both orientations, so rotation must skip
    // them - the easiest trap in this device's touch model (regression:
    // (80,1500) once rotated into on-screen (1215,603), killing all three
    // keys and the menu).
    reset();host_rotation=EPD_ROT_LANDSCAPE;first.owns_keys=true;
    for(int k=0;k<3;k++){add(k*160+80,1500,1,0);add(0,0,0,0);}run();
    assert(keys[0]==1&&keys[1]==1&&keys[2]==1&&!menus&&fulls==1);
    reset();host_rotation=EPD_ROT_LANDSCAPE;add(240,1500,1,0);add(0,0,0,0);add(400,1500,1,0);run();
    assert(!keys[1]&&!keys[2]&&menus==1&&fulls==2);
    // 分界行本身（y==UI_KEY_AREA_TOP）仍算条纹点不旋转；低一行（y=1299）是屏内点，
    // 旋转成 (1215,603) 后按不到键——分界点上下各验一侧，防止边界被悄悄挪动。
    // The split row itself (y==UI_KEY_AREA_TOP) stays unrotated; one row below
    // (y=1299) is on-screen, rotates to (1215,603) and misses every key - both
    // sides of the split are checked so the boundary cannot silently shift.
    reset();host_rotation=EPD_ROT_LANDSCAPE;first.owns_keys=true;add(80,1300,1,0);add(0,0,0,0);run();
    assert(keys[0]==1);
    reset();host_rotation=EPD_ROT_LANDSCAPE;first.owns_keys=true;add(80,1299,1,0);add(0,0,0,0);run();
    assert(!keys[0]);
    // 屏内点按 lx=ty、ly=h-1-tx 旋转：原生 (100,500) -> 逻辑 (500,583)；越界钳位：
    // 原生 (50,1216) 的 lx=1216 钳到 1215，ly=683-50=633。
    // On-screen points rotate by lx=ty, ly=h-1-tx: native (100,500) -> logical
    // (500,583); clamping: native (50,1216) has lx=1216 clamped to 1215, ly=633.
    reset();host_rotation=EPD_ROT_LANDSCAPE;first.on_gesture=coord_gesture;
    add(100,500,1,0);add(110,500,1,0);add(0,0,0,0);run();
    assert(events[UI_GESTURE_TAP]==1&&tap_x0==500&&tap_y0==583&&tap_x==500&&tap_y==573);
    reset();host_rotation=EPD_ROT_LANDSCAPE;first.on_gesture=coord_gesture;
    add(50,1216,1,0);add(60,1216,1,0);add(0,0,0,0);run();
    assert(events[UI_GESTURE_TAP]==1&&tap_x0==1215&&tap_y0==633&&tap_x==1215&&tap_y==623);
    // 竖屏（默认方向）整条映射恒等：同一个屏内点坐标原样透传。
    // Portrait (the default orientation) is an identity passthrough.
    reset();first.on_gesture=coord_gesture;
    add(100,500,1,0);add(110,500,1,0);add(0,0,0,0);run();
    assert(events[UI_GESTURE_TAP]==1&&tap_x0==100&&tap_y0==500&&tap_x==110&&tap_y==500);
    puts("landscape touch remap: key-strip points pass through unrotated (split row included), on-screen points rotate by lx=ty/ly=h-1-tx with clamping, portrait stays identical passed");
    reset();add(650,1150,1,0);add(0,0,0,0);add(100,220,1,0);add(0,0,0,0);run();
    assert(highlights==1&&restores==1&&enters==2&&exits==1);
    reset();add(650,1150,1,0);add(0,0,0,0);add(100,220,1,0);add(550,220,1,0);add(100,220,1,0);add(0,0,0,0);run();
    assert(highlights==1&&restores==1&&enters==1&&!exits);
    reset();add(650,1150,1,0);add(0,0,0,0);add(100,220,1,0);add(0,0,0,9);run();assert(restores==1&&enters==1);
    reset();first.on_gesture=gesture;full_tick=true;add(100,400,1,0);add(0,0,0,0);run();
    assert(events[UI_GESTURE_CANCEL]==1&&!events[UI_GESTURE_TAP]);
    reset();first.on_gesture=gesture;font_due=true;time_offset=4000000;add(100,400,1,0);add(0,0,0,0);run();
    // 开机首帧前已加载保存字体，因此首个手势无需再被延迟字体切换取消。
    // The saved font is ready before the first frame, so the first gesture is no longer cancelled.
    assert(events[UI_GESTURE_PRESS]==1&&events[UI_GESTURE_TAP]==1&&font_opens==1);
    reset();first.owns_keys=true;add(650,1150,1,0);add(0,0,0,0);add(240,1500,1,0);add(0,0,0,0);add(400,1500,1,0);run();
    assert(!keys[1]&&!keys[2]&&fulls==2&&menus==2);
    reset();add(650,1150,1,0);add(0,0,0,0);
    for(int i=0;i<3;i++){add(100,220,1,0);add(550,220,1,0);add(0,0,0,0);}run();
    assert(du_areas==6&&gl_areas==1);
    reset();time_step=1000000;add(650,1150,1,0);add(0,0,0,0);add(100,220,1,0);add(550,220,1,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(du_areas==2&&gl_areas==1);
    reset();first.on_gesture=gesture;time_step=1000000;lock_due=true;
    add(100,400,1,0);add(100,400,1,0);add(100,400,1,0);add(0,0,0,0);run();
    assert(!lock_due&&events[UI_GESTURE_CANCEL]==1&&!events[UI_GESTURE_TAP]&&tick_consumed[2]);
    reset();first.owns_keys=true;first.on_key_long=long_key;time_step=200000;
    add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);add(0,0,0,0);run();
    assert(keys[1]==1&&long_keys==1&&menus==1);
    reset();first.owns_keys=true;first.on_key_long=long_key;time_step=200000;
    add(240,1500,1,0);add(0,0,0,0);run();assert(keys[1]==1&&!long_keys&&!menus);
    reset();first.owns_keys=true;first.on_key_long=long_key;time_step=300000;
    add(240,1500,1,0);add(80,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);run();assert(!long_keys);
    reset();first.owns_keys=true;first.on_key_long=long_key;time_step=300000;
    add(240,1500,1,0);add(0,0,0,9);add(240,1500,1,0);add(240,1500,1,0);run();assert(!long_keys);
    reset();first.owns_keys=true;first.on_key_long=long_key;time_step=1000000;lock_due=true;
    add(0,0,0,0);add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);add(0,0,0,0);run();assert(!lock_due&&!long_keys);
    home_case();home_on_tick=true;add(0,0,0,0);add(0,0,0,0);run();
    assert(exits==0&&home_enters==0&&menus==1&&last_menu_leaf==1&&menu_background==&second);
    assert(home_renders==0&&ticks==1&&fulls==1&&mode==1);
    home_case();home_on_tick=true;add(0,0,0,0);add(400,1500,1,0);add(0,0,0,0);run();
    assert(exits==0&&home_enters==0&&menus==1&&home_renders==0);
    home_case();home_on_touch=true;request_on_touch=true;menu_on_touch=true;add(100,400,1,0);run();
    assert(exits==0&&home_enters==0&&menus==1&&home_renders==0&&ticks==0);
    home_case();second.on_gesture=gesture;home_on_touch=true;cancel_clobber=true;response=APP_REDRAW_PAGE;add(100,400,1,0);run();
    assert(exits==0&&home_enters==0&&menus==1&&events[UI_GESTURE_CANCEL]==1&&home_renders==0);
    home_case();second.owns_keys=true;second.on_key_long=long_key;home_on_key=true;time_step=300000;
    add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);run();
    assert(exits==0&&home_enters==0&&menus==1&&!long_keys&&ticks==0);
    reset();first.render=home_render;first.on_tick=origin_tick;second.on_tick=return_tick;second.on_exit=leave;
    add(0,0,0,0);add(650,1150,1,0);add(0,0,0,0);add(550,500,1,0);add(0,0,0,0);
    add(100,220,1,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(exits==2&&enters==3&&last_menu_leaf==1&&menu_background==&first&&home_renders==1);
    reset();first.render=home_render;first.on_tick=origin_tick;second.on_tick=return_tick;second.on_exit=leave;
    add(0,0,0,0);add(650,1150,1,0);add(0,0,0,0);add(550,500,1,0);add(0,0,0,0);
    add(100,220,1,0);add(0,0,0,0);add(0,0,0,0);add(400,1500,1,0);run();
    assert(exits==2&&enters==3&&rendered_leaf==7&&home_renders==2);
    reset();request_on_tick=true;first.render=home_render;first.on_tick=origin_tick;second.on_tick=return_tick;second.on_exit=leave;
    add(0,0,0,0);add(0,0,0,0);run();
    assert(exits==2&&enters==3&&!menus&&home_renders==2&&rendered_leaf==7);
    reset();first.render=home_render;first.on_enter=home_enter;first.on_tick=origin_tick;second.on_tick=return_tick;second.on_exit=leave;
    add(0,0,0,0);add(650,1150,1,0);add(0,0,0,0);add(550,500,1,0);add(0,0,0,0);
    add(100,220,1,0);add(0,0,0,0);add(0,0,0,0);
    add(100,220,1,0);add(0,0,0,0);add(0,0,0,0);add(400,1500,1,0);run();
    assert(exits==4&&enters==5&&last_menu_leaf==1&&menu_background==&first&&home_renders==2&&rendered_leaf==7);
    reset();start_second=true;second.on_tick=origin_tick;second.on_exit=leave;second.on_enter=home_enter;second.render=home_render;first.on_tick=return_tick;
    add(0,0,0,0);add(650,1150,1,0);add(0,0,0,0);add(100,120,1,0);add(0,0,0,0);add(0,0,0,0);add(400,1500,1,0);run();
    assert(exits==2&&enters==3&&last_menu_leaf==1&&menu_background==&second&&home_renders==2&&rendered_leaf==7);
    reset();first.on_tick=consumed_return_tick;second.on_tick=return_tick;second.on_exit=leave;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(exits==2&&enters==3&&menus==1&&last_menu_leaf==0&&menu_background==&first);
    // 拔卡先关资源再回退字体，取消本轮按键，持续无卡不重复通知。
    // Removal closes consumers before fallback, drops the current key and notifies only once.
    reset();media_test=sd_font=true;mounted_steps[0]=true;time_step=600000;first.on_media_lost=lost;
    add(0,0,0,0);add(80,1500,1,0);add(0,0,0,0);run();
    assert(media_lost==1&&loss_step==1&&builtin_opens==1&&!keys[0]&&fulls==2);
    // 后台页即使被菜单遮盖也释放资源，仍保留菜单视图。
    // A menu-covered page still releases resources and retains the menu view.
    reset();media_test=sd_font=true;mounted_steps[0]=true;time_step=600000;first.on_media_lost=lost;
    add(650,1150,1,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(media_lost==1&&builtin_opens==1&&menus==2&&ticks==0);
    // 延迟挂载后拔卡同样被捕获，使用内置字体无需再打开一次。
    // Removal after a late mount is detected without reopening an already built-in font.
    reset();media_test=true;mounted_steps[1]=true;time_step=600000;first.on_media_lost=lost;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(media_lost==1&&loss_step==2&&!builtin_opens);
    // 首次无卡时后插卡应触发探测，挂载完成只通知页面一次。
    // First insertion after an empty boot probes, then notifies the active page once.
    reset();media_test=true;time_step=600000;first.on_media_ready=ready;
    present_steps[1]=present_steps[2]=present_steps[3]=true;
    mounted_steps[2]=mounted_steps[3]=true;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(probes>=1&&media_ready==1&&!media_lost);
    // 手势和长按在拔卡边界取消，不能把旧按住状态变成新的动作。
    // Removal cancels gestures and holds without turning an old contact into another action.
    reset();media_test=true;mounted_steps[0]=true;time_step=600000;first.on_media_lost=lost;first.on_gesture=gesture;
    add(100,400,1,0);add(100,400,1,0);add(0,0,0,0);run();
    assert(media_lost==1&&events[UI_GESTURE_CANCEL]==1&&!events[UI_GESTURE_TAP]);
    reset();media_test=true;mounted_steps[0]=true;time_step=600000;first.on_media_lost=lost;first.owns_keys=true;first.on_key_long=long_key;
    add(240,1500,1,0);add(240,1500,1,0);add(240,1500,1,0);run();
    assert(media_lost==1&&keys[1]==1&&!long_keys);
    // 失效后不自动探测；显式恢复的挂载可重新加载保存字体并再次检测拔卡。
    // No automatic probing after invalidation; an explicit remount restores saved-font loading and loss detection.
    reset();media_test=sd_font=saved_sd_font=true;mounted_steps[0]=true;time_step=4000000;first.on_media_lost=lost;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(media_lost==1&&builtin_opens==1&&!probes&&!font_opens);
    reset();media_test=sd_font=saved_sd_font=true;mounted_steps[0]=mounted_steps[2]=true;time_step=4000000;first.on_media_lost=lost;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(media_lost==2&&builtin_opens==2&&!probes&&font_opens==1);
    reset();idle_minutes=1;time_step=60000000;first.on_before_lock=before_lock;
    add(0,0,0,0);add(0,0,0,0);run();
    assert(idle_locks==1&&before_locks==1); // Real scheduler flushes before automatic lock.
    // 横屏下自动锁：prepare_lock 先把物理旋转复位竖屏，锁屏/壁纸资产才不画在
    // 横屏物理屏上（真机 bug：长按电源睡眠后壁纸横置）；本就竖屏时保持不动。
    // Auto lock under landscape: prepare_lock resets the panel to portrait first,
    // so lock/wallpaper assets never land on a landscape frame (hardware bug:
    // sideways sleep wallpaper after a long power press); portrait stays put.
    reset();idle_minutes=1;time_step=60000000;host_rotation=EPD_ROT_LANDSCAPE;first.on_before_lock=before_lock;
    add(0,0,0,0);add(0,0,0,0);run();
    assert(idle_locks==1&&before_locks==1&&host_rotation==EPD_ROT_INVERTED_PORTRAIT);
    reset();idle_minutes=1;time_step=60000000;first.on_before_lock=before_lock;
    add(0,0,0,0);add(0,0,0,0);run();
    assert(idle_locks==1&&host_rotation==EPD_ROT_INVERTED_PORTRAIT);
    reset();idle_minutes=1;time_step=60000000;shake_activity=true;
    add(0,0,0,0);run();assert(!idle_locks);
    reset();idle_minutes=1;time_step=60000000;shake_activity=true;
    add(0,0,0,0);add(0,0,0,0);run();assert(idle_locks==1&&wake_clean_requests==1); /* Activity is consumed once, not latched. */
    reset();idle_minutes=0;time_step=600000000;
    add(0,0,0,0);add(0,0,0,0);run();assert(!idle_locks);
    reset();idle_minutes=1;time_step=600000000;online_busy=true;
    add(0,0,0,0);add(0,0,0,0);run();assert(!idle_locks);
    reset();idle_minutes=1;time_step=600000000;first.holds_pmu=true;
    add(0,0,0,0);add(0,0,0,0);run();assert(!idle_locks);
    reset();idle_minutes=1;time_step=30000000;
    add(100,400,1,0);add(100,400,1,0);add(0,0,0,0);add(0,0,0,0);run();assert(!idle_locks);
    reset();first.owns_keys=true;first.on_gesture=gesture;first.menu_handle_enabled=product_menu_disabled;
    add(636,1150,1,0);add(0,0,0,0);run();
    assert(!menus&&events[UI_GESTURE_PRESS]==1&&events[UI_GESTURE_TAP]==1);
    // 首帧保持全刷；切页一次完成，按住、松开、空闲都不安排第二次刷新。
    // Retain the initial full update; finish navigation once without another draw on hold, release or idle.
    reset();enable_main_tabs();add(0,0,0,0);run();assert(!nav_arms&&!nav_draws&&!nav_full_arms);
    reset();enable_main_tabs();request_on_touch=true;add(100,400,1,0);add(100,400,1,0);run();
    assert(nav_arms==1&&nav_draws==1&&!nav_armed);
    reset();enable_main_tabs();request_on_touch=true;add(100,400,1,0);add(100,400,1,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(nav_arms==1&&nav_draws==1&&!nav_armed&&!nav_full_arms);
    reset();enable_main_tabs();first.on_tick=nav_return_tick;add(0,0,0,0);add(0,0,0,0);second.on_tick=return_tick;add(0,0,0,0);run();
    assert(nav_arms==2&&nav_draws==2&&!nav_armed&&shelf_exit_arms==1);
    // 目录延后完成重绘和同页局部更新也获得固定底栏许可，没有额外 idle 推屏。
    // Deferred directory completion and local repaint receive frozen-navigation permission without an idle redraw.
    reset();enable_main_tabs();first.on_tick=nav_return_tick;second.on_tick=main_late_tick;
    response=APP_REDRAW_PAGE;add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(nav_draws==2&&nav_arms==2&&!nav_armed&&!nav_full_arms);
    reset();enable_main_tabs();first.on_tick=nav_return_tick;second.on_tick=main_late_tick;
    response=APP_REDRAW_AREA;add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(nav_draws==1&&nav_area_draws==1&&nav_arms==2&&!nav_armed&&!nav_full_arms);
    // 子视图、非主页面保留普通出口，显式全刷优先。
    // Subviews and non-main pages retain ordinary output; explicit full refresh has priority.
    reset();enable_main_tabs();main_source_visible=false;request_on_touch=true;add(100,400,1,0);add(0,0,0,0);run();assert(!nav_arms&&!nav_draws);
    reset();enable_main_tabs();second.on_enter=enter_subview;request_on_touch=true;add(100,400,1,0);add(0,0,0,0);run();assert(!nav_arms&&!nav_draws);
    reset();enable_main_tabs();second.main_page_visible=NULL;request_on_touch=true;add(100,400,1,0);add(0,0,0,0);run();assert(!nav_arms&&!nav_draws);
    reset();enable_main_tabs();second.enter_full=true;request_on_touch=true;add(100,400,1,0);add(0,0,0,0);run();assert(nav_arms==1&&nav_full_arms==1&&!nav_draws);
    reset();enable_main_tabs();main_fast=false;request_on_touch=true;add(100,400,1,0);add(0,0,0,0);run();
    assert(nav_arms==1&&nav_draws==1&&!shelf_exit_arms&&!nav_full_arms);
    reset();enable_main_tabs();main_fast=false;first.on_tick=nav_return_tick;second.on_tick=main_late_tick;
    response=APP_REDRAW_AREA;add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(nav_draws==1&&nav_area_draws==1&&nav_arms==2&&!nav_full_arms);
    reset();enable_main_tabs();lock_due=true;time_step=3000000;add(0,0,0,0);add(0,0,0,0);run();
    assert(idle_locks==1&&!nav_arms&&!nav_draws);
    puts("main-tab scheduler: single immediate draw, no hold/release/idle redraw, rapid return, root/subview boundaries and full-output priority passed");
    // 跑真实边缘识别和主循环，验证不会漏给底层页面或按住重复动作。
    // Run the real edge recognizer and loop to reject underlay leakage and repeated hold actions.
    reset();first.on_gesture=gesture;
    add(300,20,1,0);add(300,110,1,0);add(300,120,1,0);add(0,0,0,0);
    add(300,500,1,0);add(0,0,0,0);add(100,400,1,0);add(0,0,0,0);run();
    assert(quick_draws==1&&quick_gl==2&&!quick_du);
    assert(events[UI_GESTURE_PRESS]==1&&events[UI_GESTURE_TAP]==1);
    reset();main_fast=true;saved_wifi=true;
    add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);
    add(85,156,1,0);add(85,156,1,0);add(0,0,0,0);
    add(85,156,1,0);add(0,0,0,0);run();
    assert(wifi_starts==1&&wifi_stops==1&&quick_gl==3&&!quick_du&&!touch_calls);
    reset();upload_busy=true;transfer_status=(read_pico_transfer_status_t){.state=1,.mode=READ_PICO_TRANSFER_MODE_STA};
    add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);add(85,156,1,0);add(0,0,0,0);run();
    assert(!wifi_starts&&!wifi_stops&&transfer_status.state==1);
    reset();add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);add(85,156,1,0);add(0,0,0,0);run();
    assert(!wifi_starts&&!wifi_stops&&!touch_calls);
    reset();add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);
    add(256,156,1,0);add(0,0,0,0);add(256,156,1,0);add(0,0,0,0);run();
    assert(!ble_enabled&&ble_resets==1&&!touch_calls);
    reset();first.on_before_lock=before_lock;
    add(300,20,1,0);add(300,110,1,0);add(0,0,0,0);
    add(427,156,1,0);add(0,0,0,0);add(598,156,1,0);add(0,0,0,0);run();
    assert(idle_locks==1&&before_locks==1&&fulls>=2&&!touch_calls);
    for(int policy=0;policy<3;++policy){
        reset();main_fast=policy==1;main_water=policy==2;first.on_gesture=gesture;click_enabled=true;
        add(85,1141,1,0);add(85,1141,1,0);add(0,0,0,0);run();
        assert(click_presses==1&&click_restores==1&&!click_active&&ticks==1&&du_areas==2&&!gl_areas);
        reset();enable_main_tabs();main_fast=policy==1;main_water=policy==2;click_enabled=true;request_on_touch=true;
        add(200,1141,1,0);add(0,0,0,0);run();
        assert(click_presses==1&&click_restores==1&&!click_active&&du_areas==2&&!gl_areas&&nav_draws==1);
    }
    reset();click_enabled=true;add(85,1141,1,0);add(85,1141,2,0);add(0,0,0,0);run();
    assert(click_presses==1&&click_restores==1&&!click_active);
    reset();click_enabled=true;add(85,1141,1,0);add(0,0,0,1);add(0,0,0,0);run();
    assert(click_presses==1&&click_restores==1&&!click_active);
    puts("quick layer/click scheduler: modal ownership, local BW/gray, one saved-network connect/disconnect, upload/no-credential guards, BLE toggle, full/lock and release/multitouch/read-error restoration passed");
    // 反馈必须早于页面保存/后台收尾；下一页与自动休眠不显示主动锁屏徽标。
    // Acknowledgement precedes saves/joins; consumed page turns and idle sleep omit it.
    for(int policy=0;policy<3;++policy){
        reset();main_fast=policy==1;main_water=policy==2;feedback_enabled=true;
        first.on_before_lock=before_lock;lock_due=true;time_step=3000000;
        add(0,0,0,0);add(0,0,0,0);run();
        assert(lock_badges==1&&lock_feedback_calls==1&&before_locks==1&&idle_locks==1);
        reset();feedback_enabled=true;first.on_power_short=consume_power;lock_due=true;time_step=3000000;
        add(0,0,0,0);add(0,0,0,0);run();assert(!lock_badges&&!lock_feedback_calls&&!before_locks&&!idle_locks);
        reset();feedback_enabled=true;idle_minutes=1;time_step=60000000;first.on_before_lock=before_lock;
        add(0,0,0,0);add(0,0,0,0);run();assert(idle_locks==1&&before_locks==1&&!lock_feedback_calls&&!lock_badges);
    }
    puts("PASS: manual lock acknowledgement precedes page save/reader joins in all three modes; consumed power turns and auto sleep omit the badge");
    reset();power_trigger_step=1;time_step=3000000;
    add(0,0,0,0);add(0,0,0,0);add(128,374,1,0);add(240,374,1,0);add(556,374,1,0);add(0,0,0,0);run();
    assert(power_open_draws==1&&power_controls_draws==2&&shutdowns==1&&!restarts&&power_final_draws==1&&!touch_calls&&ticks==1&&du_areas==2&&gl_areas==1);
    reset();power_trigger_step=1;time_step=3000000;staged_shutdown=true;
    add(0,0,0,0);add(0,0,0,0);add(128,374,1,0);add(556,374,1,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(idle_locks==1&&wake_clean_requests==1&&!shutdowns&&!restarts&&!power_final_draws);
    reset();power_trigger_step=1;time_step=3000000;
    add(0,0,0,0);add(0,0,0,0);add(238,960,1,0);add(0,0,0,0);run();
    assert(restarts==1&&!shutdowns&&power_final_draws==1);
    reset();enable_main_tabs();power_trigger_step=1;time_step=3000000;
    add(0,0,0,0);add(0,0,0,0);add(446,960,1,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(!shutdowns&&!restarts&&!power_final_draws&&nav_arms==0&&mode==1);
    for(int fault=0;fault<2;++fault){
        reset();power_trigger_step=1;time_step=3000000;
        add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);add(128,374,1,0);add(556,374,1,0);add(556,374,fault?2:0,fault?0:1);add(0,0,0,0);run();
        assert(!shutdowns&&!restarts&&power_controls_draws==2&&!touch_calls);
    }
    reset();power_trigger_step=1;time_step=3000000;
    add(0,0,0,0);add(128,374,1,0);add(556,374,1,0);add(0,0,0,0);run();
    assert(power_open_draws==1&&!power_controls_draws&&!shutdowns&&!restarts);
    // 守门用例一：横屏长按电源打开关机对话框，断言对话框分支已把物理旋转复位
    // 竖屏——对话框是第 7 个系统画面，并入「系统画面一律竖屏」不变式（真机
    // round-2 缺陷：横屏下对话框没有转过来、取消按钮踩空无法显示）。
    // Gatekeeper 1: a landscape long power press opens the shutdown dialog with
    // the panel rotation already reset to portrait - the dialog is the 7th
    // system screen and joins the "system frames are portrait" invariant
    // (hardware round-2 defect: the dialog did not turn, and the cancel button
    // could not be shown or hit).
    reset();host_rotation=EPD_ROT_LANDSCAPE;power_trigger_step=1;time_step=3000000;
    add(0,0,0,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(power_open_draws==1&&gl_areas==1&&host_rotation==EPD_ROT_INVERTED_PORTRAIT);
    // 守门用例二（用户裁决生死线）：横屏打开对话框 → 取消 → 返回阅读。断言取消
    // 路径重走当前视图 render()（其内部按视图状态自设旋转回横屏并整页重绘），
    // 而不是帧缓冲区域恢复式——后者会让旋转停留竖屏而像素是横屏内容、触摸映射
    // 错乱，等于修一个 bug 埋一个 bug。启动首帧与取消各调一次 render，故计 2。
    // Gatekeeper 2 (the ruling's lifeline): landscape → dialog → cancel → back
    // to reading. Assert the cancel path repaints through the current view's
    // render(), which self-sets rotation back to landscape per view state and
    // fully repaints - not a framebuffer region restore, which would leave
    // rotation portrait over landscape pixels and misalign touch, trading one
    // bug for another. Startup frame and cancel each call render once, hence 2.
    reset();host_rotation=EPD_ROT_LANDSCAPE;first.render=landscape_render;power_trigger_step=1;time_step=3000000;
    add(0,0,0,0);add(0,0,0,0);add(446,960,1,0);add(0,0,0,0);add(0,0,0,0);run();
    assert(power_open_draws==1&&landscape_renders==2&&host_rotation==EPD_ROT_LANDSCAPE&&!shutdowns&&!restarts);
    puts("PASS: real loop and power recognizer: DU-only contact, release commits off/staged sleep/restart once, cancel restores full navigation, touch/multitouch errors and entry-held suppression");
    puts("PASS: shutdown dialog gatekeepers: landscape long-press resets the dialog branch to portrait, and cancel repaints through render() to restore landscape");
    puts("app_loop: 53 scheduler scenarios passed, including automatic lock, save callback, input reset, busy guards and the shutdown dialog rotation gatekeepers");
    return 0;
}

/* 中文：硬件 BLE 与联网任务替身。/ English: Hardware BLE and online worker shims. */
void ble_pt_sync(bool enabled) { (void)enabled; }
void ble_pt_poll(void) {}
bool pico_online_busy(void) { return online_busy; }

/* 中文：push_sink 调度面替身。宿主默认未武装：翻页器连接、睡眠调度与对话框
   逻辑因此不受推流影响；武装页的渲染路径由 transfer_ui 回归单独覆盖。
   English: scheduling shims for push_sink. The host defaults to unarmed, so the
   page-turner link, sleep scheduling and dialog logic stay unaffected; the arm
   page's rendering paths are covered by the transfer_ui regression alone. */
bool push_sink_armed(void) { return false; }
void push_sink_poll(void) {}

uint8_t app_settings_auto_lock_minutes(void){return idle_minutes;}
uint32_t ble_pt_input_serial(void){return 0;}


void crash_note_log(void){}

void app_time_service_poll(bool idle) { (void)idle; }

void ui_avatar_invalidate(void) {}
bool book_cover_jobs_quiesce(unsigned timeout){(void)timeout;return true;}
void book_lock_collage_invalidate(void) {}

bool ble_pt_connected(void) { return false; }
