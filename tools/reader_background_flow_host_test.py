"""Exercise real picture catalogue, deferred previews and directory navigation.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import subprocess, tempfile
from home_cover_cache_host_test import function
ROOT=Path(__file__).resolve().parents[1]
book=ROOT/'main/apps/app_book.c'
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define READER_BACKGROUND_ROWS 7
typedef struct {int x,y,width,height;} EpdRect;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE} app_redraw_t;
enum {READING,SHELF};
enum {READER_PANEL_FONT_SETTINGS,READER_PANEL_BACKGROUND,READER_PANEL_BACKGROUND_PREVIEW};
typedef struct {char path[288];} reader_background_item_t;
static reader_background_item_t *s_backgrounds;
static int s_background_count,s_background_selected,s_background_page,s_view=READING,s_reader_panel;
static bool s_background_ready,s_background_prepare_pending,oom,decode_ok=true,save_ok=true;
static char s_background_notice[96],s_background_preview_notice[96],configured[288];
static const char *mount;
static unsigned decodes,invalidations;
static DIR *mock_opendir(const char *path){assert(!strcmp(path,"/sdcard/pictures"));return opendir(mount);}
static int mock_stat(const char *path,struct stat *st){
 assert(!strncmp(path,"/sdcard/pictures/",17));assert(path[17]!='.');
 char local[1024];snprintf(local,sizeof(local),"%s/%s",mount,path+17);return stat(local,st);
}
#define opendir mock_opendir
#define stat(p,s) mock_stat(p,s)
static void *heap_caps_calloc(size_t n,size_t size,int caps){assert(n==128&&size==288&&caps==3);return oom?NULL:calloc(n,size);}
static void copy_text(char *dst,size_t cap,const char *src){snprintf(dst,cap,"%s",src);}
static const char *app_settings_book_background_path(void){return configured;}
static bool app_settings_set_book_background_path(const char *path){if(!save_ok)return false;copy_text(configured,sizeof(configured),path);return true;}
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&y>=r.y&&x<r.x+r.width&&y<r.y+r.height;}
static void invalidate_prep(void){++invalidations;}
static bool ui_reader_background_prepare(const char *path){assert(strstr(path,"/sdcard/pictures/wallpaper-"));++decodes;return decode_ok;}
'''
for n in ('background_compare','reader_background_scan','reader_background_pages','reader_background_row','reader_background_preview','reader_background_step','reader_background_action','reader_background_tick'):
    unit+=function(n,book)+'\n'
unit+=r'''
int main(int argc,char **argv){
 assert(argc==3);mount=argv[1];strcpy(configured,"/sdcard/pictures/wallpaper-09.jpg");
 reader_background_scan();assert(s_background_count==16&&s_background_selected==9&&s_background_page==1&&decodes==0);
 for(int i=0;i<16;++i){char expected[288];snprintf(expected,sizeof(expected),"/sdcard/pictures/wallpaper-%02d.%s",i,i==15?"PNG":"jpg");assert(!strcmp(expected,s_backgrounds[i].path));}
 s_reader_panel=READER_PANEL_BACKGROUND;
 assert(reader_background_action(500,1060)==APP_REDRAW_PAGE&&s_background_page==2&&decodes==0);
 assert(reader_background_action(100,370)==APP_REDRAW_NONE&&s_reader_panel==READER_PANEL_BACKGROUND_PREVIEW&&s_background_selected==14&&decodes==0);
 assert(reader_background_tick()==APP_REDRAW_PAGE&&s_background_ready&&decodes==1);
 assert(reader_background_step(-1)==APP_REDRAW_NONE&&s_background_selected==13);
 assert(reader_background_tick()==APP_REDRAW_PAGE&&decodes==2);
 assert(reader_background_action(300,1130)==APP_REDRAW_PAGE&&s_reader_panel==READER_PANEL_BACKGROUND&&s_background_page==1);
 assert(strstr(configured,"09.jpg")&&!s_background_prepare_pending);
 assert(reader_background_action(100,370)==APP_REDRAW_NONE&&s_background_selected==7);
 assert(reader_background_action(60,250)==APP_REDRAW_PAGE&&reader_background_tick()==APP_REDRAW_NONE&&decodes==2);
 reader_background_preview(15);decode_ok=false;
 assert(reader_background_tick()==APP_REDRAW_PAGE&&!s_background_ready&&s_background_preview_notice[0]);
 assert(reader_background_action(500,1130)==APP_REDRAW_NONE&&strstr(configured,"09.jpg"));
 decode_ok=true;reader_background_preview(15);assert(reader_background_tick()==APP_REDRAW_PAGE);
 save_ok=false;assert(reader_background_action(500,1130)==APP_REDRAW_NONE&&s_reader_panel==READER_PANEL_BACKGROUND_PREVIEW);
 save_ok=true;assert(reader_background_action(500,1130)==APP_REDRAW_PAGE&&s_reader_panel==READER_PANEL_BACKGROUND&&strstr(configured,"15.PNG"));
 unsigned before=decodes;assert(reader_background_step(1)==APP_REDRAW_NONE&&decodes==before);
 assert(reader_background_action(100,1150)==APP_REDRAW_PAGE&&!configured[0]);
 assert(reader_background_action(500,1150)==APP_REDRAW_PAGE&&!s_backgrounds&&s_reader_panel==READER_PANEL_FONT_SETTINGS);
 mount=argv[2];reader_background_scan();assert(s_background_count==128&&s_background_notice[0]);
 oom=true;reader_background_scan();assert(!s_backgrounds&&s_background_count==0&&s_background_notice[0]);
 puts("PASS: sorted pictures catalogue, hidden AppleDouble files/directories/empty/unsupported files filtered; no directory decode; paging, deferred preview/swipes, back preserves directory and setting, apply/save/decode failure, cancellation, 128-file bound and OOM");
}
'''
with tempfile.TemporaryDirectory(dir=ROOT/'build') as directory:
    d=Path(directory); pictures=d/'pictures';pictures.mkdir();many=d/'many';many.mkdir()
    for i in range(16):(pictures/f'wallpaper-{i:02d}.{"PNG" if i==15 else "jpg"}').write_bytes(b'image')
    for name in ('._wallpaper-00.jpg','.hidden.png','.DS_Store','notes.txt'):(pictures/name).write_bytes(b'metadata')
    (pictures/'directory.png').mkdir();(pictures/'empty.jpg').touch()
    for i in range(129):(many/f'wallpaper-{i:03d}.jpg').write_bytes(b'image')
    c=d/'flow.c';exe=d/'flow';c.write_text(unit)
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe),str(pictures),str(many)],check=True)
