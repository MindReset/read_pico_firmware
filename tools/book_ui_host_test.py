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

def function(name):
    text = source.read_text(encoding="utf-8")
    import re
    found = re.search(r"^static [^\n]+\b" + name + r"\([^\n]*\) \{", text, re.M)
    assert found, name
    start, at, depth, quote, escape = found.start(), found.end(), 1, None, False
    while depth:
        c = text[at]
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
#define ESP_OK 0
#define ESP_ERR_NO_MEM 1
#define ESP_ERR_NOT_FOUND 2
#define ESP_ERR_INVALID_SIZE 3
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define ESP_LOGI(...) ((void)0)
typedef int esp_err_t;
typedef struct {int leaf;bool request_menu;} app_ctx_t;
typedef enum {APP_REDRAW_PAGE,APP_REDRAW_AREA,APP_REDRAW_NONE} app_redraw_t;
typedef struct {int x,y,width,height;} EpdRect;
static EpdRect ui_bar_rect(int i,int count){int width=(508-(count-1)*12)/count;return(EpdRect){40+i*(width+12),1096,width,96};}
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
typedef struct {char name[256],path[288];uint32_t size;bool is_flash,has_progress;uint8_t pct;uint32_t recent;bool selected,removed,search_match;} shelf_entry_t;
#define BOOK_ROWS 7
#define UI_BTN_H 84
static EpdRect ui_row_rect(int i,int count,int y,int height){EpdRect r=ui_bar_rect(i,count);r.y=y;r.height=height;return r;}
static char s_query[65],s_search_draft[65],s_batch_message[128];
static bool s_batch_confirm,s_batch_delete;
static bool read_pico_search_match(const char* name,const char* query){return !*query||strstr(name,query)!=NULL;}
static int book_chapter_count(void){return 1;}
#define BOOK_TOC_ROWS 10
static int s_filter;
static bool s_recent_sort;
static shelf_entry_t* s_shelf;
static size_t s_shelf_capacity;
static int s_count,s_visible_count;
static char s_message[128],s_shelf_warning[128],s_storage[128];
static bool s_pending_invalidated,test_oom,test_degraded;
static unsigned s_store_revision;
typedef struct {char path[288];bool is_flash;} book_store_root_t;
typedef struct {uint32_t file_size;uint16_t chapter;uint32_t byte_off;uint8_t px,pct;uint32_t last_open_s;} book_progress_t;
typedef struct book_progress_watch {char path[288];atomic_bool invalidated;struct book_progress_watch* next;} book_progress_watch_t;
static book_progress_watch_t* test_watches;
static book_progress_watch_t* book_progress_watch_create(const char* path){book_progress_watch_t* w=calloc(1,sizeof(*w));assert(w);strcpy(w->path,path);atomic_init(&w->invalidated,false);w->next=test_watches;test_watches=w;return w;}
static bool book_progress_watch_invalidated(const book_progress_watch_t* w){return atomic_load(&w->invalidated);}
static void book_progress_watch_destroy(book_progress_watch_t* w){book_progress_watch_t** p=&test_watches;while(*p!=w)p=&(*p)->next;*p=w->next;free(w);}
static book_store_root_t test_roots[2];
static int test_root_count=1;
static void* heap_caps_realloc(void* p,size_t n,int caps){(void)caps;return test_oom?NULL:realloc(p,n);}
static void* heap_caps_malloc(size_t n,int caps){(void)caps;return malloc(n);}
static bool book_store_flash_ready(void){return true;}
static bool book_store_roots_degraded(void){return test_degraded;}
static uint64_t book_store_free_bytes(const book_store_root_t* root){(void)root;return 1000000;}
static int book_store_roots(book_store_root_t out[2],int* n){*n=test_root_count;memcpy(out,test_roots,sizeof(test_roots));return 0;}
static bool book_progress_load(const char* p,uint32_t n,book_progress_t* out){(void)p;(void)n;*out=(book_progress_t){0};return false;}
static void loading(app_ctx_t* ctx,const char* text){(void)ctx;(void)text;}
typedef struct pending_progress {char path[288];book_progress_t value;bool dirty,progress_saved;book_progress_watch_t* watch;struct pending_progress* next;} pending_progress_t;
static pending_progress_t* s_pending;
typedef struct delete_retry {shelf_entry_t entry;struct delete_retry* next;} delete_retry_t;
static delete_retry_t* s_delete_retries;
static char s_latest_path[288],test_last_path[288];
static bool s_save_failed;
static char s_path[288], s_resume_path[288];
#define BOOK_STORE_PATH_MAX 288
static unsigned test_open_calls;
static char test_open_path[288];
static bool open_book(app_ctx_t* ctx,const char* path){(void)ctx;test_open_calls++;strcpy(test_open_path,path);return true;}
static char* s_text;
static int s_unsaved,s_px=48;
static uint32_t s_file_size=1000;
static size_t s_chapter,s_page;
typedef struct {char* image_src;bool image_repeated;uint16_t image_first_chapter;bool image_title;} blk_t;
static blk_t* s_blocks;
static size_t s_block_count;
static bool s_image_open;
static uint8_t* s_image_pixels;
static uint16_t s_image_width,s_image_height;
static size_t s_image_block=SIZE_MAX;
static const char* s_image_error;
static char s_image_origin[224];
static int test_image_loads,test_image_error;
static int book_chapter_load_image(size_t chapter,const char* ref,uint8_t** pixels,uint16_t* width,uint16_t* height){(void)chapter;assert(ref);++test_image_loads;*pixels=NULL;*width=*height=0;if(test_image_error)return test_image_error;*pixels=malloc(4);assert(*pixels);*width=*height=2;return ESP_OK;}
static int book_chapter_title(size_t chapter,char* out,size_t cap){snprintf(out,cap,"Fixture chapter %zu",chapter);return ESP_OK;}
static void lock_draw(void){}
static void unlock_draw(void){}
static void loading_detail(app_ctx_t* ctx,const char* text,const char* detail){(void)ctx;(void)text;(void)detail;}
static void free_image(void);
static int test_save_error,test_last_error,test_save_calls,test_last_calls;
static size_t book_layout_page_count(void){return 3;}
static size_t book_layout_page_start_offset(size_t page){return page*100;}
static unsigned percent(size_t page){return (unsigned)page*20;}
static int book_progress_save(const char* p,const book_progress_t* value){(void)p;(void)value;test_save_calls++;return test_save_error;}
static int book_progress_set_last_path(const char* p){test_last_calls++;if(!test_last_error)snprintf(test_last_path,sizeof(test_last_path),"%s",p);return test_last_error;}
static void save_progress(void);
static void invalidate_prep(void){}
static shelf_entry_t s_managed;
static bool s_delete_confirm,s_file_removed,s_clear_confirm;
static char s_manage_message[128];
typedef enum {SHELF,READING,TOC,MANAGE,BULK,SEARCH} book_view_t;
static book_view_t s_view,s_search_parent;
#define UI_KEY_1 1
#define UI_KEY_2 2
#define UI_KEY_3 3
static int s_pressed_control;
static bool s_scan_pending,s_toolbar;
static bool s_resume_pending,s_full,s_shake_enabled;
static int s_du_count;
static int64_t s_size_settle_ms,s_poll_ms;
static void* s_prep_task,*s_prep_done,*s_draw_lock,*s_next_fb;
static void ensure_prep(void){}
static int app_settings_book_px(void){return 48;}
static bool app_settings_book_shake(void){return false;}
static void read_pico_sd_start_probe(void){}
static void sensor_set(app_ctx_t* ctx,bool on){(void)ctx;(void)on;}
static void vTaskDelete(void* p){(void)p;}
static void vSemaphoreDelete(void* p){(void)p;}
static unsigned test_turns;
static app_redraw_t turn_page(app_ctx_t* ctx,int dir){(void)ctx;(void)dir;++test_turns;return APP_REDRAW_NONE;}
typedef enum {UI_GESTURE_PRESS,UI_GESTURE_TAP,UI_GESTURE_LONG_PRESS,UI_GESTURE_SWIPE_L,UI_GESTURE_SWIPE_R,UI_GESTURE_SWIPE_U,UI_GESTURE_SWIPE_D,UI_GESTURE_CANCEL} ui_gesture_type_t;
typedef struct {ui_gesture_type_t type;uint16_t x0,y0,x,y;} ui_gesture_event_t;
static EpdRect body_rect(void){return(EpdRect){40,24,604,1000};}
static int control_at(app_ctx_t* ctx,uint16_t x,uint16_t y,EpdRect* rect){(void)ctx;*rect=(EpdRect){48,100,588,88};return ui_rect_hit(*rect,x,y)?1000:-1;}
static app_redraw_t open_image(app_ctx_t* ctx,size_t block);
static app_redraw_t action_at(app_ctx_t* ctx,uint16_t x,uint16_t y){(void)x;(void)y;return open_image(ctx,0);}
static app_redraw_t paint_control(app_ctx_t* ctx,EpdRect rect){(void)ctx;(void)rect;return APP_REDRAW_AREA;}
static int test_delete_error,test_forget_error,test_notify_count,test_delete_calls;
static bool test_removed,test_mixed;
static int book_store_delete(const char* path,bool* removed){(void)path;test_delete_calls++;*removed=test_removed;return test_mixed ? (strstr(path,"book001") ? -1 : 0) : test_delete_error;}
static int book_progress_forget(const char* path){for(book_progress_watch_t* w=test_watches;w;w=w->next)if(!strcmp(w->path,path))atomic_store(&w->invalidated,true);return test_forget_error;}
static void book_store_notify_changed(void){test_notify_count++;}
static unsigned book_store_revision(void){return (unsigned)test_notify_count;}
static void free_book(void){s_text=NULL;s_path[0]=0;free_image();}
static char test_wrapped[512];
#define UI_PX_CAPTION 28
#define UI_MARGIN 40
#define EPD_DRAW_ALIGN_LEFT 0
static int ui_content_width(void){return 604;}
static int ttf_text_width_px(int px,const char* text){int width=0;for(;*text;text++)if(((unsigned char)*text&0xc0)!=0x80)width+=px;return width;}
static void ui_text(uint8_t* fb,int x,int y,int px,const char* text,int align,bool inv){(void)fb;(void)x;(void)y;(void)px;(void)align;(void)inv;assert(strlen(test_wrapped)+strlen(text)<sizeof(test_wrapped));strcat(test_wrapped,text);}
'''
for name in ("copy_text", "shelf_matches", "compare_books", "sort_shelf", "shelf_reserve", "delete_retry_find", "delete_retry_reserve", "delete_retry_discard", "scan_shelf",
             "pending_find", "pending_reserve", "pending_restore", "pending_discard", "pending_mark_latest", "pending_drop_invalidated", "pending_flush", "save_progress", "retry_progress", "layout_name", "manage_panel", "manage_rect", "batch_rect", "leaves", "selected_count", "clear_selection", "toggle_selection", "select_page", "search_keys", "search_begin", "refresh_search_matches", "search_finish", "search_action", "refresh_capacity", "manage_apply", "manage_action", "batch_apply", "batch_action", "resume_choice", "free_image", "close_image", "open_image", "gesture_event", "on_key", "draw_wrapped_name", "on_enter", "book_on_exit"):
    unit += function(name) + "\n"
unit += r'''
int main(void) {
    app_ctx_t image_ctx={0};
    blk_t image_blocks[]={{.image_src="first.png",.image_repeated=true,.image_first_chapter=2},{.image_src="second.jpg"}};
    s_blocks=image_blocks;s_block_count=2;s_chapter=9;s_page=6;s_view=READING;
    ui_gesture_event_t image_event={.type=UI_GESTURE_PRESS,.x0=300,.y0=150,.x=300,.y=150};
    assert(gesture_event(&image_ctx,&image_event)==APP_REDRAW_NONE && test_image_loads==0);
    image_event.type=UI_GESTURE_SWIPE_L;image_event.x=236;
    gesture_event(&image_ctx,&image_event);assert(test_turns==1 && test_image_loads==0 && !s_image_open);
    image_event.type=UI_GESTURE_TAP;image_event.x=300;
    assert(gesture_event(&image_ctx,&image_event)==APP_REDRAW_PAGE && test_image_loads==1 && s_image_open);
    assert(s_image_pixels && strstr(s_image_origin,"第 3 节") && s_page==6 && s_chapter==9);
    image_event.type=UI_GESTURE_SWIPE_L;image_event.x=236;
    assert(gesture_event(&image_ctx,&image_event)==APP_REDRAW_NONE && test_turns==1 && s_image_open);
    assert(on_key(&image_ctx,UI_KEY_3)==APP_REDRAW_PAGE && !s_image_open && test_turns==1 && s_page==6);
    assert(open_image(&image_ctx,0)==APP_REDRAW_PAGE && test_image_loads==1);
    image_event.type=UI_GESTURE_TAP;image_event.x=300;
    assert(gesture_event(&image_ctx,&image_event)==APP_REDRAW_PAGE && !s_image_open && s_page==6);
    test_image_error=ESP_ERR_NOT_FOUND;
    assert(open_image(&image_ctx,1)==APP_REDRAW_PAGE && s_image_open && !s_image_pixels && s_image_error && test_image_loads==2);
    assert(on_key(&image_ctx,UI_KEY_1)==APP_REDRAW_PAGE && !s_image_open && test_turns==1);
    test_image_error=0;assert(open_image(&image_ctx,1)==APP_REDRAW_PAGE && s_image_pixels && test_image_loads==3);
    free_image();assert(!s_image_open && !s_image_pixels && s_image_block==SIZE_MAX);
    image_blocks[0].image_title=true;
    assert(open_image(&image_ctx,0)==APP_REDRAW_PAGE && !s_image_origin[0]);
    free_image();
    s_view=SHELF;
    app_ctx_t resume_ctx={0};
    strcpy(s_resume_path,"/sdcard/books/large.epub");
    assert(on_key(&resume_ctx,UI_KEY_1)==APP_REDRAW_PAGE && test_open_calls==0 && !s_resume_path[0]);
    strcpy(s_resume_path,"/sdcard/books/large.epub");
    assert(on_key(&resume_ctx,UI_KEY_3)==APP_REDRAW_PAGE && test_open_calls==1 && !s_resume_path[0]);
    assert(!strcmp(test_open_path,"/sdcard/books/large.epub"));
    assert(resume_choice(&resume_ctx,true)==APP_REDRAW_NONE && test_open_calls==1);
    strcpy(s_resume_path,"/sdcard/books/large.epub");
    assert(on_key(&resume_ctx,UI_KEY_2)==APP_REDRAW_PAGE && resume_ctx.request_menu && !s_resume_path[0] && test_open_calls==1);

    shelf_entry_t a={.search_match=true,.name="Same.txt",.path="/sdcard/books/A/Same.txt"};
    shelf_entry_t b={.search_match=true,.name="Same.txt",.path="/sdcard/books/B/Same.txt"};
    assert(compare_books(&a,&b)<0);
    b.recent=10;s_recent_sort=true;assert(compare_books(&a,&b)>0);
    b.is_flash=true;s_filter=1;assert(compare_books(&a,&b)<0);
    s_filter=2;assert(compare_books(&a,&b)>0);
    s_filter=0;s_recent_sort=false;
    snprintf(test_roots[0].path,sizeof(test_roots[0].path),"/tmp/book-ui-%d",(int)getpid());
    assert(mkdir(test_roots[0].path,0700)==0);
    for(int i=0;i<65;i++){char path[340];snprintf(path,sizeof(path),"%s/book%03d.txt",test_roots[0].path,i);FILE* f=fopen(path,"w");assert(f);fputs("x",f);fclose(f);}
    app_ctx_t ctx={0};scan_shelf(&ctx);
    assert(s_count==65&&s_visible_count==65&&s_shelf_capacity>=65);
    ctx.leaf=3;s_view=MANAGE;s_clear_confirm=false;s_file_removed=false;
    EpdRect back=manage_rect(0,3);manage_action(&ctx,back.x+1,back.y+1);
    assert(s_view==SHELF&&ctx.leaf==3);
    assert(!strcmp(s_shelf[0].name,"book000.txt")&&!strcmp(s_shelf[64].name,"book064.txt"));
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
    strcpy(s_managed.name,"Short.txt");assert(manage_panel().height<500);
    char long_name[256];memset(long_name,'W',255);long_name[255]=0;test_wrapped[0]=0;
    assert(draw_wrapped_name(NULL,long_name,176)+104<870);assert(!strcmp(test_wrapped,long_name));
    for(int i=0;i<80;i++){memcpy(long_name+i*3,"书",3);}long_name[240]=0;test_wrapped[0]=0;
    assert(draw_wrapped_name(NULL,long_name,176)+104<870);assert(!strcmp(test_wrapped,long_name));
    scan_shelf(&ctx);s_view=BULK;toggle_selection(0);toggle_selection(14);assert(selected_count()==2);
    s_recent_sort=true;sort_shelf(&ctx);assert(selected_count()==2);select_page(1);assert(selected_count()==9);
    ctx.leaf=2;strcpy(s_query,"book");search_begin();strcpy(s_search_draft,"bad");search_finish(&ctx,false);
    assert(ctx.leaf==2&&!strcmp(s_query,"book")&&selected_count()==9);
    search_begin();assert(on_key(&ctx,UI_KEY_3)==APP_REDRAW_NONE&&ctx.leaf==2);assert(on_key(&ctx,UI_KEY_1)==APP_REDRAW_PAGE&&ctx.leaf==2&&s_view==BULK);
    s_batch_confirm=true;assert(on_key(&ctx,UI_KEY_3)==APP_REDRAW_NONE&&ctx.leaf==2);assert(on_key(&ctx,UI_KEY_1)==APP_REDRAW_PAGE&&!s_batch_confirm&&ctx.leaf==2);
    search_begin();strcpy(s_search_draft,"book001");search_finish(&ctx,true);assert(!selected_count()&&s_visible_count==1);
    search_begin();memset(s_search_draft,'x',64);s_search_draft[64]=0;search_action(&ctx,0);assert(strlen(s_search_draft)==64);search_action(&ctx,41);assert(strlen(s_search_draft)==63);search_action(&ctx,42);assert(!s_search_draft[0]);search_action(&ctx,43);
    strcpy(s_query,"");refresh_search_matches();sort_shelf(&ctx);clear_selection();s_view=BULK;toggle_selection(0);toggle_selection(1);
    s_batch_confirm=true;calls=test_delete_calls;EpdRect bc=ui_row_rect(0,2,620,UI_BTN_H);batch_action(&ctx,bc.x+1,bc.y+1);assert(!s_batch_confirm&&test_delete_calls==calls);
    s_batch_delete=true;test_removed=true;test_delete_error=-1;batch_apply(&ctx);assert(selected_count()==2);
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
    scan_shelf(&ctx);clear_selection();toggle_selection(0);toggle_selection(1);s_batch_delete=true;
    batch_apply(&ctx);assert(selected_count()==2&&s_delete_retries);
    for(int i=0;i<s_count;i++)if(s_shelf[i].removed)assert(unlink(s_shelf[i].path)==0);
    book_on_exit(&ctx);on_enter(&ctx);scan_shelf(&ctx);
    int retry_count=0;
    for(int i=0;i<s_count;i++)if(s_shelf[i].removed){s_shelf[i].selected=true;++retry_count;}
    assert(retry_count==2);calls=test_delete_calls;batch_apply(&ctx);
    assert(!s_delete_retries&&test_delete_calls==calls&&!selected_count());
    free(s_shelf);
    while(s_pending)pending_discard(s_pending->path);
    for(int i=0;i<65;i++){char path[340];snprintf(path,sizeof(path),"%s/book%03d.txt",test_roots[0].path,i);unlink(path);}rmdir(test_roots[0].path);
    puts("book_ui_host_test: PASS");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / "test.c"
    exe = Path(tmp) / "test"
    c.write_text(unit, encoding="utf-8")
    subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-variable", "-fsanitize=address,undefined", str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
