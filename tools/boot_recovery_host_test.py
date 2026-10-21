"""中文：真实启动恢复页的确认、取消、缺卡和删除失败回归。
English: Exercise confirmation, cancellation, absent media and delete failures in the actual recovery page.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import ROOT, function
page = ROOT/'main/apps/app_boot_recovery.c'
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <errno.h>
#define PICO_BOOT_PATH_MAX 288
#define BOOK_STORE_ROOT_MAX 3
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_INVALID_ARG 3
typedef int esp_err_t;
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {char path[128];bool is_flash;} book_store_root_t;
typedef struct {bool present,mounted;} read_pico_sd_info_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_PAGE} app_redraw_t;
typedef struct {const void *request_app;} app_ctx_t;
typedef struct {int type,x0,y0;} ui_gesture_event_t;
enum {UI_GESTURE_TAP,UI_GESTURE_MOVE};
static char s_path[288],s_message[128],recorded_path[288]="/sdcard/books/bad.epub";
static bool s_confirm,s_removed,recorded=true,card_ready=true,flash_ready=true;
static bool exists=true,backup_exists=true,regular=true,backup_regular=true,unlink_fail,stat_fail,progress_fail;
static unsigned deletes,backup_deletes,forgets,changes;
static int home;
static const void *app_home_page(void){return &home;}
static int ui_nav_hit(int x,int y){(void)x;return y>1096?0:-1;}
static void ui_nav_request(app_ctx_t *ctx,int tab){assert(tab==0);ctx->request_app=&home;}
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&y>=r.y&&x<r.x+r.width&&y<r.y+r.height;}
static bool pico_boot_interrupted_book(char *out,size_t cap){if(!recorded||strlen(recorded_path)>=cap)return false;strcpy(out,recorded_path);return true;}
static void pico_boot_forget_interrupted_book(void){recorded=false;}
static esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *sd){sd->present=sd->mounted=card_ready;return card_ready?ESP_OK:ESP_ERR_INVALID_STATE;}
static bool book_store_flash_ready(void){return flash_ready;}
static esp_err_t book_store_roots(book_store_root_t *out,int *n){(void)out;*n=0;return ESP_FAIL;}
static void book_store_notify_changed(void){++changes;}
static esp_err_t book_progress_forget(const char *path){assert(!strcmp(path,s_path));++forgets;return progress_fail?ESP_FAIL:ESP_OK;}
static int test_stat(const char *path,struct stat *st){if(stat_fail){errno=EIO;return -1;}bool backup=strstr(path,".rename-backup")!=NULL;if(!(backup?backup_exists:exists)){errno=ENOENT;return -1;}memset(st,0,sizeof(*st));st->st_mode=(backup?backup_regular:regular)?S_IFREG:S_IFDIR;return 0;}
static int test_unlink(const char *path){if(unlink_fail){errno=EIO;return -1;}if(strstr(path,".rename-backup")){++backup_deletes;backup_exists=false;}else{assert(!strcmp(path,s_path));++deletes;exists=false;}return 0;}
#define lstat test_stat
#define unlink test_unlink
'''
for name in ('delete_book','on_enter','on_gesture','on_key'):
    unit += function(name,page)+'\n'
unit += r'''
static void reset(app_ctx_t *ctx){recorded=card_ready=flash_ready=exists=backup_exists=regular=backup_regular=true;unlink_fail=stat_fail=progress_fail=false;deletes=backup_deletes=forgets=changes=0;ctx->request_app=NULL;strcpy(recorded_path,"/sdcard/books/bad.epub");on_enter(ctx);}
static void tap(app_ctx_t *ctx,int x,int y){ui_gesture_event_t ev={UI_GESTURE_TAP,x,y};on_gesture(ctx,&ev);}
int main(void){
    app_ctx_t ctx={0};reset(&ctx);assert(!s_confirm&&!s_removed&&!deletes&&!strcmp(s_path,recorded_path));
    tap(&ctx,340,850);assert(s_confirm&&!deletes&&!backup_deletes);
    tap(&ctx,340,750);assert(!s_confirm&&!deletes&&!ctx.request_app);
    tap(&ctx,340,750);assert(ctx.request_app==&home);
    reset(&ctx);tap(&ctx,340,850);tap(&ctx,340,850);assert(s_removed&&!s_confirm&&deletes==1&&backup_deletes==1&&forgets==1&&changes==1&&!recorded);
    tap(&ctx,340,850);assert(deletes==1);on_key(&ctx,1);assert(ctx.request_app==&home);
    reset(&ctx);card_ready=false;tap(&ctx,340,850);tap(&ctx,340,850);assert(!s_removed&&s_confirm&&!deletes&&recorded);card_ready=true;tap(&ctx,340,850);assert(s_removed&&deletes==1);
    reset(&ctx);unlink_fail=true;tap(&ctx,340,850);tap(&ctx,340,850);assert(!s_removed&&!deletes&&!forgets&&recorded);unlink_fail=false;tap(&ctx,340,850);assert(s_removed);
    reset(&ctx);regular=false;assert(delete_book()==ESP_ERR_INVALID_ARG&&!deletes&&!backup_deletes);
    reset(&ctx);backup_regular=false;assert(delete_book()==ESP_FAIL&&!deletes&&!backup_deletes);
    reset(&ctx);stat_fail=true;assert(delete_book()==ESP_FAIL&&!deletes&&!forgets);
    reset(&ctx);strcpy(s_path,"/sdcard/books/other.epub");assert(delete_book()==ESP_ERR_INVALID_STATE&&!deletes);
    reset(&ctx);exists=backup_exists=false;assert(delete_book()==ESP_OK&&s_removed&&!deletes&&forgets==1);
    reset(&ctx);progress_fail=true;assert(delete_book()==ESP_FAIL&&s_removed&&deletes==1);
    reset(&ctx);strcpy(recorded_path,"/flash/books/bad.epub");on_enter(&ctx);flash_ready=false;assert(delete_book()==ESP_ERR_INVALID_STATE&&!deletes);flash_ready=true;assert(delete_book()==ESP_OK&&s_removed);
    reset(&ctx);ui_gesture_event_t ev={UI_GESTURE_MOVE,340,850};assert(on_gesture(&ctx,&ev)==APP_REDRAW_NONE&&!s_confirm&&!deletes);
    puts("PASS: recovery page never deletes on first tap; cancellation/Home remain usable; exact-file guard; absent card retry; directory/backup refusal; I/O failure; missing file; progress cleanup; internal-flash gate");
}
'''
with tempfile.TemporaryDirectory(dir=ROOT/'build') as directory:
    c,exe=Path(directory)/'test.c',Path(directory)/'test'
    c.write_text(unit)
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
