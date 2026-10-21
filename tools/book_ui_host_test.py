"""Compile actual shelf helpers for deterministic ordering and failure regressions.

SPDX-FileCopyrightText: 2026 mindreset
SPDX-License-Identifier: Apache-2.0
中文：抽取真实静态助手，避免宿主链接硬件页面依赖。
English: Extract real static helpers without linking hardware page dependencies.
"""
from pathlib import Path
import subprocess
import tempfile

source = Path(__file__).resolve().parents[1] / "main/apps/app_book.c"
layout_source = Path(__file__).resolve().parents[1] / "main/book/book_layout.c"

def function(name, path=source):
    text = path.read_text(encoding="utf-8")
    import re
    # 注释中的引号/花括号不是 C 语法，保持位置后再匹配函数边界。
    # Quotes/braces in comments are not C syntax; mask comments while preserving positions.
    tokens = r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    scan = re.sub(tokens, lambda m: ' ' * len(m[0]) if m[0].startswith(('//', '/*')) else m[0], text)
    found = re.search(r"^(?:static )?[^\n]+\b" + name + r"\([^;{}]*?\)\s*\{", text, re.M)
    assert found, name
    start, at, depth, quote, escape = found.start(), found.end(), 1, None, False
    while depth:
        c = scan[at]
        if quote:
            if escape: escape = False
            elif c == "\\": escape = True
            elif c == quote: quote = None
        elif c in "\"'": quote = c
        elif c == "{": depth += 1
        elif c == "}": depth -= 1
        at += 1
    return text[start:at]

unit = r'''
typedef enum { UI_GESTURE_SWIPE_L, UI_GESTURE_SWIPE_R, UI_GESTURE_SWIPE_U, UI_GESTURE_SWIPE_D, UI_GESTURE_TAP, UI_GESTURE_CANCEL } ui_gesture_type_t;
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdatomic.h>
#include "ui_text_edit.h"
#include "book_shelf_categories.h"
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NVS_NOT_FOUND -2
#define ESP_ERR_INVALID_STATE -3
#define ESP_ERR_NOT_FINISHED 7
typedef struct {uint8_t usage,mods;} ble_pt_event_t;
typedef struct {uint8_t len;uint8_t bytes[16];} ble_pt_frame_t;
typedef struct {ble_pt_frame_t frame;bool pressed;} ble_pt_raw_t;
typedef enum {BLE_PT_ACTION_NONE,BLE_PT_ACTION_PREV,BLE_PT_ACTION_NEXT} ble_pt_action_t;
static ble_pt_event_t test_remote_keys[4];
static ble_pt_raw_t test_remote_raw[4];
static int test_remote_key_count,test_remote_raw_count;
static bool ble_pt_pop_key(ble_pt_event_t *event){if(!test_remote_key_count)return false;*event=test_remote_keys[--test_remote_key_count];return true;}
static bool ble_pt_pop_raw(ble_pt_raw_t *event){if(!test_remote_raw_count)return false;*event=test_remote_raw[--test_remote_raw_count];return true;}
static ble_pt_action_t ble_pt_action_for_frame(const ble_pt_frame_t *frame){
    if(!frame||!frame->len)return BLE_PT_ACTION_NONE;
    return frame->bytes[0]==1?BLE_PT_ACTION_PREV:frame->bytes[0]==2?BLE_PT_ACTION_NEXT:BLE_PT_ACTION_NONE;
}
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define ESP_LOGI(...) ((void)0)
typedef int esp_err_t;
typedef struct app_desc {int unused;} app_desc_t;
typedef struct {bool touched;} test_touch_t;
typedef struct {int leaf;bool request_menu,consumed;int64_t now_ms;uint8_t *fb;const app_desc_t *request_app;test_touch_t *touch;} app_ctx_t;
typedef enum {APP_REDRAW_PAGE,APP_REDRAW_AREA,APP_REDRAW_NONE,APP_REDRAW_FULL} app_redraw_t;
typedef struct {int x,y,width,height;} EpdRect;
static EpdRect ui_bar_rect(int i,int count){int width=(508-(count-1)*12)/count;return(EpdRect){40+i*(width+12),1096,width,96};}
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
typedef struct {char name[256],author[128],path[288];uint32_t size;uint16_t chapter;bool is_flash,has_progress;uint8_t pct;uint32_t recent;bool selected,removed,search_match,favorite;} shelf_entry_t;
#define BOOK_ROWS 13
#define BOOK_GRID_ROWS 9
#define BOOK_BULK_ROWS 6
#define BOOKMARK_MAX 24
#define BOOK_SCAN_DEPTH_LIMIT 12
typedef struct {DIR* dir;char path[288];} shelf_scan_frame_t;
typedef struct {uint16_t chapter,reserved;uint32_t byte_off,saved_s;} reader_bookmark_entry_t;
typedef struct {uint32_t magic,file_size;uint16_t count,reserved;char path[288];reader_bookmark_entry_t entries[BOOKMARK_MAX];} reader_bookmarks_t;
#define BOOK_STORE_PATH_MAX 288
#define UI_BTN_H 84
#define UI_GAP 12
static EpdRect ui_row_rect(int i,int count,int y,int height){EpdRect r=ui_bar_rect(i,count);r.y=y;r.height=height;return r;}
static char s_query[65],s_search_draft[65],s_batch_message[128];
static ui_text_edit_t s_search_input;
static void ui_keyboard_begin(ui_text_edit_t *e,bool ascii){(void)e;(void)ascii;}
static void ui_keyboard_end(void){}
typedef enum {BATCH_DELETE,BATCH_CLEAR,BATCH_UNSHELF} batch_kind_t;
static bool s_batch_confirm;
static batch_kind_t s_batch_kind;
static bool read_pico_search_match(const char* name,const char* query){return !*query||strstr(name,query)!=NULL;}
static int book_chapter_count(void){return 1;}
static size_t book_navigation_count(void){return (size_t)book_chapter_count();}
#define BOOK_TOC_ROWS 7
static int book_toc_pages(size_t chapters){return chapters?1+(int)((chapters-1)/BOOK_TOC_ROWS):1;}
static int s_filter;
static bool s_recent_sort;
static int test_shelf_style;
static int app_settings_shelf_style(void){return test_shelf_style;}
static bool app_settings_shelf_recent_sort(void){return s_recent_sort;}
static void app_settings_set_shelf_recent_sort(bool value){s_recent_sort=value;}
typedef int nvs_handle_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define SHELF_HIDDEN_NS "rp_shelf"
typedef struct {char key[11],path[BOOK_STORE_PATH_MAX];} test_hidden_t;
static test_hidden_t test_hidden[16];
static int test_hidden_count,test_hidden_error;
static char test_favorite_key[11];
static int nvs_open(const char* ns,int mode,nvs_handle_t* out){(void)mode;*out=!strcmp(ns,SHELF_HIDDEN_NS)?2:1;return ESP_OK;}
static void nvs_close(nvs_handle_t h){(void)h;}
// r93 起书架要跟着进度修订号和封面批次走；本测试只管 UI 分支，这两个外部协作件按源码初值记账。
// Since r93 the shelf tracks a progress revision and a cover batch; this test only exercises UI
// branches, so these two collaborators are stubbed with the source's initial values.
static uint32_t s_progress_revision=UINT32_MAX;
// 真机上 book_progress.c 的原子计数从 0 起步，所以首轮必然与 UINT32_MAX 的缓存副本不等、强制重扫。
// On device the atomic counter in book_progress.c starts at 0, so the first pass can never equal
// the UINT32_MAX cache copy and always forces a rescan.
static uint32_t test_progress_revision;
static unsigned s_cover_generation;
static uint32_t book_progress_revision(void){return test_progress_revision;}
static void book_cover_jobs_cancel(unsigned generation){(void)generation;s_cover_generation=0;}
static void book_lock_collage_invalidate(void){}
static int nvs_get_u8(nvs_handle_t h,const char* key,uint8_t* out){if(h==1&&!strcmp(key,test_favorite_key)){*out=1;return ESP_OK;}return ESP_ERR_NVS_NOT_FOUND;}
static int nvs_get_str(nvs_handle_t h,const char* key,char* out,size_t* len){
    assert(h==2);
    for(int i=0;i<test_hidden_count;i++)if(!strcmp(key,test_hidden[i].key)){
        assert(*len>strlen(test_hidden[i].path));strcpy(out,test_hidden[i].path);*len=strlen(out)+1;return ESP_OK;
    }
    return ESP_ERR_NVS_NOT_FOUND;
}
static int nvs_set_str(nvs_handle_t h,const char* key,const char* path){
    assert(h==2);if(test_hidden_error)return test_hidden_error;
    int i;for(i=0;i<test_hidden_count;i++)if(!strcmp(key,test_hidden[i].key))break;
    if(i==test_hidden_count){assert(i<16);++test_hidden_count;}
    strcpy(test_hidden[i].key,key);strcpy(test_hidden[i].path,path);return ESP_OK;
}
static int nvs_erase_key(nvs_handle_t h,const char* key){
    assert(h==2);if(test_hidden_error)return test_hidden_error;
    for(int i=0;i<test_hidden_count;i++)if(!strcmp(key,test_hidden[i].key)){
        test_hidden[i]=test_hidden[--test_hidden_count];return ESP_OK;
    }
    return ESP_ERR_NVS_NOT_FOUND;
}
static int nvs_commit(nvs_handle_t h){assert(h==2);return ESP_OK;}
static shelf_entry_t* s_shelf;
static size_t s_shelf_capacity;
static int s_count,s_visible_count;
static char s_message[128],s_shelf_warning[128],s_storage[128];
static bool s_pending_invalidated,test_oom,test_degraded;
static bool pico_boot_asset_allowed(const char *path){(void)path;return true;}
static unsigned s_store_revision;
typedef struct {char path[288];bool is_flash;} book_store_root_t;
typedef struct {uint32_t file_size;uint16_t chapter;uint32_t byte_off;uint8_t px,pct;uint32_t last_open_s;} book_progress_t;
typedef bool (*book_progress_visit_fn)(const char*,const book_progress_t*,void*);
typedef struct {char path[BOOK_STORE_PATH_MAX];book_progress_t value;} test_progress_t;
static test_progress_t test_progress[8];
static int test_progress_count;
static char test_recent_path[BOOK_STORE_PATH_MAX];
typedef struct {nvs_handle_t favorites,hidden;bool truncated;} shelf_backfill_t;
static esp_err_t book_progress_list(book_progress_visit_fn visit,void* ctx){
    for(int i=0;i<test_progress_count;i++)if(!visit(test_progress[i].path,&test_progress[i].value,ctx))break;
    return ESP_OK;
}
typedef struct book_progress_watch {char path[288];atomic_bool invalidated;struct book_progress_watch* next;} book_progress_watch_t;
static book_progress_watch_t* test_watches;
static book_progress_watch_t* book_progress_watch_create(const char* path){book_progress_watch_t* w=calloc(1,sizeof(*w));assert(w);strcpy(w->path,path);atomic_init(&w->invalidated,false);w->next=test_watches;test_watches=w;return w;}
static bool book_progress_watch_invalidated(const book_progress_watch_t* w){return atomic_load(&w->invalidated);}
static void book_progress_watch_destroy(book_progress_watch_t* w){book_progress_watch_t** p=&test_watches;while(*p!=w)p=&(*p)->next;*p=w->next;free(w);}
#define BOOK_STORE_ROOT_MAX 3
static book_store_root_t test_roots[BOOK_STORE_ROOT_MAX];
static const char *app_settings_books_dir(void){return test_roots[0].path;}
static int test_root_count=1;
static void* heap_caps_realloc(void* p,size_t n,int caps){(void)caps;return test_oom?NULL:realloc(p,n);}
static void* heap_caps_malloc(size_t n,int caps){(void)caps;return malloc(n);}
static void* heap_caps_calloc(size_t count,size_t n,int caps){(void)caps;return test_oom?NULL:calloc(count,n);}
static bool book_store_roots_degraded(void){return test_degraded;}
static uint64_t book_store_free_bytes(const book_store_root_t* root){(void)root;return 1000000;}
static int book_store_roots(book_store_root_t out[BOOK_STORE_ROOT_MAX],int* n){*n=test_root_count;memcpy(out,test_roots,sizeof(test_roots));return 0;}
static bool book_progress_load(const char* p,uint32_t n,book_progress_t* out){
    for(int i=0;i<test_progress_count;i++)if(!strcmp(p,test_progress[i].path)&&n==test_progress[i].value.file_size){*out=test_progress[i].value;return true;}
    *out=(book_progress_t){0};return false;
}
static bool book_progress_last_path(char* out,size_t cap){if(!test_recent_path[0])return false;snprintf(out,cap,"%s",test_recent_path);return true;}
static int book_epub_metadata(const char* path,char* title,size_t tcap,char* author,size_t acap){(void)path;snprintf(title,tcap,"原书名");snprintf(author,acap,"作者");return ESP_OK;}
static int book_epub_metadata_cached(const char* path,char* title,size_t tcap,char* author,size_t acap){return book_epub_metadata(path,title,tcap,author,acap);}
static bool book_title_get(const char* path,char* out,size_t cap){(void)path;(void)out;(void)cap;return false;}
static void book_title_clean_import(char* title){(void)title;}
static bool book_title_from_path(const char* path,char* out,size_t cap){const char* name=strrchr(path,'/');name=name?name+1:path;size_t n=strlen(name);const char* dot=strrchr(name,'.');if(dot)n=(size_t)(dot-name);if(n>=cap)n=cap-1;memcpy(out,name,n);out[n]=0;return n>0;}
static void clean_filename(char* out,size_t cap,const char* name){size_t n=strlen(name);if(n>=cap)n=cap-1;memcpy(out,name,n);out[n]=0;}
typedef struct pending_progress {char path[288];book_progress_t value;bool dirty,progress_saved;book_progress_watch_t* watch;struct pending_progress* next;} pending_progress_t;
static pending_progress_t* s_pending;
typedef struct delete_retry {shelf_entry_t entry;struct delete_retry* next;} delete_retry_t;
static delete_retry_t* s_delete_retries;
static char s_latest_path[288],test_last_path[288];
static bool s_save_failed;
static char s_path[288],s_requested_open[288];
static bool s_requested_open_home,s_reader_return_home,s_requested_fullscreen;
static int test_open_calls;
static char* s_text;
static int s_unsaved,s_px=48,s_margin=36,s_line_spacing=130;
static uint32_t s_file_size=1000;
static size_t s_chapter,s_page;
static bool s_book_fonts_applied;
static bool app_settings_book_fonts(void){return false;}
static void book_set_embedded_fonts(bool enabled){(void)enabled;}
#define BOOK_KIND_EPUB 1
static int book_kind(void){return 0;}
static bool load_chapter(app_ctx_t *ctx,size_t chapter,size_t offset,bool last_page){
    (void)ctx;(void)chapter;(void)offset;(void)last_page;return true;
}
static size_t s_jump_offset=SIZE_MAX,s_jump_page;
static int test_save_error,test_last_error,test_save_calls,test_last_calls;
static size_t book_layout_page_count(void){return 3;}
static size_t book_layout_page_start_offset(size_t page){return page*100;}
static unsigned percent(size_t page){return (unsigned)page*20;}
static int book_progress_save(const char* p,const book_progress_t* value){(void)p;(void)value;test_save_calls++;return test_save_error;}
static int book_progress_set_last_path(const char* p){test_last_calls++;if(!test_last_error)snprintf(test_last_path,sizeof(test_last_path),"%s",p);return test_last_error;}
static void save_progress(void);
static void invalidate_prep(void){}
static void invalidate_covers(void){}
static shelf_entry_t s_managed;
static bool s_delete_confirm,s_file_removed,s_clear_confirm;
static char s_manage_message[128];
typedef enum {SHELF,SETTINGS,READING,TOC,MANAGE,BULK,IMPORT,SEARCH,EDIT} book_view_t;
static book_view_t s_view,s_search_parent;
static bool test_draw_locked,test_system_face=true;
static void lock_draw(void){assert(!test_draw_locked);test_draw_locked=true;}
static void unlock_draw(void){assert(test_draw_locked);test_draw_locked=false;}
static bool app_font_activate_system(void){assert(test_draw_locked);test_system_face=true;return true;}
static bool app_font_activate_reading(void){assert(test_draw_locked);test_system_face=false;return true;}
static bool s_toc_jump_open,s_toc_jump_drag;
static int s_toc_jump_percent;
typedef enum {READER_PANEL_NONE,READER_PANEL_TOOLS,READER_PANEL_DICT} reader_panel_t;
static reader_panel_t s_reader_panel;
static int s_reader_slider=-1;
static bool s_reader_slider_endpoint;
static int s_reader_preview_px,s_reader_preview_margin,s_reader_preview_line,s_reader_preview_para,s_reader_preview_tracking;
static bool s_requested_manage;
static int s_presented_view;
static uint8_t* s_editor_cover;
static void editor_start(void){}
static app_redraw_t editor_save(app_ctx_t* ctx){(void)ctx;return APP_REDRAW_PAGE;}
static void editor_backspace(void){}
static app_redraw_t editor_paint(app_ctx_t* ctx,bool field){(void)ctx;(void)field;return APP_REDRAW_AREA;}
static int test_nav_target=-1;
static void ui_nav_request(app_ctx_t* ctx,int tab){(void)ctx;test_nav_target=tab;}
static void app_files_request_folder(int folder){(void)folder;}
#define UI_KEY_1 1
#define UI_KEY_2 2
#define UI_KEY_3 3
static int s_pressed_control;
static bool s_shelf_feedback_pending,s_shelf_page_pending,s_cover_batch_present_pending;
static bool s_scan_pending,s_toolbar;
static int s_mode,test_cover_prepares,test_shelf_renders;
static EpdRect s_area;
#define MODE_GL16 1
static void prepare_covers(app_ctx_t *ctx){(void)ctx;++test_cover_prepares;}
static void render(app_ctx_t *ctx,uint8_t *fb){(void)ctx;(void)fb;++test_shelf_renders;}
static bool s_resume_pending,s_reader_cleanup,s_shake_enabled;
static bool s_reader_fullscreen,test_reader_immersive,test_hold_refresh,test_hide_images,test_power_turn;
static bool test_images_visible=true;
static int test_reflow_failures,test_reflows,test_image_preparations;
static EpdRect test_reflow_rect;
static size_t s_text_len=4,s_block_count;
typedef struct {int image;unsigned image_width,image_height;} blk_t;
static blk_t* s_blocks;
static char **s_images;
static size_t s_image_count;
static unsigned test_image_probes;
static int book_chapter_image_dimensions(size_t chapter,const char *name,unsigned *width,unsigned *height){
    (void)chapter;assert(!test_draw_locked);assert(name);++test_image_probes;*width=60;*height=15;return ESP_OK;
}
static void vTaskDelay(unsigned ticks){(void)ticks;}
static void release_page_images(void){}
static char s_reader_notice[96];static int64_t s_reader_notice_until;
static int64_t esp_timer_get_time(void){return 1000000;}
static void prepare_inline_image(void){test_image_preparations++;}
// 词典替身：点译在本用例里不参与，翻页会关闭卡片，这里给空实现即可。
// Dictionary shim: tap-to-lookup is out of scope here; turns only close the card, so no-ops suffice.
static void dict_popup_reset(void){}
static bool dict_ready(void){return true;}
static bool dict_warm(void){return true;}
static void dict_close(void){}
static bool book_layout_build_blocks(const char* text,size_t len,const void* blocks,size_t count,EpdRect rect,int px){
    (void)text;(void)len;(void)blocks;(void)count;(void)px;assert(test_draw_locked);test_reflows++;test_reflow_rect=rect;
    if(test_reflow_failures){--test_reflow_failures;return false;}return true;
}
static size_t book_layout_page_for_offset(size_t off){return off/100;}
/* P0-1 视觉页地图身份桩：单栏下视觉页恒等于逻辑页，与生产退化路径一致；真实地图
   由 tools/reader_page_map_host_test.py 覆盖。/ P0-1 visual page map identity stub:
   in single column a visual page equals its logical page, matching the production
   degenerate path; the real map is covered by tools/reader_page_map_host_test.py. */
static size_t reader_visual_count(void){return book_layout_page_count();}
static size_t reader_visual_index(size_t logical){return logical;}
static size_t reader_visual_start(size_t visual){return visual;}
static size_t reader_visual_for_offset(size_t off){return book_layout_page_for_offset(off);}
// 归因：我方 kind=3 是横屏阅读开关，apply_reader_option 委托 toggle_reader_landscape()
// （真机侧重分页与方向切换，宿主层只记录次数）。/ Attribution: our kind 3 is the
// landscape toggle, delegated to toggle_reader_landscape() (real relayout and rotation
// live on device; the host just records the calls).
/* 章节题头刷新：r92 的 apply_reader_layout 会调它；本用例不验证题头几何，空实现顶掉。
   r92's apply_reader_layout calls it; heading geometry is out of scope here. */
static void refresh_chapter_lead(void){}
static bool test_reader_landscape;
static app_redraw_t toggle_reader_landscape(app_ctx_t *ctx){(void)ctx;test_reader_landscape=!test_reader_landscape;return APP_REDRAW_PAGE;}
static bool app_settings_reader_power_turn(void){return test_power_turn;}
static int test_two_column;
bool app_settings_reader_two_column(void){return test_two_column!=0;}
void app_settings_set_reader_two_column(bool on){test_two_column=on?1:0;}
/* 第 4 步起被抽取函数经 reader_relayout 单出口建版——恒等转发桥接桩（单栏语义）。
   From step 4 the extracted functions build through reader_relayout — identity bridge
   (single-column semantics). */
static bool reader_relayout(EpdRect body){
 return book_layout_build_blocks(s_text,s_text_len,s_blocks,s_block_count,body,s_px);
}
static void app_settings_set_reader_power_turn(bool on){test_power_turn=on;}
static void app_settings_set_reader_immersive(bool on){test_reader_immersive=on;}
static void app_settings_set_reader_hold_refresh(bool on){test_hold_refresh=on;}
static void app_settings_set_reader_hide_images(bool on){test_hide_images=on;}
static bool s_reader_footer_pending,s_reader_image_refresh_pending,s_water_turn_pending;
static int s_turns;
static bool s_bookmark_edit,s_bookmark_delete_confirm,s_bookmark_delete_error;
static uint32_t s_bookmark_selected;
static int64_t s_stats_activity_ms;
static int s_du_count;
static int64_t s_size_settle_ms,s_poll_ms;
static void* s_prep_task,*s_prep_done,*s_draw_lock,*s_next_fb;
static void ensure_prep(void){}
static int app_settings_book_px(void){return 48;}
static int test_saved_margin=36;
static int app_settings_book_margin(void){return test_saved_margin;}
static void app_settings_set_book_px(uint8_t px){assert(px==48);}
static void app_settings_set_book_margin(uint8_t margin){test_saved_margin=margin;}
static void app_settings_set_book_line_spacing(uint8_t line){assert(line==130);}
static void app_settings_set_book_paragraph_spacing(uint8_t para){assert(para==50);}
static app_redraw_t apply_reader_typography(app_ctx_t* ctx,int tracking){(void)ctx;(void)tracking;return APP_REDRAW_NONE;}
static int app_settings_book_line_spacing(void){return 130;}
static int app_settings_book_paragraph_spacing(void){return 50;}
static int app_settings_book_tracking(void){return 2;}
static int app_settings_book_indent(void){return 2;}
static int app_settings_book_indent_adjust(void){return 0;}
static int app_settings_book_reading_line(void){return 0;}
static int app_settings_book_reading_line_offset(void){return 0;}
static bool app_settings_reader_immersive(void){return test_reader_immersive;}
static bool app_settings_reader_hold_refresh(void){return test_hold_refresh;}
static bool app_settings_reader_hide_images(void){return test_hide_images;}
static void book_layout_set_spacing(int line,int para){(void)line;(void)para;}
static void book_layout_set_typography(int tracking){(void)tracking;}
static void book_layout_set_first_line_indent(unsigned em){(void)em;}
static void book_layout_set_first_line_indent_adjust(int px){(void)px;}
static void book_layout_set_reading_line(int style){(void)style;}
static void book_layout_set_reading_line_offset(int offset){(void)offset;}
static void book_layout_set_images_visible(bool visible){test_images_visible=visible;}
static bool test_book_vertical;
static bool app_settings_book_vertical(void){return test_book_vertical;}
static void app_settings_set_book_vertical(bool on){test_book_vertical=on;}
static void book_layout_set_image_bleed_width(int screen_width){(void)screen_width;}
static void book_layout_set_vertical(bool vertical){test_book_vertical=vertical;}
static bool app_settings_book_shake(void){return false;}
static void read_pico_sd_start_probe(void){}
typedef struct {bool present,mounted;} read_pico_sd_info_t;
static int read_pico_sd_get_info(read_pico_sd_info_t* out){*out=(read_pico_sd_info_t){0};return 0;}
static bool s_shelf_cache_valid,s_cache_sd_present,s_cache_sd_mounted;
static uint8_t s_cache_shelf_style;
static bool app_poll_main_navigation(app_ctx_t *ctx){(void)ctx;return false;}
static void ui_reader_background_release(void){}
static void *s_backgrounds;static int s_background_count;
static void vTaskDelete(void* p){(void)p;}
static void vSemaphoreDelete(void* p){(void)p;}
static int test_turn_direction;
static app_redraw_t turn_page(app_ctx_t* ctx,int dir){(void)ctx;test_turn_direction=dir;return APP_REDRAW_NONE;}
static app_redraw_t paint_catalog_page(app_ctx_t* ctx){(void)ctx;return APP_REDRAW_AREA;}
static int test_delete_error,test_forget_error,test_notify_count,test_delete_calls;
static bool test_removed,test_mixed;
static int book_store_delete(const char* path,bool* removed){(void)path;test_delete_calls++;*removed=test_removed;return test_mixed ? (strstr(path,"book001") ? -1 : 0) : test_delete_error;}
static int book_progress_forget(const char* path){for(book_progress_watch_t* w=test_watches;w;w=w->next)if(!strcmp(w->path,path))atomic_store(&w->invalidated,true);return test_forget_error;}
static void book_store_notify_changed(void){test_notify_count++;}
static unsigned book_store_revision(void){return (unsigned)test_notify_count;}
static void free_book(void){s_text=NULL;s_path[0]=0;}
static void refresh_cached_progress(app_ctx_t* ctx);
static void return_to_cached_shelf(app_ctx_t* ctx){(void)ctx;}
static bool open_book(app_ctx_t* ctx,const char* path){(void)ctx;assert(path[0]);test_open_calls++;s_view=READING;return true;}
static char test_wrapped[512];
#define UI_PX_CAPTION 28
#define UI_MARGIN 40
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
#define READER_FULLSCREEN_PROGRESS_TOP (UI_LOCK_HEIGHT - 12)
#define UI_BAR_TOP 1096
#define UI_BAR_H 96
#define BOOK_MARGIN_MIN 0
#define BOOK_MARGIN_MAX 48
#define BOOK_MARGIN_CHOICES 9
#define BOOK_PX_MIN 36
#define BOOK_PX_MAX 72
#define EPD_DRAW_ALIGN_LEFT 0
static int ui_content_width(void){return 604;}
static int ttf_text_width_px(int px,const char* text){int width=0;for(;*text;text++)if(((unsigned char)*text&0xc0)!=0x80)width+=px;return width;}
static void ui_text(uint8_t* fb,int x,int y,int px,const char* text,int align,bool inv){(void)fb;(void)x;(void)y;(void)px;(void)align;(void)inv;assert(strlen(test_wrapped)+strlen(text)<sizeof(test_wrapped));strcat(test_wrapped,text);}
'''
settings=(source.parents[1]/'settings.h').read_text()
enum=settings[settings.index('typedef enum {\n    APP_READER_KEY_PREV'):settings.index('} app_reader_key_action_t;')+len('} app_reader_key_action_t;')]
input_header=(source.parents[1]/'book/book_reader_input.h').read_text()
input_header='\n'.join(line for line in input_header.splitlines() if not line.startswith(('#include','#pragma')))
unit+=enum+'\n'+input_header+r'''
static book_reader_tap_t s_reader_tap;
static app_reader_key_action_t test_keys[3]={APP_READER_KEY_PREV,APP_READER_KEY_TOOLS,APP_READER_KEY_NEXT};
static app_reader_key_action_t test_hold_action=APP_READER_KEY_REFRESH;
static app_reader_key_action_t app_settings_reader_hold_action(void){return test_hold_action;}
static app_reader_key_action_t app_settings_reader_key_action(unsigned key){return test_keys[key];}
static bool app_settings_main_fast_refresh(void){return false;}
// 横屏旋转替身：book_on_exit 的兜底复位走这两个入口（真机 bug：横屏长按
// 退出后 Home 竖屏布局画在横屏物理屏上，半屏错位）。
// Rotation shim: book_on_exit's fallback reset enters through these two
// (hardware bug: a landscape panel misaligns the portrait Home after a
// long-press exit).
enum EpdRotation {EPD_ROT_LANDSCAPE=0,EPD_ROT_PORTRAIT=1,EPD_ROT_INVERTED_LANDSCAPE=2,EPD_ROT_INVERTED_PORTRAIT=3};
static enum EpdRotation test_rotation=EPD_ROT_INVERTED_PORTRAIT;
static enum EpdRotation epd_get_rotation(void){return test_rotation;}
static void epd_set_rotation(enum EpdRotation rotation){test_rotation=rotation;}
static app_redraw_t toggle_reader_fullscreen(app_ctx_t *ctx){(void)ctx;s_reader_fullscreen=!s_reader_fullscreen;return APP_REDRAW_PAGE;}
'''
unit += r"""
#define SHELF_CATEGORY_LIMIT 128
typedef struct{char path[288];int pages[2],count;bool available;}shelf_category_t;
static shelf_category_t *s_categories;
static int s_category_count,s_category_capacity,s_category_selected,s_category_menu_page;
static bool s_category_menu;
static char s_category_root[288];
"""
unit += function("book_layout_balanced_rect", layout_source) + "\n"
unit += function("set_reader_view") + "\n"
unit += function("ble_pt_action_for_usage", source.parents[2] / "components/ble_page_turner/src/ble_page_turner.c") + "\n"
# 生词本：on_enter / book_on_exit 会调用它，这里用空实现顶掉，测试不验证 TF 卡落盘。
# Vocabulary log: on_enter / book_on_exit call it; no-op stand-ins keep the card out of the test.
unit += (Path(__file__).resolve().parent / "vocab_log_stubs.h").read_text(encoding="utf-8") + "\n"
# 手机推流反向通道：book_on_exit 在离开推送虚拟书时 notify EVT_READ(0x14)，
# 这里给出路径宏与空实现——本测试只验证退出路径不被它带偏，notify 本身由
# push_sink 宿主测试覆盖。
# Phone push reverse channel: book_on_exit notifies EVT_READ(0x14) when leaving
# the pushed virtual book; the path macro and a no-op stand in here. This test
# only pins that the exit path is not derailed by it - the notify itself is
# covered by the push_sink host test.
unit += '#define PUSH_DIR "/sdcard/.readpico"\n'
unit += '#define PUSH_TEXT_PATH PUSH_DIR "/push.txt"\n'
unit += 'static void push_sink_notify_read(void){}\n'
# gen18（18-a）：book_on_exit 的收摊分流读 s_push_handoff 交接标记——交接
# 离页标记自销、非交接离页带原因取消。宿主只验证退出路径不被带偏，取消
# 语义由 push_sink 宿主测试覆盖。/ gen18 (18-a): book_on_exit's teardown fork
# reads the s_push_handoff flag - a handoff exit consumes the mark, any other
# exit cancels with a reason. The host only pins that the exit path is not
# derailed; cancel semantics are covered by the push_sink host test.
unit += 'static bool s_push_handoff;\n'
unit += 'static void push_sink_cancel_why(const char *why){(void)why;}\n'
for name in ("inline_ink_gray", "reader_margin_width", "reader_margin_levels", "reader_margin_level_for", "reader_margin_for_level", "slider_index", "reader_margin_input", "reader_area", "reader_fullscreen_progress_area", "body_rect_for_tracking", "body_rect", "progress_rect", "copy_text", "reader_footer_strip_number", "favorite_key", "favorite_read_handle", "shelf_hidden_key", "shelf_hidden_read_handle", "shelf_hidden_load", "shelf_hidden_save", "shelf_rows", "row_rect", "shelf_category_add", "shelf_category_init", "shelf_category_matches", "shelf_category_remember", "shelf_category_restore", "shelf_categories_recount", "shelf_matches", "compare_books", "sort_shelf", "shelf_reserve", "delete_retry_find", "delete_retry_reserve", "delete_retry_discard", "scan_shelf_dir", "shelf_backfill_visit", "shelf_backfill_read_books", "scan_shelf", "refresh_cached_progress",             "pending_find", "pending_reserve", "pending_restore", "pending_discard", "pending_mark_latest", "pending_drop_invalidated", "pending_flush", "reader_page_offset", "save_progress", "retry_progress", "layout_name", "manage_panel", "manage_rect", "batch_rect", "manage_back_rect", "bulk_filter_rect", "bulk_nav_rect", "bulk_nav_hit", "leaves", "selected_count", "clear_selection", "toggle_selection", "select_page", "bookmark_compact", "search_begin", "refresh_search_matches", "search_finish", "refresh_capacity", "manage_apply", "manage_action", "batch_apply", "batch_action", "bulk_turn_page", "bulk_finish", "bulk_action", "bulk_control_at", "menu_handle_enabled", "reader_manual_refresh", "apply_reader_option", "reader_return", "reader_key_action", "on_key", "on_key_long", "draw_wrapped_name", "open_requested_book", "on_enter", "book_on_exit"):
    unit += function(name) + "\n"
for name in ("book_remote_direction", "shelf_turn_page", "shelf_page_arrow_rect", "reader_swipe_direction"):
    unit += function(name) + "\n"
unit += "static unsigned app_settings_system_font_size(void){return 100;}\n"
for name in ("apply_reader_layout", "reader_ui_extra", "reader_slider_rect", "reader_slider_map", "reader_slider_begin", "reader_slider_move", "reader_slider_commit"):
    unit += function(name) + "\n"
unit += r'''
int main(void) {
    for(int i=0;i<3;++i){
        EpdRect r=bulk_nav_rect(i);
        assert(r.x+r.width/2==134+i*208);
        assert(r.y+r.height/2==1144);
        assert(bulk_nav_hit(r.x+r.width/2,r.y+r.height/2)==i);
    }
    assert(bulk_nav_hit(30,1144)==-1&&bulk_nav_hit(655,1144)==-1);
    for(int vertical=0;vertical<2;++vertical){
        assert(reader_swipe_direction(UI_GESTURE_SWIPE_L,vertical)==1);
        assert(reader_swipe_direction(UI_GESTURE_SWIPE_R,vertical)==-1);
        assert(reader_swipe_direction(UI_GESTURE_SWIPE_U,vertical)==(vertical?1:0));
        assert(reader_swipe_direction(UI_GESTURE_SWIPE_D,vertical)==(vertical?-1:0));
        assert(reader_swipe_direction(UI_GESTURE_TAP,vertical)==0);
        assert(reader_swipe_direction(UI_GESTURE_CANCEL,vertical)==0);
    }
    // 竖排从右到左：横向滑动的下一页在右边，上下滑动不受影响。
    // Right-to-left vertical text: the next page sits on the right, so horizontal swipes mirror.
    test_book_vertical=true;
    assert(reader_swipe_direction(UI_GESTURE_SWIPE_L,false)==-1);
    assert(reader_swipe_direction(UI_GESTURE_SWIPE_R,false)==1);
    assert(reader_swipe_direction(UI_GESTURE_SWIPE_U,true)==1);
    test_book_vertical=false;
    // 普通/沉浸正文采用半屏或1/3+2/3，中间双击独立处理。
    // Normal/immersive tap regions use halves or 1/3+2/3 with independent center double taps.
    const EpdRect bodies[]={{36,180,612,900},{24,24,636,1182}};
    for (unsigned i=0;i<2;++i) {
        EpdRect body=bodies[i];
        for (int y=body.y;y<body.y+body.height;++y)
            assert(book_reader_tap_direction(body,342,y,true,false)==(y<body.y+body.height/3?-1:1));
        assert(book_reader_tap_direction(body,body.x,body.y,false,false)==-1);
        assert(book_reader_tap_direction(body,body.x+body.width/2,body.y,false,false)==1);
        // rtl 翻转左右分区：右半边是上一页，左半边是下一页。
        assert(book_reader_tap_direction(body,body.x,body.y,false,true)==1);
        assert(book_reader_tap_direction(body,body.x+body.width-1,body.y,false,true)==-1);
        book_reader_tap_t tap={0};
        assert(!book_reader_tap_feed(&tap,body,342,600,false,false,1000));
        assert(book_reader_tap_feed(&tap,body,340,600,true,false,1250)==BOOK_READER_TAP_FULLSCREEN);
        assert(!book_reader_tap_tick(&tap,2000,false));
        assert(!book_reader_tap_feed(&tap,body,342,600,true,false,3000));
        assert(book_reader_tap_tick(&tap,3341,false)==1);
        assert(!book_reader_tap_tick(&tap,4000,false));
    }
    app_ctx_t remote_ctx={0};s_view=SHELF;test_shelf_style=2;s_visible_count=27;
    assert(shelf_turn_page(&remote_ctx,-1)==APP_REDRAW_NONE&&remote_ctx.leaf==0);
    assert(shelf_turn_page(&remote_ctx,1)==APP_REDRAW_AREA&&remote_ctx.leaf==1);
    assert(test_cover_prepares==1&&test_shelf_renders==1&&s_area.y==176&&s_area.height==912);
    remote_ctx.leaf=2;
    assert(shelf_turn_page(&remote_ctx,1)==APP_REDRAW_NONE&&remote_ctx.leaf==2);
    assert(shelf_turn_page(&remote_ctx,-1)==APP_REDRAW_AREA&&remote_ctx.leaf==1);
    s_scan_pending=true;
    assert(shelf_turn_page(&remote_ctx,1)==APP_REDRAW_NONE&&remote_ctx.leaf==1);
    s_scan_pending=false;s_view=MANAGE;
    assert(shelf_turn_page(&remote_ctx,1)==APP_REDRAW_NONE);
    EpdRect left=shelf_page_arrow_rect(-1),right=shelf_page_arrow_rect(1);
    assert(left.y==right.y&&left.y+left.height<=UI_BAR_TOP);
    test_shelf_style=5;s_view=SHELF;s_visible_count=15;assert(shelf_rows()==4&&leaves()==4);
    for(int i=0;i<4;++i){EpdRect r=row_rect(i);assert(r.y==184+i*197&&r.y+r.height<=972);}
    s_view=MANAGE;assert(shelf_rows()==9);s_view=SHELF;
    s_visible_count=0;s_view=SHELF;test_shelf_style=0;
    for (int gray=0; gray<=255; ++gray)
        assert(inline_ink_gray((uint8_t)gray)==gray);
    EpdRect margin_slider={36,700,294,66};
    int narrow=reader_margin_input(margin_slider,40,36,48,0);
    int wide=reader_margin_input(margin_slider,325,36,48,0);
    assert(reader_margin_width(0,48,0)>reader_margin_width(24,48,0));
    assert(reader_margin_width(narrow,48,0)>reader_margin_width(36,48,0));
    assert(wide==36&&reader_margin_width(wide,48,0)==reader_margin_width(36,48,0));
    assert(reader_margin_level_for(24,72,0)==1&&reader_margin_for_level(1,24,72,0)==24);
    // 每个字体/字间距下，只展示实际宽度不同的档位，任意旧边距只读保留。
    // All fonts/tracking values expose distinct visible widths and retain existing margins on a read.
    for(int px=36;px<=72;++px)for(int tracking=-4;tracking<=4;tracking+=2){
        int widths[BOOK_MARGIN_CHOICES],values[BOOK_MARGIN_CHOICES];
        int count=reader_margin_levels(px,tracking,widths,values);
        assert(count>=2&&count<=BOOK_MARGIN_CHOICES&&values[0]==0&&values[count-1]==48);
        for(int i=0;i<count;++i){
            assert(widths[i]==reader_margin_width(values[i],px,tracking));
            if(i)assert(widths[i-1]>widths[i]&&values[i-1]<values[i]);
        }
        for(int margin=0;margin<=48;++margin)
            assert(reader_margin_for_level(reader_margin_level_for(margin,px,tracking),margin,px,tracking)==margin);
        for(int i=1;i<count;++i){
            int smaller=reader_margin_input(margin_slider,40,values[i],px,tracking);
            assert(reader_margin_width(smaller,px,tracking)>widths[i]);
        }
    }
    assert(reader_margin_width(reader_margin_input(margin_slider,325,36,48,-2),48,-2)
           <=reader_margin_width(36,48,-2));
    // Actual slider commit must reflow and persist every new small-margin value, not just move the thumb.
    app_ctx_t margin_ctx={0};
    for(int margin=0;margin<=48;margin+=6){
        s_margin=margin==0?48:0;test_saved_margin=s_margin;s_px=48;s_page=2;
        int before=test_reflows;
        assert(apply_reader_layout(&margin_ctx,48,margin,130,50)==APP_REDRAW_PAGE);
        assert(s_margin==margin&&test_saved_margin==margin&&test_reflows==before+1&&s_page==2);
        assert(test_reflow_rect.width==reader_margin_width(margin,48,0));
    }
    assert(apply_reader_layout(&margin_ctx,48,-1,130,50)==APP_REDRAW_NONE);
    assert(apply_reader_layout(&margin_ctx,48,49,130,50)==APP_REDRAW_NONE);
    s_margin=test_saved_margin=24;s_reader_slider_endpoint=false;
    reader_slider_begin(1,40);assert(s_reader_slider_endpoint&&s_reader_preview_margin==0);
    assert(reader_slider_commit(&margin_ctx)==APP_REDRAW_PAGE&&s_margin==0&&test_saved_margin==0);
    s_margin=test_saved_margin=24;s_reader_slider_endpoint=true;
    reader_slider_begin(1,183);assert(!s_reader_slider_endpoint);
    reader_slider_move(100);
    assert(reader_slider_commit(&margin_ctx)==APP_REDRAW_PAGE&&s_margin==0&&test_saved_margin==0);
    assert(body_rect().width==reader_margin_width(0,48,0));
    s_margin=test_saved_margin=24;test_reflow_failures=1;
    assert(apply_reader_layout(&margin_ctx,48,0,130,50)==APP_REDRAW_NONE);
    assert(s_margin==24&&test_saved_margin==24&&!test_draw_locked);
    s_margin=test_saved_margin=36;s_page=0;test_reflows=0;
    reader_bookmarks_t marks={.count=6};
    for(unsigned i=0;i<marks.count;++i)marks.entries[i].byte_off=(i+1)*100;
    bookmark_compact(&marks,(1u<<0)|(1u<<2)|(1u<<5));
    assert(marks.count==3&&marks.entries[0].byte_off==200&&
           marks.entries[1].byte_off==400&&marks.entries[2].byte_off==500);
    bookmark_compact(&marks,(1u<<0)|(1u<<1)|(1u<<2));
    assert(marks.count==0);
    EpdRect reading_body=reader_area(), text_body=body_rect(), reading_footer=progress_rect();
    assert(reading_body.y>=160&&reading_body.y%32==0);
    assert(text_body.y==188&&text_body.y>=reading_body.y);
    assert(reading_body.y+reading_body.height==UI_BAR_TOP+8);
    assert(text_body.y+text_body.height==reading_body.y+reading_body.height);
    assert(reading_body.y+reading_body.height<=reading_footer.y+16);
    assert(reading_footer.y==UI_BAR_TOP&&reading_footer.height==UI_BAR_H);
    assert(reading_footer.x==36&&reading_footer.width==612);
    s_reader_fullscreen=true;
    reading_body=reader_area();
    text_body=body_rect();
    EpdRect fullscreen_progress=reader_fullscreen_progress_area();
    assert(reading_body.y==80&&text_body.y==96);
    assert(reading_body.y+reading_body.height==fullscreen_progress.y);
    assert(text_body.y+text_body.height==fullscreen_progress.y-2);
    assert(fullscreen_progress.y==UI_LOCK_HEIGHT-12&&fullscreen_progress.height==12);
    test_reader_immersive=true;
    reading_body=reader_area();
    text_body=body_rect();
    assert(reading_body.y==0&&text_body.y==24&&text_body.y+text_body.height==fullscreen_progress.y-2);
    s_reader_fullscreen=test_reader_immersive=false;
    char footer_name[72];
    reader_footer_strip_number(footer_name,sizeof(footer_name),"第一章：海边的信");
    assert(!strcmp(footer_name,"海边的信"));
    reader_footer_strip_number(footer_name,sizeof(footer_name),"第 3 章 · 雨后的港口");
    assert(!strcmp(footer_name,"雨后的港口"));
    reader_footer_strip_number(footer_name,sizeof(footer_name),"扉页");
    assert(!strcmp(footer_name,"扉页"));
    reader_footer_strip_number(footer_name,sizeof(footer_name),"第1章");
    assert(!strcmp(footer_name,"未命名章节"));
    s_jump_page=1;s_jump_offset=145;
    assert(reader_page_offset(1)==145&&reader_page_offset(2)==200);
    s_jump_offset=SIZE_MAX;
    shelf_entry_t a={.search_match=true,.name="Same.txt",.path="/sdcard/books/A/Same.txt"};
    shelf_entry_t b={.search_match=true,.name="Same.txt",.path="/sdcard/books/B/Same.txt"};
    assert(compare_books(&a,&b)<0);
    b.recent=10;s_recent_sort=true;assert(compare_books(&a,&b)>0);
    s_recent_sort=false;a.favorite=true;
    for(int style=1;style<=4;style++){test_shelf_style=style;assert(compare_books(&a,&b)<0);}
    s_recent_sort=true;assert(compare_books(&a,&b)<0);s_recent_sort=false;test_shelf_style=0;
    b.is_flash=true;s_filter=1;assert(compare_books(&a,&b)<0);
    s_filter=2;assert(compare_books(&a,&b)>0);
    s_filter=0;s_recent_sort=false;
    snprintf(test_roots[0].path,sizeof(test_roots[0].path),"/tmp/book-ui-%d",(int)getpid());
    assert(mkdir(test_roots[0].path,0700)==0);
    for(int i=0;i<65;i++){char path[340];snprintf(path,sizeof(path),"%s/book%03d.txt",test_roots[0].path,i);FILE* f=fopen(path,"w");assert(f);fputs("x",f);fclose(f);}
    app_ctx_t ctx={0};scan_shelf(&ctx);
    assert(s_count==65&&s_visible_count==65&&s_shelf_capacity>=65);
    char nested[340],nested_book[380];snprintf(nested,sizeof(nested),"%s/nested",test_roots[0].path);
    assert(mkdir(nested,0700)==0);snprintf(nested_book,sizeof(nested_book),"%s/inside.epub",nested);
    FILE* nested_file=fopen(nested_book,"w");assert(nested_file);fputs("x",nested_file);fclose(nested_file);
    scan_shelf(&ctx);assert(s_count==66);assert(unlink(nested_book)==0);assert(rmdir(nested)==0);scan_shelf(&ctx);
    char deep[14][340];snprintf(deep[0],sizeof(deep[0]),"%s",test_roots[0].path);
    for(int level=1;level<=13;level++){
        snprintf(deep[level],sizeof(deep[level]),"%s/d%02d",deep[level-1],level);
        assert(mkdir(deep[level],0700)==0);
    }
    char deep_book[380],ignored_book[380];
    snprintf(deep_book,sizeof(deep_book),"%s/deep.epub",deep[12]);
    snprintf(ignored_book,sizeof(ignored_book),"%s/ignored.epub",deep[13]);
    FILE* deep_file=fopen(deep_book,"w");assert(deep_file);fputs("x",deep_file);fclose(deep_file);
    deep_file=fopen(ignored_book,"w");assert(deep_file);fputs("x",deep_file);fclose(deep_file);
    scan_shelf(&ctx);assert(s_count==66&&s_shelf_warning[0]);
    assert(unlink(deep_book)==0&&unlink(ignored_book)==0);
    for(int level=13;level>=1;level--)assert(rmdir(deep[level])==0);
    scan_shelf(&ctx);assert(s_count==65);
    // 缓存路径也补入卡上其他目录的已读书，跳过重复、缺失和大小已变的记录。
    // Cached refresh backfills outside-root reads and skips duplicate, missing and resized files.
    char outside_dir[96],outside[BOOK_STORE_PATH_MAX],changed[BOOK_STORE_PATH_MAX];
    snprintf(outside_dir,sizeof(outside_dir),"/tmp/book-ui-outside-%d",(int)getpid());
    assert(mkdir(outside_dir,0700)==0);
    snprintf(outside,sizeof(outside),"%s/读过.epub",outside_dir);
    snprintf(changed,sizeof(changed),"%s/changed.txt",outside_dir);
    FILE* outside_file=fopen(outside,"w");assert(outside_file);fputs("x",outside_file);fclose(outside_file);
    outside_file=fopen(changed,"w");assert(outside_file);fputs("xx",outside_file);fclose(outside_file);
    // r93 起 refresh_cached_progress 会先比进度修订号，scan 时登记的号若没变就直接返回、不回填；
    // 真机上写进度会递增它，所以这里登记完新进度后必须同步递增。
    // Since r93 refresh_cached_progress compares the progress revision first and returns without
    // backfilling when it matches the one recorded at scan time; on device writing progress bumps
    // it, so registering new progress here has to bump the stub counter too.
    test_progress_count=4;
    test_progress_revision++;
    strcpy(test_progress[0].path,outside);
    test_progress[0].value=(book_progress_t){.file_size=1,.chapter=3,.byte_off=17,.px=48,.pct=42,.last_open_s=19};
    snprintf(test_progress[1].path,BOOK_STORE_PATH_MAX,"%s/missing.txt",outside_dir);
    strcpy(test_progress[2].path,changed);
    snprintf(test_progress[3].path,BOOK_STORE_PATH_MAX,"%s/book005.txt",test_roots[0].path);
    for(int i=1;i<4;i++)test_progress[i].value=(book_progress_t){.file_size=1,.px=48,.pct=7};
    favorite_key(outside,test_favorite_key);
    strcpy(test_recent_path,outside);
    refresh_cached_progress(&ctx);assert(s_count==66&&s_visible_count==66);
    assert(!strcmp(s_shelf[0].path,outside)&&!strcmp(s_shelf[0].name,"读过")&&
           !strcmp(s_shelf[0].author,"作者")&&s_shelf[0].favorite&&s_shelf[0].has_progress&&
           s_shelf[0].pct==42&&s_shelf[0].chapter==3&&s_shelf[0].recent==19);
    refresh_cached_progress(&ctx);assert(s_count==66);
    // 六个按钮可命中；取消不写入，失败可重试，移出不删除文件或修改进度。
    // All six buttons respond; cancellation is safe and failed removal retries without data loss.
    s_view=BULK;clear_selection();toggle_selection(0);
    EpdRect unshelf= batch_rect(5);int removal_delete_calls=test_delete_calls;
    batch_action(&ctx,unshelf.x+1,unshelf.y+1);
    assert(s_batch_confirm&&s_batch_kind==BATCH_UNSHELF&&!shelf_hidden_load(outside));
    EpdRect dismiss=ui_row_rect(0,2,620,UI_BTN_H);
    batch_action(&ctx,dismiss.x+1,dismiss.y+1);
    assert(!s_batch_confirm&&!shelf_hidden_load(outside)&&selected_count()==1);
    test_hidden_error=ESP_FAIL;batch_apply(&ctx);
    assert(s_count==66&&selected_count()==1&&!shelf_hidden_load(outside));
    test_hidden_error=ESP_OK;batch_apply(&ctx);
    assert(s_count==65&&!selected_count()&&shelf_hidden_load(outside)&&test_delete_calls==removal_delete_calls);
    struct stat outside_stat;assert(stat(outside,&outside_stat)==0&&outside_stat.st_size==1);
    book_progress_t retained;
    assert(book_progress_load(outside,1,&retained)&&retained.pct==42&&retained.byte_off==17);
    scan_shelf(&ctx);refresh_cached_progress(&ctx);assert(s_count==65&&shelf_hidden_load(outside));
    // 真机上「重新上书架」发生在 open_book：先 save_progress()（递增进度修订号）再取消隐藏，
    // 所以随后的 refresh 不会被修订号短路；桩里补上这次递增，否则回填永远不发生。
    // On device re-shelving happens in open_book: save_progress() bumps the progress revision
    // before unhiding, so the following refresh is not short-circuited; mirror that bump here.
    assert(shelf_hidden_save(outside,false));test_progress_revision++;refresh_cached_progress(&ctx);
    assert(s_count==66&&!shelf_hidden_load(outside));
    assert(shelf_hidden_save(test_progress[3].path,true));scan_shelf(&ctx);assert(s_count==65);
    assert(shelf_hidden_save(test_progress[3].path,false));
    char hidden_key[11];shelf_hidden_key(outside,hidden_key);
    assert(nvs_set_str(2,hidden_key,changed)==ESP_OK);
    assert(!shelf_hidden_save(outside,true)&&!shelf_hidden_load(outside));
    assert(!shelf_hidden_save(outside,false));
    assert(nvs_erase_key(2,hidden_key)==ESP_OK);
    test_progress_count=0;test_recent_path[0]=test_favorite_key[0]=0;
    assert(unlink(outside)==0&&unlink(changed)==0&&rmdir(outside_dir)==0);
    scan_shelf(&ctx);assert(s_count==65);
    // 真实批量路由覆盖旧菜单重叠区、跨页勾选、边界、确认与退出。
    // Exercise real batch routing across the former menu overlap, selections, bounds and exit.
    s_view=BULK;ctx.leaf=0;ctx.request_menu=false;clear_selection();s_batch_confirm=false;
    assert(!menu_handle_enabled(&ctx));
    EpdRect entry=row_rect(0),hit;
    assert(bulk_control_at(&ctx,entry.x+10,entry.y+10,&hit)==0);
    assert(bulk_action(&ctx,entry.x+10,entry.y+10)==APP_REDRAW_PAGE&&selected_count()==1);
    for(int page=1;page<=10;++page){
        assert(bulk_control_at(&ctx,636,1150,&hit)==102);
        assert(bulk_action(&ctx,636,1150)==APP_REDRAW_PAGE&&ctx.leaf==page);
        assert(s_view==BULK&&!ctx.request_menu&&selected_count()==1);
    }
    assert(bulk_action(&ctx,636,1150)==APP_REDRAW_NONE&&ctx.leaf==10);
    assert(bulk_action(&ctx,entry.x+10,entry.y+10)==APP_REDRAW_PAGE&&selected_count()==2);
    assert(s_shelf[0].selected&&s_shelf[60].selected);
    s_batch_confirm=true;
    assert(bulk_control_at(&ctx,636,1150,&hit)==-1);
    assert(bulk_action(&ctx,636,1150)==APP_REDRAW_NONE&&ctx.leaf==10);
    EpdRect circle_button=manage_back_rect();
    assert(bulk_action(&ctx,circle_button.x+10,circle_button.y+10)==APP_REDRAW_PAGE);
    assert(!s_batch_confirm&&s_view==BULK&&selected_count()==2);
    EpdRect finish=bulk_nav_rect(1);
    assert(bulk_action(&ctx,finish.x+10,finish.y+10)==APP_REDRAW_PAGE);
    assert(s_view==SHELF&&!selected_count()&&!ctx.request_menu&&ctx.leaf==6);
    for(int view=SHELF;view<=EDIT;++view){s_view=view;assert(!menu_handle_enabled(&ctx));}
    s_view=BULK;ctx.leaf=0;
    assert(bulk_turn_page(&ctx,-1)==APP_REDRAW_NONE);
    s_scan_pending=true;assert(bulk_turn_page(&ctx,1)==APP_REDRAW_NONE);s_scan_pending=false;
        ctx.leaf=3;s_view=MANAGE;s_clear_confirm=false;s_file_removed=false;
    EpdRect back=manage_rect(0,3);manage_action(&ctx,back.x+1,back.y+1);
    assert(s_view==SHELF&&ctx.leaf==3);
    EpdRect circle=manage_back_rect();assert(circle.y==92&&circle.height==44);
    EpdRect filter=bulk_filter_rect(1);assert(filter.y==216&&filter.height==70);
    s_view=MANAGE;manage_action(&ctx,circle.x+20,circle.y+20);assert(s_view==SHELF);
    assert(!strcmp(s_shelf[0].name,"book000")&&!strcmp(s_shelf[64].name,"book064"));
    test_degraded=true;scan_shelf(&ctx);assert(s_count==65&&s_shelf_warning[0]);test_degraded=false;
    test_root_count=2;strcpy(test_roots[1].path,"/nonexistent-book-root");scan_shelf(&ctx);assert(s_count==65&&s_shelf_warning[0]);test_root_count=1;
    free(s_shelf);s_shelf=NULL;s_shelf_capacity=0;s_count=0;test_oom=true;scan_shelf(&ctx);assert(s_count==0&&s_shelf_warning[0]);test_oom=false;
    strcpy(s_path,"/sdcard/books/a.txt");s_text="text";s_page=1;s_unsaved=8;
    assert(pending_reserve(s_path));pending_mark_latest(s_path);test_save_error=-1;save_progress();
    assert(s_unsaved==8&&s_save_failed&&pending_find(s_path)->dirty);
    book_progress_t restored={0};assert(pending_reserve("/sdcard/books/b.txt"));pending_mark_latest("/sdcard/books/b.txt");
    assert(pending_restore(s_path,s_file_size,&restored)&&restored.byte_off==100);
    assert(!pending_restore(s_path,s_file_size+1,&restored));pending_mark_latest(s_path);
    test_save_error=0;test_last_error=-1;save_progress();assert(s_unsaved==8&&pending_find(s_path)->progress_saved);
    int calls=test_save_calls;save_progress();assert(test_save_calls==calls&&s_unsaved==8);
    test_last_error=0;retry_progress();assert(!s_unsaved&&!s_save_failed);
    s_unsaved=7;retry_progress();assert(!s_unsaved);
    pending_progress_t* earlier=pending_find(s_path);earlier->dirty=true;earlier->progress_saved=false;
    assert(pending_reserve("/sdcard/books/b.txt"));pending_progress_t* newer=pending_find("/sdcard/books/b.txt");newer->dirty=true;
    pending_mark_latest("/sdcard/books/b.txt");pending_mark_latest(s_path);
    s_text=NULL;retry_progress();assert(!strcmp(test_last_path,s_path)&&!s_save_failed);
    strcpy(s_managed.path,s_path);s_view=MANAGE;s_delete_confirm=true;s_clear_confirm=true;
    EpdRect cancel=manage_rect(0,2);manage_action(&ctx,cancel.x+1,cancel.y+1);assert(!s_clear_confirm&&test_delete_calls==0);
    EpdRect del=manage_rect(2,3);manage_action(&ctx,del.x+1,del.y+1);assert(s_clear_confirm&&s_delete_confirm&&test_delete_calls==0);
    test_removed=false;test_delete_error=-1;manage_apply(&ctx);assert(s_view==MANAGE&&!s_file_removed&&s_manage_message[0]);
    test_removed=true;manage_apply(&ctx);assert(s_view==MANAGE&&s_file_removed&&test_notify_count==1&&!pending_find(s_managed.path));assert(s_store_revision==book_store_revision());
    test_forget_error=-1;manage_apply(&ctx);assert(s_view==MANAGE&&test_notify_count==1);
    test_forget_error=0;manage_apply(&ctx);assert(s_view==SHELF&&test_notify_count==1);
    strcpy(s_managed.name,"Short.txt");assert(manage_panel().height<700);
    ctx.request_app=NULL;
    char long_name[256];memset(long_name,'W',255);long_name[255]=0;test_wrapped[0]=0;
    assert(draw_wrapped_name(NULL,long_name,176)+104<870);assert(!strcmp(test_wrapped,long_name));
    for(int i=0;i<80;i++){memcpy(long_name+i*3,"书",3);}long_name[240]=0;test_wrapped[0]=0;
    assert(draw_wrapped_name(NULL,long_name,176)+104<870);assert(!strcmp(test_wrapped,long_name));
    scan_shelf(&ctx);s_view=BULK;toggle_selection(0);toggle_selection(14);assert(selected_count()==2);
    s_recent_sort=true;sort_shelf(&ctx);assert(selected_count()==2);select_page(1);assert(selected_count()==8);
    ctx.leaf=2;strcpy(s_query,"book");search_begin();strcpy(s_search_draft,"bad");search_finish(&ctx,false);
    assert(ctx.leaf==2&&!strcmp(s_query,"book")&&selected_count()==8);
    search_begin();assert(on_key(&ctx,UI_KEY_3)==APP_REDRAW_PAGE&&ctx.leaf==2&&s_view==BULK);
    search_begin();assert(on_key(&ctx,UI_KEY_1)==APP_REDRAW_PAGE&&ctx.leaf==2&&s_view==BULK);
    s_batch_confirm=true;assert(on_key(&ctx,UI_KEY_3)==APP_REDRAW_PAGE&&!s_batch_confirm&&ctx.leaf==2);
    s_batch_confirm=true;assert(on_key(&ctx,UI_KEY_1)==APP_REDRAW_PAGE&&!s_batch_confirm&&ctx.leaf==2);
    search_begin();strcpy(s_search_draft,"book001");search_finish(&ctx,true);assert(!selected_count()&&s_visible_count==1);
    search_begin();memset(s_search_draft,'x',64);s_search_draft[64]=0;ui_text_edit_place(&s_search_input,64);
    assert(!ui_text_edit_insert(&s_search_input,"a"));assert(ui_text_edit_backspace(&s_search_input));
    assert(strlen(s_search_draft)==63);s_search_draft[0]=0;search_finish(&ctx,false);
    strcpy(s_query,"");refresh_search_matches();sort_shelf(&ctx);clear_selection();s_view=BULK;toggle_selection(0);toggle_selection(1);
    s_batch_confirm=true;calls=test_delete_calls;EpdRect bc=ui_row_rect(0,2,620,UI_BTN_H);batch_action(&ctx,bc.x+1,bc.y+1);assert(!s_batch_confirm&&test_delete_calls==calls);
    s_batch_kind=BATCH_DELETE;test_removed=true;test_delete_error=-1;batch_apply(&ctx);assert(selected_count()==2);
    calls=test_delete_calls;test_forget_error=0;batch_apply(&ctx);assert(!selected_count()&&test_delete_calls==calls&&s_count==63);
    scan_shelf(&ctx);clear_selection();toggle_selection(0);toggle_selection(1);test_mixed=true;
    batch_apply(&ctx);assert(s_count==64&&selected_count()==1&&s_shelf[0].removed);
    calls=test_delete_calls;batch_apply(&ctx);assert(s_count==63&&!selected_count()&&test_delete_calls==calls);
    // 保存失败的 A 不应因无关 B 变动而丢失。/ An unrelated B change must retain A's failed save.
    strcpy(s_path,"/sdcard/books/a.txt");s_text="text";s_page=2;s_unsaved=8;
    assert(pending_reserve(s_path));pending_mark_latest(s_path);test_save_error=-1;save_progress();
    book_on_exit(&ctx);assert(book_progress_forget("/sdcard/books/b.txt")==ESP_OK);book_store_notify_changed();on_enter(&ctx);
    assert(pending_restore("/sdcard/books/a.txt",s_file_size,&restored)&&restored.byte_off==200);
    test_save_error=0;calls=test_save_calls;retry_progress();assert(test_save_calls==calls+1&&!s_save_failed);
    // 同路径替换即使 NVS 清理失败，也不能回写旧进度。/ Replacing A must not restore stale progress even when cleanup fails.
    strcpy(s_path,"/sdcard/books/a.txt");s_text="text";s_unsaved=8;assert(pending_reserve(s_path));
    pending_mark_latest(s_path);test_save_error=-1;save_progress();book_on_exit(&ctx);
    test_forget_error=-1;assert(book_progress_forget("/sdcard/books/a.txt")!=ESP_OK);book_store_notify_changed();
    on_enter(&ctx);assert(!pending_find("/sdcard/books/a.txt"));test_save_error=0;
    calls=test_save_calls;retry_progress();assert(test_save_calls==calls);test_forget_error=0;
    // 文件删除后清理失败，切页回来仍可重试。/ Cleanup after deletion remains retryable across page exits.
    scan_shelf(&ctx);s_managed=s_shelf[0];s_view=MANAGE;s_delete_confirm=true;s_file_removed=false;
    test_mixed=false;test_removed=true;test_delete_error=-1;manage_apply(&ctx);
    assert(s_file_removed);assert(unlink(s_managed.path)==0);
    book_on_exit(&ctx);on_enter(&ctx);scan_shelf(&ctx);
    bool found_retry=false;
    for(int i=0;i<s_count;i++)if(!strcmp(s_shelf[i].path,s_managed.path)){found_retry=s_shelf[i].removed;}
    assert(found_retry);
    s_view=MANAGE;calls=test_delete_calls;test_forget_error=0;manage_apply(&ctx);
    assert(s_view==SHELF&&test_delete_calls==calls);
    // 批量失败记录同样跨页存活，重试仅清元数据。/ Batch cleanup retries also survive exits without repeating unlink.
    scan_shelf(&ctx);clear_selection();toggle_selection(0);toggle_selection(1);s_batch_kind=BATCH_DELETE;
    batch_apply(&ctx);assert(selected_count()==2&&s_delete_retries);
    for(int i=0;i<s_count;i++)if(s_shelf[i].removed)assert(unlink(s_shelf[i].path)==0);
    book_on_exit(&ctx);on_enter(&ctx);scan_shelf(&ctx);
    int retry_count=0;
    for(int i=0;i<s_count;i++)if(s_shelf[i].removed){s_shelf[i].selected=true;++retry_count;}
    assert(retry_count==2);calls=test_delete_calls;batch_apply(&ctx);
    assert(!s_delete_retries&&test_delete_calls==calls&&!selected_count());
    // 首页指定的书籍无需先扫描或绘制书架。/ Home's requested book opens before a shelf scan.
    snprintf(s_requested_open,sizeof(s_requested_open),"%s/book010.txt",test_roots[0].path);
    s_requested_open_home=true;s_shelf_cache_valid=false;calls=test_open_calls;
    on_enter(&ctx);
    assert(test_open_calls==calls+1&&s_view==READING&&!s_scan_pending&&s_reader_return_home);
    test_nav_target=-1;s_text="body";
    assert(on_key_long(&ctx,UI_KEY_2)==APP_REDRAW_FULL&&test_nav_target==-1&&s_view==READING);
    book_on_exit(&ctx);assert(!s_reader_return_home);
    // 横屏下退出兜底复位物理旋转，竖屏下不动（与 app_loop 的 prepare_lock 复位
    // 互为两面：那处覆盖不经切换的系统画面）。/ A landscape panel is reset on
    // exit; portrait stays untouched (two faces of one boundary with
    // prepare_lock's reset, which covers system frames without a switch).
    test_rotation=EPD_ROT_LANDSCAPE;book_on_exit(&ctx);assert(test_rotation==EPD_ROT_INVERTED_PORTRAIT);
    test_rotation=EPD_ROT_INVERTED_PORTRAIT;book_on_exit(&ctx);assert(test_rotation==EPD_ROT_INVERTED_PORTRAIT);
    s_view=READING;test_nav_target=-1;s_text="body";
    assert(on_key_long(&ctx,UI_KEY_2)==APP_REDRAW_FULL&&test_nav_target==-1&&s_view==READING);
    // 屏幕返回仍回来源，中键长按始终全刷。/ Screen back returns to source; middle holds always refresh.
    assert(reader_return(&ctx)==APP_REDRAW_PAGE&&s_view==SHELF);
    s_view=READING;s_text="text";s_reader_fullscreen=true;test_hold_refresh=true;test_nav_target=-1;
    s_reader_return_home=true;s_reader_panel=READER_PANEL_TOOLS;s_reader_slider=0;
    s_reader_footer_pending=s_reader_image_refresh_pending=s_water_turn_pending=s_reader_cleanup=true;
    s_turns=4;s_du_count=2;
    assert(on_key_long(&ctx,UI_KEY_2)==APP_REDRAW_FULL&&test_nav_target==-1&&s_view==READING);
    assert(s_reader_fullscreen&&s_reader_panel==READER_PANEL_NONE&&s_reader_slider==-1);
    assert(!s_reader_footer_pending&&!s_reader_image_refresh_pending&&!s_water_turn_pending&&!s_reader_cleanup&&!s_turns&&!s_du_count);
    test_hold_refresh=false;s_reader_return_home=false;
    s_reader_panel=READER_PANEL_TOOLS;s_page=1;
    assert(apply_reader_option(&ctx,0)==APP_REDRAW_PAGE&&test_power_turn);
    assert(on_key_long(&ctx,UI_KEY_2)==APP_REDRAW_FULL);
    assert(apply_reader_option(&ctx,1)==APP_REDRAW_PAGE&&test_reader_immersive&&s_page==1);
    assert(apply_reader_option(&ctx,2)==APP_REDRAW_PAGE&&test_hide_images&&!test_images_visible&&s_page==1);
    assert(test_image_preparations==2&&test_reflows==2);
    test_reflow_failures=1;
    assert(apply_reader_option(&ctx,2)==APP_REDRAW_PAGE&&test_hide_images&&!test_images_visible&&s_page==1);
    assert(test_reflows==4&&test_image_preparations==2);
    assert(apply_reader_option(&ctx,2)==APP_REDRAW_PAGE&&!test_hide_images&&test_images_visible&&s_page==1);
    // 归因：我方行表第 8 槽「横屏阅读」派发到 toggle_reader_landscape，来回各一次。
    // Attribution: our table's eighth slot dispatches to toggle_reader_landscape, once each way.
    assert(apply_reader_option(&ctx,3)==APP_REDRAW_PAGE&&test_reader_landscape);
    assert(apply_reader_option(&ctx,3)==APP_REDRAW_PAGE&&!test_reader_landscape);
    // r93 把 reader_return 单参化：HOME 不再无条件回首页，改由来源决定——s_reader_return_home
    // 为真才导航回首页（NONE），否则回书架（PAGE）。r92 是 reader_return(ctx,true) 恒导航，
    // 故此处期望随上游语义更新；本例来源非首页（835 行置 false），走回书架分支。
    // r93 collapsed reader_return to one argument: HOME no longer always goes home, it follows
    // the origin — nav(0)/NONE only when s_reader_return_home, else back to the shelf (PAGE).
    // r92 called reader_return(ctx,true) and always navigated, so the expectation follows the
    // upstream change; this case comes from a non-home origin (set false above).
    test_hold_action=APP_READER_KEY_HOME;test_nav_target=-1;
    assert(on_key_long(&ctx,UI_KEY_2)==APP_REDRAW_PAGE&&test_nav_target==-1&&s_view==SHELF);
    test_hold_action=APP_READER_KEY_REFRESH;
    test_hold_refresh=test_power_turn=false;s_reader_fullscreen=test_reader_immersive=false;
    s_view=TOC;s_toc_jump_open=true;s_toc_jump_percent=50;
    assert(on_key(&ctx,UI_KEY_1)==APP_REDRAW_PAGE&&s_toc_jump_percent==45);
    assert(on_key(&ctx,UI_KEY_3)==APP_REDRAW_PAGE&&s_toc_jump_percent==50);
    assert(on_key(&ctx,UI_KEY_2)==APP_REDRAW_PAGE&&!s_toc_jump_open);
    // 目录统一切系统字形；返回正文与长按返回都恢复阅读字形。
    // TOC switches to the system face; both return paths restore the reader face.
    s_text="body";set_reader_view(TOC);assert(test_system_face&&!test_draw_locked);
    assert(on_key(&ctx,UI_KEY_2)==APP_REDRAW_PAGE&&s_view==READING&&!test_system_face);
    set_reader_view(TOC);
    assert(on_key_long(&ctx,UI_KEY_2)==APP_REDRAW_PAGE&&s_view==READING&&!test_system_face);
    s_text=NULL;set_reader_view(TOC);
    assert(on_key(&ctx,UI_KEY_2)==APP_REDRAW_PAGE&&s_view==SHELF&&test_system_face);
    s_view=READING;s_text="body";s_reader_tap.pending=true;
    test_keys[0]=APP_READER_KEY_PREV;test_keys[1]=APP_READER_KEY_TOOLS;test_keys[2]=APP_READER_KEY_NEXT;
    assert(on_key(&ctx,UI_KEY_1)==APP_REDRAW_NONE && test_turn_direction==-1 && !s_reader_tap.pending);
    assert(on_key(&ctx,UI_KEY_3)==APP_REDRAW_NONE && test_turn_direction==1);
    s_reader_panel=READER_PANEL_NONE;assert(on_key(&ctx,UI_KEY_2)==APP_REDRAW_PAGE && s_reader_panel==READER_PANEL_TOOLS);
    assert(on_key(&ctx,UI_KEY_2)==APP_REDRAW_PAGE && s_reader_panel==READER_PANEL_NONE);
    test_keys[0]=APP_READER_KEY_HOME;test_keys[1]=APP_READER_KEY_FULLSCREEN;test_keys[2]=APP_READER_KEY_TOOLS;
    s_reader_fullscreen=false;assert(on_key(&ctx,UI_KEY_2)==APP_REDRAW_PAGE && s_reader_fullscreen);
    assert(on_key(&ctx,UI_KEY_3)==APP_REDRAW_PAGE && s_reader_panel==READER_PANEL_TOOLS);
    assert(on_key_long(&ctx,UI_KEY_2)==APP_REDRAW_FULL && s_reader_panel==READER_PANEL_NONE);
    // 同上：r93 的 HOME 只在「来源是首页」时导航，所以先把来源置为首页，仍然验证 HOME 键回首页。
    // Same as above: r93's HOME navigates only when the origin is Home, so set that origin first
    // and still assert that the HOME key returns to Home.
    s_reader_return_home=true;
    test_nav_target=-1;assert(on_key(&ctx,UI_KEY_1)==APP_REDRAW_NONE && test_nav_target==0);
    s_view=READING;assert(reader_key_action(&ctx,APP_READER_KEY_NONE)==APP_REDRAW_NONE);
    assert(reader_key_action(&ctx,APP_READER_KEY_REFRESH)==APP_REDRAW_FULL);
    free(s_shelf);free(s_categories);
    while(s_pending)pending_discard(s_pending->path);
    for(int i=0;i<65;i++){char path[340];snprintf(path,sizeof(path),"%s/book%03d.txt",test_roots[0].path,i);unlink(path);}rmdir(test_roots[0].path);
    puts("book_ui_host_test: PASS");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / "test.c"
    exe = Path(tmp) / "test"
    c.write_text(unit, encoding="utf-8")
    subprocess.run(["gcc", "-std=c11", "-D_DEFAULT_SOURCE", "-Wall", "-Wextra", "-Werror", "-Wno-unused-variable",
                    "-Wno-unused-function", "-Wno-error=format-truncation", "-Wno-error=restrict",
                    "-fsanitize=address,undefined", "-I", str(source.parents[1] / "ui"), "-I", str(source.parents[1] / "book"),
                    str(c), str(source.parents[1] / "ui/ui_text_edit.c"), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
