/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：配网界面的宿主依赖替身。/ English: Host dependency substitutes for provisioning UI.
 * 冻结：仅供测试。/ Frozen: Tests only.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "read_pico_transfer_network.h"
#include "../../main/assets/ui_icons.h"
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_NOT_FINISHED 0x107
#define ESP_ERR_NOT_ALLOWED 0x10E
typedef struct { int x,y,width,height; } EpdRect;
typedef enum { APP_REDRAW_NONE, APP_REDRAW_AREA, APP_REDRAW_PAGE, APP_REDRAW_FULL, APP_REDRAW_DONE } app_redraw_t;
struct app_desc;
typedef struct { bool touched; uint8_t count; uint16_t x,y; } cst836u_touch_t;
typedef struct { uint8_t* fb; void* hl; bool consumed,released; int64_t now_ms; const cst836u_touch_t* touch; const struct app_desc* request_app; bool request_return; } app_ctx_t;
typedef enum { UI_GESTURE_PRESS, UI_GESTURE_TAP, UI_GESTURE_CANCEL, UI_GESTURE_LONG_PRESS } ui_gesture_type_t;
typedef struct { ui_gesture_type_t type; uint16_t x,y,x0,y0; } ui_gesture_event_t;
typedef struct app_desc { const char *title,*detail; bool enter_full,owns_keys; bool(*menu_handle_enabled)(app_ctx_t*); void(*on_enter)(app_ctx_t*); void(*on_exit)(app_ctx_t*); void(*on_media_lost)(app_ctx_t*); void(*render)(app_ctx_t*,uint8_t*); bool(*present)(app_ctx_t*,app_redraw_t); app_redraw_t(*on_tick)(app_ctx_t*); app_redraw_t(*on_gesture)(app_ctx_t*,const ui_gesture_event_t*); app_redraw_t(*on_key)(app_ctx_t*,int); } app_desc_t;
enum EpdDrawMode { MODE_DU,MODE_GL16,MODE_GC16 };
#define UI_MARGIN 40
#define UI_CONTENT_TOP 176
#define UI_BTN_H 84
#define UI_BTN_RADIUS 14
#define UI_LONG_PRESS_MS 500
#define UI_PX_CAPTION 28
#define UI_PX_BODY 38
#define UI_PX_BTN 34
#define UI_PX_SUB 30
#define UI_PAD 16
#define UI_GAP 12
#define UI_BAR_TOP 1096
#define UI_LOCK_HEIGHT 1216
#define UI_LOCK_WIDTH 684
#define EPD_DRAW_ALIGN_LEFT 0
#define EPD_DRAW_ALIGN_CENTER 1
#define EPD_DRAW_ALIGN_RIGHT 2
#define UI_GRAY_LIGHT 0xcc
#define UI_GRAY_BLACK 0
#define UI_GRAY_WHITE 0xff
#define MALLOC_CAP_8BIT 1
#define MALLOC_CAP_INTERNAL 2
#define MALLOC_CAP_SPIRAM 4
static inline void* heap_caps_malloc(size_t n,int caps){(void)caps;return malloc(n);}
static const int E0470_WAVEFORM = 0;
static const int E0470_FOLLOW_WAVEFORM = 1;
#define ESP_LOGI(tag,...) ((void)(tag))
#define ESP_LOGW(tag,...) ((void)(tag))
static inline const char* esp_err_to_name(int e) { (void)e; return "error"; }
static inline int ui_content_width(void) { return 604; }
static inline EpdRect ui_row_rect(int i,int n,int y,int h) { int w=(604-(n-1)*12)/n; return (EpdRect){40+i*(w+12),y,w,h}; }
static inline EpdRect ui_bar_rect(int i,int n) { int w=(508-(n-1)*12)/n; return (EpdRect){40+i*(w+12),1096,w,96}; }
static inline bool ui_rect_hit(EpdRect r,int x,int y) { return x>=r.x&&y>=r.y&&x<r.x+r.width&&y<r.y+r.height; }
static inline EpdRect ui_rect_union(EpdRect a,EpdRect b) { int x=a.x<b.x?a.x:b.x,y=a.y<b.y?a.y:b.y; int x2=a.x+a.width>b.x+b.width?a.x+a.width:b.x+b.width; int y2=a.y+a.height>b.y+b.height?a.y+a.height:b.y+b.height; return(EpdRect){x,y,x2-x,y2-y}; }
static inline void ui_clear_page(uint8_t* f) {(void)f;}
static inline void ui_clear_rect_fast(uint8_t* f,EpdRect r) {(void)f;(void)r;}
// r93 起传输页入口用快速实心填充画导航条底色。/ Since r93 the transfer entry fills the nav strip with the fast fill.
static inline void ui_fill_rect_fast(EpdRect r,uint8_t gray,uint8_t* f) {(void)r;(void)gray;(void)f;}
static inline void ui_draw_header(uint8_t* f,...) {(void)f;}
static inline void ui_draw_button(uint8_t* f,...) {(void)f;}
static inline void ui_draw_icon(uint8_t* f,...) {(void)f;}
static inline void ui_draw_pressed_round_rect(uint8_t* f,...) {(void)f;}
static inline void ui_draw_round_rect(uint8_t* f,...) {(void)f;}
static inline void ui_fill_round_rect(uint8_t* f,...) {(void)f;}
static inline void ui_draw_menu_handle(uint8_t* f,...) {(void)f;}
static inline void ui_hairline(uint8_t* f,...) {(void)f;}
static inline void ui_text(uint8_t* f,...) {(void)f;}
static inline int ui_text_effective_px(int px) {return px + 2;}
static inline void ui_text_vc(uint8_t* f,...) {(void)f;}
static inline void ui_text_fixed_vc(uint8_t* f,...) {(void)f;}
static inline int ui_text_fixed_width_px(int px,const char* s) {return (int)strlen(s)*px/2;}
static inline int ui_text_fixed_context_width_px(int px,const char* s,const char* sample) {(void)sample;return ui_text_fixed_width_px(px,s);}
static inline void ui_text_fixed_context_vc(uint8_t* f,...) {(void)f;}
static inline void epd_fill_rect(EpdRect r,...) {(void)r;}
static inline void epd_draw_rect(EpdRect r,...) {(void)r;}
static inline void epd_draw_circle(int x,int y,int radius,...) {(void)x;(void)y;(void)radius;}
static inline void epd_draw_line(int x0,int y0,int x1,int y1,...) {(void)x0;(void)y0;(void)x1;(void)y1;}
static inline int ttf_text_width_px(int px,const char* s) {return (int)strlen(s)*px/2;}
static inline void display_set_bulk_io(bool b) {(void)b;}
static inline int update_display_area_diff_with(void* p,...) {(void)p;return 0;}
static inline int update_display_area_full_with(void* p,...) {(void)p;return 0;}
static inline int update_display_area_with(void* p,...) {(void)p;return 0;}
static inline int update_display_white(void* p) {(void)p;return 0;}
static inline int update_display_full(void* p) {(void)p;return 0;}
static inline void guard_draw_result(void* p,int e) {(void)p;(void)e;}
static inline size_t heap_caps_get_free_size(int cap) {(void)cap;return 1000000;}
typedef struct {char path[160];bool is_flash;} book_store_root_t;
#define BOOK_STORE_PATH_MAX 288
static inline uint64_t book_store_free_bytes(void* r) {(void)r;return 1000000;}
static inline int book_store_upload_root(book_store_root_t* r) {strcpy(r->path,"/flash/books");return 0;}
static inline size_t book_store_file_limit(void* r) {(void)r;return 1000000;}
static int test_store_changes;
static inline void book_store_notify_changed(void) { ++test_store_changes; }
static inline int app_count(void) {return 0;}
static inline const app_desc_t* app_at(int i) {(void)i;return NULL;}
typedef enum {READ_PICO_TRANSFER_MODE_AP,READ_PICO_TRANSFER_MODE_STA} read_pico_transfer_mode_t;
typedef enum {READ_PICO_TRANSFER_STOPPED,READ_PICO_TRANSFER_STARTING,READ_PICO_TRANSFER_READY,READ_PICO_TRANSFER_UPLOADING,READ_PICO_TRANSFER_ERROR} read_pico_transfer_state_t;
typedef struct {read_pico_transfer_mode_t mode;bool network_only;const char* root_dir;bool is_flash;size_t file_limit;uint64_t(*free_bytes_cb)(void*);void* free_bytes_ctx;bool(*title_get_cb)(const char*,char*,size_t);int(*title_set_cb)(const char*,const char*);int(*file_changed_cb)(const char*);void(*file_deleted_cb)(const char*);void(*file_moved_cb)(const char*,const char*,uint32_t);void(*directory_deleted_cb)(const char*);void(*directory_moved_cb)(const char*,const char*);bool(*wallpaper_set_cb)(const char*);bool(*signature_get_cb)(char*,size_t);int(*signature_set_cb)(const char*);} read_pico_transfer_cfg_t;
typedef struct {read_pico_transfer_mode_t mode;read_pico_transfer_state_t state;bool wifi_configured,network_ready;char ssid[33],wifi_ssid[33],url[64],cur_name[241];unsigned sta_count,done_count,changed_count;size_t cur_bytes,cur_total;int last_error;} read_pico_transfer_status_t;
#define READ_PICO_TRANSFER_PASSWORD "readpico"
static read_pico_transfer_status_t test_status;
static bool test_busy;
static int test_save_error;
static int test_forget_count, test_forget_error;
static bool test_configured = true;
static int test_scan_error, test_save_count;
static size_t test_scan_count = 8;
static char test_saved_ssid[33];
static inline bool read_pico_transfer_try_stop_if_idle(void) {return !test_busy;}
static int test_stop_count;
static bool test_wifi_running;
static inline void read_pico_transfer_stop(void) { ++test_stop_count; test_wifi_running=false; }
static inline void read_pico_transfer_get_status(read_pico_transfer_status_t* s) {*s=test_status;}
static inline int read_pico_transfer_get_saved_wifi(char* s,bool* c) {strcpy(s,test_configured?"saved":"");*c=test_configured;return 0;}
/* 上游 r91 的启动守卫（同一个 Wi-Fi 不允许重复启动）与我方飞行模式闸门并存：
   两者管的是不同条件，互不替代。/ Upstream r91's start guard (no double start on the
   same Wi-Fi) and our flight-mode gate coexist: they test different conditions. */
static inline int read_pico_transfer_start(const read_pico_transfer_cfg_t* c) {if (test_wifi_running) return ESP_FAIL;if (!c->network_only && (!c->file_changed_cb || !c->wallpaper_set_cb || c->file_changed_cb("/flash/books/test.txt"))) return ESP_FAIL;return 0;}
/* 飞行模式闸门：测试里默认关闭，可用 test_rf_blocked 打开以验证提示文案。
   Flight mode gate: off by default in tests; set test_rf_blocked to check the prompts. */
static bool test_rf_blocked;
static inline void read_pico_transfer_set_rf_blocked(bool b) { test_rf_blocked = b; }
static inline bool read_pico_transfer_rf_blocked(void) { return test_rf_blocked; }
static inline void read_pico_transfer_service_poll(void) {}
static inline int read_pico_transfer_save_wifi(const char* s,const char* p) {(void)p;test_save_count++;snprintf(test_saved_ssid,33,"%s",s);return test_save_error;}
static inline int read_pico_transfer_scan_wifi(read_pico_transfer_network_t* out,size_t* count) {*count=test_scan_error?0:test_scan_count;for(size_t i=0;i<*count;i++){out[i]=(read_pico_transfer_network_t){.supported=true,.requires_password=true};snprintf(out[i].ssid,33,"network%u",(unsigned)i);}if(*count)snprintf(out[0].ssid,33,"中文家庭网络");return test_scan_error;}
static int test_qr_encodes;
static bool test_qr_failure;
static char test_qr_payload[128];
static inline void ui_wifi_qr_clear(void) {test_qr_payload[0]=0;}
static inline void ttf_font_cache_clear(void) {}
static inline bool ui_wifi_qr_prepare(const char* s,const char* p) {(void)p;++test_qr_encodes;snprintf(test_qr_payload,sizeof(test_qr_payload),"%s",s);return !test_qr_failure;}
static inline bool ui_wifi_qr_prepare_url(const char* s) {++test_qr_encodes;snprintf(test_qr_payload,sizeof(test_qr_payload),"%s",s);return !test_qr_failure;}
static inline void ui_wifi_qr_draw(uint8_t* fb,EpdRect r) {(void)fb;(void)r;}

static inline int read_pico_transfer_forget_wifi(void) { ++test_forget_count; if (test_wifi_running) return ESP_FAIL; if (!test_forget_error) test_configured=false; return test_forget_error; }

// 进度清除的记录缓冲：虚拟书回归据此断言「清旧进度」。
// Recording buffer for the progress clear: the virtual-book regression asserts
// "drop stale progress" against it.
static char test_progress_forget_path[160];
static inline int book_progress_forget(const char* path) {snprintf(test_progress_forget_path,sizeof(test_progress_forget_path),"%s",path?path:"");return ESP_OK;}
typedef struct {uint32_t file_size;} book_progress_t;
static inline bool book_progress_load(const char* path,uint32_t size,book_progress_t* out) {(void)path;(void)size;(void)out;return false;}
static inline bool book_progress_save(const char* path,const book_progress_t* progress) {(void)path;(void)progress;return true;}
static inline bool book_progress_last_path(char* out,size_t cap) {if(cap)out[0]=0;return false;}
static inline bool book_progress_set_last_path(const char* path) {(void)path;return true;}

static char test_signature[96];
static bool test_signature_fail;
static inline const char *app_settings_status_signature(void){return test_signature;}
static inline void app_settings_set_status_signature(const char *text){if(!test_signature_fail)snprintf(test_signature,sizeof(test_signature),"%s",text);}

/* 中文：push_sink 武装页的宿主替身。类型与常量按真实 push_session.h/
   push_proto.h 逐项对表（见 tools/push_sink_host_test.c 的帧对表），函数替身是
   static inline，与真实头文件的外建原型互斥，故本目录另有 push_sink.h 整头遮蔽
   （include 本文件）。Step 4 起替身升级为旋钮驱动：未注入时保持「未武装、无
   来件」的惰性形态，配网/传书既有回归不受影响；武装页回归经 test_push_* 旋钮
   按序注入事件与状态快照，驱动开书/单词卡/收摊各分支。
   English: host shims for the push_sink arm page. Types and constants mirror
   push_session.h/push_proto.h item by item; the function doubles are static
   inline and therefore mutually exclusive with the real header's extern
   prototypes, which is why push_sink.h in this directory shadows the whole
   header (it includes this file). Since Step 4 the doubles are knob-driven:
   with nothing injected they keep the inert "never armed, never receives"
   shape so the existing provisioning regression is unaffected, while the
   arm-page regression injects events and state snapshots through the
   test_push_* knobs to drive the open-book, word-card and teardown branches. */
typedef enum {
    PUSH_STATE_IDLE = 0,    ///< 未武装 / not armed
    PUSH_STATE_ARMED,       ///< 已点接收，等广播起来 / receive pressed, awaiting advertising
    PUSH_STATE_WINDOW,      ///< 窗口期：广播中，等手机连接 / window open: advertising
    PUSH_STATE_CONNECTED,   ///< 手机已连接，未开始传输 / phone connected, no transfer yet
    PUSH_STATE_RECEIVING,   ///< BEGIN 已接受，正文流式中 / BEGIN accepted, body streaming
    PUSH_STATE_PROCESSING,  ///< END 已到，等 CRC 对账 / END arrived, awaiting the CRC verdict
    PUSH_STATE_COOLDOWN,    ///< 推送成功后的续推宽限 / post-push grace for a follow-up push
} push_state_t;
#define PUSH_TITLE_MAX 64u ///< 标题 UTF-8 上限（含结尾 0）/ title cap including NUL
#define PUSH_DIR "/sdcard/.readpico"
#define PUSH_PART_PATH PUSH_DIR "/push.part"
#define PUSH_TEXT_PATH PUSH_DIR "/push.txt"
enum {
    PUSH_ERR_NOT_ARMED = 1, ///< 设备未进入接收态 / the device is not receiving
    PUSH_ERR_BAD_FRAME = 2, ///< 帧结构非法 / malformed frame
    PUSH_ERR_TOO_LARGE = 3, ///< 超过 512 KB 硬上限 / beyond the 512 KB hard cap
    PUSH_ERR_CHECKSUM = 4,  ///< CRC32 不符 / CRC32 mismatch
    PUSH_ERR_NO_CARD = 5,   ///< TF 卡不可用 / card unavailable
    PUSH_ERR_LOW_BATTERY = 6, ///< 电量不足且未充电 / low battery, not charging
    PUSH_ERR_BUSY = 7,      ///< 上一条推送未收官 / the previous push is still open
    PUSH_ERR_FETCH = 8,     ///< URL 抓取失败（M1 不触发）/ URL fetch failed (not in M1)
    PUSH_ERR_NO_DICT = 9,   ///< 词典不可用，单词卡取消 / no dictionary, word card cancelled
};

/* 中文：push_sink 事件旋钮：宿主回归按序注入一次性事件与状态快照，驱动武装页
   的分支。/ English: push_sink event knobs: the host regression injects
   one-shot events and state snapshots in sequence to drive the arm page's
   branches. */
static bool test_push_armed;
static push_state_t test_push_state = PUSH_STATE_IDLE;
static char test_push_adv[64];
static uint32_t test_push_left_ms, test_push_recv, test_push_total;
static bool test_push_closed, test_push_start_failed, test_push_open, test_push_word_card, test_push_err;
static char test_push_title[64], test_push_word[64];
static char test_push_close_why[48];
static uint8_t test_push_type;
static int test_push_err_code;
static int test_push_arm_calls, test_push_arm_window, test_push_cancel_calls;

static inline esp_err_t push_sink_arm(uint16_t token, uint32_t window_ms) {
    (void)token;
    ++test_push_arm_calls;
    test_push_arm_window = (int)window_ms;
    test_push_armed = true;
    test_push_close_why[0] = 0; // 对齐真实组件：新回合清原因 / mirror: a fresh round clears the reason
    return ESP_OK;
}
static inline void push_sink_cancel_why(const char *why) {
    ++test_push_cancel_calls;
    if (why && why[0]) snprintf(test_push_close_why, sizeof(test_push_close_why), "%s", why);
    else test_push_close_why[0] = 0;
    // 对齐真实组件：cancel 从武装态出发，状态机走 ADV_OFF，补一个 closed。
    // Mirror the real glue: a cancel from an armed state walks the machine
    // through ADV_OFF and posts one closed event.
    if (test_push_armed) test_push_closed = true;
    test_push_armed = false;
}
static inline void push_sink_cancel(void) {
    push_sink_cancel_why("已退出接收");
}
static inline const char *push_sink_last_close_why(void) { return test_push_close_why[0] ? test_push_close_why : NULL; }
static inline bool push_sink_armed(void) { return test_push_armed; }
static inline push_state_t push_sink_state(void) { return test_push_state; }
static inline const char *push_sink_adv_name(void) { return test_push_adv; }
static inline uint32_t push_sink_window_left_ms(void) { return test_push_left_ms; }
static inline void push_sink_progress(uint32_t *received, uint32_t *total) {
    if (received) *received = test_push_recv;
    if (total) *total = test_push_total;
}
static inline bool push_sink_take_closed(void) {
    if (!test_push_closed) return false;
    test_push_closed = false;
    return true;
}
static inline bool push_sink_take_start_failed(void) {
    if (!test_push_start_failed) return false;
    test_push_start_failed = false;
    return true;
}
static inline bool push_sink_take_open(char *title, size_t cap, uint8_t *type) {
    if (!test_push_open) return false;
    test_push_open = false;
    if (title && cap) snprintf(title, cap, "%s", test_push_title);
    if (type) *type = test_push_type;
    return true;
}
static inline bool push_sink_take_word_card(char *word, size_t cap) {
    if (!test_push_word_card) return false;
    test_push_word_card = false;
    if (word && cap) snprintf(word, cap, "%s", test_push_word);
    return true;
}
static inline bool push_sink_take_err(int *code) {
    if (!test_push_err) return false;
    test_push_err = false;
    if (code) *code = test_push_err_code;
    return true;
}
