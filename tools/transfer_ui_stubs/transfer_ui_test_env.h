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
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
typedef struct { int x,y,width,height; } EpdRect;
typedef enum { APP_REDRAW_NONE, APP_REDRAW_AREA, APP_REDRAW_PAGE, APP_REDRAW_FULL, APP_REDRAW_DONE } app_redraw_t;
struct app_desc;
typedef struct { uint8_t* fb; void* hl; bool consumed; int64_t now_ms; const struct app_desc* request_app; bool request_return; } app_ctx_t;
typedef enum { UI_GESTURE_PRESS, UI_GESTURE_TAP, UI_GESTURE_CANCEL, UI_GESTURE_LONG_PRESS } ui_gesture_type_t;
typedef struct { ui_gesture_type_t type; uint16_t x,y,x0,y0; } ui_gesture_event_t;
typedef struct app_desc { const char *title,*detail; bool enter_full; void(*on_enter)(app_ctx_t*); void(*on_exit)(app_ctx_t*); void(*on_media_lost)(app_ctx_t*); void(*render)(app_ctx_t*,uint8_t*); bool(*present)(app_ctx_t*,app_redraw_t); app_redraw_t(*on_tick)(app_ctx_t*); app_redraw_t(*on_gesture)(app_ctx_t*,const ui_gesture_event_t*); } app_desc_t;
enum EpdDrawMode { MODE_DU,MODE_GL16,MODE_GC16 };
#define UI_MARGIN 40
#define UI_CONTENT_TOP 176
#define UI_BTN_H 84
#define UI_BTN_RADIUS 14
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
#define UI_GRAY_LIGHT 0xcc
#define UI_GRAY_BLACK 0
#define MALLOC_CAP_8BIT 1
#define MALLOC_CAP_INTERNAL 2
static const int E0470_WAVEFORM = 0;
static const int E0470_FOLLOW_WAVEFORM = 1;
#define ESP_LOGI(tag,...) ((void)(tag))
static inline const char* esp_err_to_name(int e) { (void)e; return "error"; }
static inline int ui_content_width(void) { return 604; }
static inline EpdRect ui_row_rect(int i,int n,int y,int h) { int w=(604-(n-1)*12)/n; return (EpdRect){40+i*(w+12),y,w,h}; }
static inline EpdRect ui_bar_rect(int i,int n) { int w=(508-(n-1)*12)/n; return (EpdRect){40+i*(w+12),1096,w,96}; }
static inline bool ui_rect_hit(EpdRect r,int x,int y) { return x>=r.x&&y>=r.y&&x<r.x+r.width&&y<r.y+r.height; }
static inline EpdRect ui_rect_union(EpdRect a,EpdRect b) { int x=a.x<b.x?a.x:b.x,y=a.y<b.y?a.y:b.y; int x2=a.x+a.width>b.x+b.width?a.x+a.width:b.x+b.width; int y2=a.y+a.height>b.y+b.height?a.y+a.height:b.y+b.height; return(EpdRect){x,y,x2-x,y2-y}; }
static inline void ui_clear_page(uint8_t* f) {(void)f;}
static inline void ui_clear_rect_fast(uint8_t* f,EpdRect r) {(void)f;(void)r;}
static inline void ui_draw_header(uint8_t* f,...) {(void)f;}
static inline void ui_draw_button(uint8_t* f,...) {(void)f;}
static inline void ui_draw_pressed_round_rect(uint8_t* f,...) {(void)f;}
static inline void ui_draw_round_rect(uint8_t* f,...) {(void)f;}
static inline void ui_draw_menu_handle(uint8_t* f,...) {(void)f;}
static inline void ui_hairline(uint8_t* f,...) {(void)f;}
static inline void ui_text(uint8_t* f,...) {(void)f;}
static inline void ui_text_vc(uint8_t* f,...) {(void)f;}
static inline void epd_fill_rect(EpdRect r,...) {(void)r;}
static inline int ttf_text_width_px(int px,const char* s) {return (int)strlen(s)*px/2;}
static bool test_font_suspended;
static inline int ttf_font_suspend_sd(bool suspend) {test_font_suspended=suspend;return ESP_OK;}
static inline void display_set_bulk_io(bool b) {(void)b;}
static inline int update_display_area_with(void* p,...) {(void)p;return 0;}
static inline int update_display_white(void* p) {(void)p;return 0;}
static inline int update_display_full(void* p) {(void)p;return 0;}
static inline void guard_draw_result(void* p,int e) {(void)p;(void)e;}
static inline size_t heap_caps_get_free_size(int cap) {(void)cap;return 1000000;}
typedef struct {char path[160];bool is_flash;} book_store_root_t;
static inline uint64_t book_store_free_bytes(void* r) {(void)r;return 1000000;}
static inline int book_store_upload_root(book_store_root_t* r) {strcpy(r->path,"/flash/books");return 0;}
static inline size_t book_store_file_limit(void* r) {(void)r;return 1000000;}
static int test_store_changes;
static inline void book_store_notify_changed(void) { ++test_store_changes; }
static inline int app_count(void) {return 0;}
static inline const app_desc_t* app_at(int i) {(void)i;return NULL;}
typedef enum {READ_PICO_TRANSFER_MODE_AP,READ_PICO_TRANSFER_MODE_STA} read_pico_transfer_mode_t;
typedef enum {READ_PICO_TRANSFER_STOPPED,READ_PICO_TRANSFER_STARTING,READ_PICO_TRANSFER_READY,READ_PICO_TRANSFER_UPLOADING,READ_PICO_TRANSFER_ERROR} read_pico_transfer_state_t;
typedef struct {read_pico_transfer_mode_t mode;const char* root_dir;const char* font_dir;bool is_flash;size_t file_limit;uint64_t(*free_bytes_cb)(void*);void* free_bytes_ctx;int(*file_changed_cb)(const char*);} read_pico_transfer_cfg_t;
typedef struct {read_pico_transfer_mode_t mode;read_pico_transfer_state_t state;bool wifi_configured,network_ready;char ssid[33],wifi_ssid[33],url[64],cur_name[121];unsigned sta_count,done_count,changed_count;size_t cur_bytes,cur_total;int last_error;} read_pico_transfer_status_t;
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
static inline void read_pico_transfer_stop(void) { ++test_stop_count; }
static inline void read_pico_transfer_get_status(read_pico_transfer_status_t* s) {*s=test_status;}
static inline int read_pico_transfer_get_saved_wifi(char* s,bool* c) {strcpy(s,test_configured?"saved":"");*c=test_configured;return 0;}
static inline int read_pico_transfer_start(const read_pico_transfer_cfg_t* c) {if (!test_font_suspended || !c->file_changed_cb || c->file_changed_cb("/flash/books/test.txt")) return ESP_FAIL;return 0;}
static inline void read_pico_transfer_service_poll(void) {}
static inline int read_pico_transfer_save_wifi(const char* s,const char* p) {(void)p;test_save_count++;snprintf(test_saved_ssid,33,"%s",s);return test_save_error;}
static inline int read_pico_transfer_scan_wifi(read_pico_transfer_network_t* out,size_t* count) {*count=test_scan_error?0:test_scan_count;for(size_t i=0;i<*count;i++){out[i]=(read_pico_transfer_network_t){.supported=true,.requires_password=true};snprintf(out[i].ssid,33,"network%u",(unsigned)i);}if(*count)snprintf(out[0].ssid,33,"中文家庭网络");return test_scan_error;}
static int test_qr_encodes;
static bool test_qr_failure;
static char test_qr_payload[128];
static inline void ui_wifi_qr_clear(void) {test_qr_payload[0]=0;}
static inline bool ui_wifi_qr_prepare(const char* s,const char* p) {(void)p;++test_qr_encodes;snprintf(test_qr_payload,sizeof(test_qr_payload),"%s",s);return !test_qr_failure;}
static inline bool ui_wifi_qr_prepare_url(const char* s) {++test_qr_encodes;snprintf(test_qr_payload,sizeof(test_qr_payload),"%s",s);return !test_qr_failure;}
static inline void ui_wifi_qr_draw(uint8_t* fb,EpdRect r) {(void)fb;(void)r;}

static inline int read_pico_transfer_forget_wifi(void) { ++test_forget_count; if (!test_forget_error) test_configured=false; return test_forget_error; }

static inline int book_progress_forget(const char* path) {(void)path;return ESP_OK;}
