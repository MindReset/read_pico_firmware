"""Exercise actual Home cache permutation and mode ownership with checked frees.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import re
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]

def function(name, path):
    text = path.read_text()
    scan = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"', lambda m: ' ' * len(m[0]), text)
    start = re.search(r'^(?:static )?(?:[A-Za-z_]\w*(?:\s+|\s*\*\s*))+' + name + r'\([^;{}]*?\)\s*\{', scan, re.M)
    assert start, name
    at, depth = start.end(), 1
    while depth:
        depth += (scan[at] == '{') - (scan[at] == '}')
        at += 1
    return text[start.start():at]

if __name__ == '__main__':
    home = ROOT / 'main/apps/app_dashboard.c'
    source = home.read_text()
    types = source[source.index('typedef struct {'):source.index('static int s_cover_next;')]
    unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "ui_image_dither.h"
#include "book_cover_jobs.h"
#define BOOK_STORE_PATH_MAX 288
#define BOOK_COVER_W 176
#define BOOK_COVER_H 240
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
typedef struct {int x,y,width,height;} EpdRect;

typedef struct {int unused;} book_progress_t;
static uint8_t *s_folder;static uint32_t s_folder_key;static bool s_folder_attempted;
static bool fast, alloc_fail, asset_allowed=true, disk_hit, corrupt_disk;
static unsigned loads,disk_reads,disk_writes;
static size_t psram=4*1024*1024;
static size_t live_bytes;
static struct {void *ptr;size_t bytes;} live[24];
static void *remember(void *p,size_t bytes){if(!p)return NULL;for(int i=0;i<24;++i)if(!live[i].ptr){live[i].ptr=p;live[i].bytes=bytes;live_bytes+=bytes;return p;}assert(0);return NULL;}
static void tracked_free(void *p){if(!p)return;for(int i=0;i<24;++i)if(live[i].ptr==p){live_bytes-=live[i].bytes;live[i].ptr=NULL;free(p);return;}assert(0);}
#define free tracked_free
static void *heap_caps_malloc(size_t n,int caps){assert(caps==3);return alloc_fail?NULL:remember(malloc(n),n);}
static void *heap_caps_calloc(size_t n,size_t bytes,int caps){assert(caps==3);return alloc_fail?NULL:remember(calloc(n,bytes),n*bytes);}
static size_t heap_caps_get_free_size(int caps){assert(caps==3);return psram-live_bytes;}

static uint8_t disk_bits[5280];
bool book_cover_fast_cache_read(const char*p,const char*t,const char*a,book_fast_cover_key_t k,uint8_t*out,bool*edge){(void)p;(void)t;(void)a;assert(k.width==176&&k.height==240&&k.stride==22&&k.contrast==100&&k.phase==17);++disk_reads;if(corrupt_disk){memset(out,255,5280);return false;}if(!disk_hit)return false;memcpy(out,disk_bits,5280);*edge=false;return true;}
void book_cover_fast_cache_write(const char*p,const char*t,const char*a,book_fast_cover_key_t k,const uint8_t*out,bool edge){(void)p;(void)t;(void)a;assert(k.width==176&&k.height==240&&!edge);memcpy(disk_bits,out,5280);++disk_writes;}
static bool pico_boot_asset_allowed(const char *path){(void)path;return asset_allowed;}
static bool app_settings_main_fast_refresh(void){return fast;}
static uint8_t app_settings_system_contrast(void){return 100;}
static uint8_t sample(int p){return (uint8_t)(p*19+31);}
// out 写成数组形式以匹配 book_cover.h 的声明：形式不一致会被 -Werror=array-parameter 拦下。
// Spell out as an array to match book_cover.h; a mismatched form trips -Werror=array-parameter.
bool book_cover_load_gray(const char *path,const char *title,const char *author,uint8_t out[BOOK_COVER_W*BOOK_COVER_H],bool decode,bool *pending){(void)path;(void)title;(void)author;assert(decode&&!pending);++loads;for(int i=0;i<176*240;++i)out[i]=sample(i);return true;}
bool book_cover_load_list_gray(const char *p,const char *t,const char *a,uint8_t out[BOOK_COVER_W*BOOK_COVER_H],bool decode,bool *pending,unsigned *width,unsigned *height){*width=176;*height=240;return book_cover_load_gray(p,t,a,out,decode,pending);}
static int missing=-1, modified=-1;
static int test_stat(const char *path,struct stat *st){int id=-1;assert(sscanf(path,"/book%d.epub",&id)==1);if(id==missing)return -1;memset(st,0,sizeof(*st));st->st_mode=S_IFREG;st->st_size=100+id;st->st_mtime=200+id+(id==modified);return 0;}
#define stat(path,out) test_stat(path,out)
'''+types+'\nstatic int s_cover_next;\n'
    unit+=r"""
static unsigned s_cover_generation;static bool s_cover_waiting;
static book_cover_job_t job_requests[9];
unsigned book_cover_jobs_begin(void){return 1;}
bool book_cover_jobs_submit(unsigned generation,unsigned slot,const book_cover_job_t *q){assert(generation==1&&slot<9);job_requests[slot]=*q;return true;}
int book_cover_jobs_take(unsigned generation,unsigned slot,book_cover_result_t *result){
 (void)generation;book_cover_job_t *q=&job_requests[slot];struct stat st;
 if(stat(q->path,&st)||heap_caps_get_free_size(3)<512u*1024u+42240u)return -1;
 uint8_t *gray=NULL,*bits=NULL;bool edge=false;
 book_fast_cover_key_t key={176,240,22,100,17};
 if(q->fast){bits=heap_caps_calloc(5280,1,3);if(!bits)return -1;
  if(book_cover_fast_cache_read(q->path,q->title,q->author,key,bits,&edge))goto ready;memset(bits,0,5280);}
 gray=heap_caps_malloc(42240,3);if(!gray){free(bits);return -1;}
 unsigned w,h;assert(book_cover_load_list_gray(q->path,q->title,q->author,gray,true,NULL,&w,&h));
 if(q->fast){for(unsigned y=0;y<240;++y)for(unsigned x=0;x<176;++x){int v=128+((int)gray[y*176+x]-128);v=v<0?0:v>255?255:v;
  if(ui_image_dither_bw((uint8_t)v&0xf0,x,y))bits[y*22+x/8]|=1u<<(x&7);}
  book_cover_fast_cache_write(q->path,q->title,q->author,key,bits,false);free(gray);gray=NULL;}
ready:
 *result=(book_cover_result_t){.pixels=q->fast?bits:gray,.width=176,.height=240,.fast=q->fast,.size=st.st_size,.modified=st.st_mtime};return 1;
}
"""
    unit += function('ui_contrast_gray', ROOT/'main/ui/ui_kit.c')+'\n'
    unit += function('book_cover_crop', ROOT/'main/book/book_cover.c')+'\n'
    for name in ('book_at','clear_cover_cache','app_home_cover_mode_changed','restore_cover_cache','load_cover'):
        unit += function(name, home)+'\n'
    unit += r'''
static void book(int slot,int id){home_book_t *b=book_at(slot);memset(b,0,sizeof(*b));if(id<0)return;snprintf(b->path,sizeof(b->path),"/book%d.epub",id);snprintf(b->title,sizeof(b->title),"title%d",id);strcpy(b->author,"author");}
static void seed(void){clear_cover_cache();for(int i=0;i<7;++i){book(i,i);assert(load_cover(book_at(i),i));}assert(live_bytes==7u*176*240);}
int main(void){
    unsigned rng=1;
    for(int attempt=0;attempt<300;++attempt){
        seed();int ids[]={0,1,2,3,4,5,6};void *original[7];for(int i=0;i<7;++i)original[i]=s_cover_cache[i].pixels;
        for(int i=6;i>0;--i){rng=rng*1664525u+1013904223u;unsigned j=rng%(i+1);int t=ids[i];ids[i]=ids[j];ids[j]=t;}
        for(int i=0;i<7;++i)book(i,ids[i]);restore_cover_cache();
        for(int i=0;i<7;++i){assert(book_at(i)->cover==original[ids[i]]);assert(s_cover_cache[i].pixels==book_at(i)->cover);assert(!strcmp(book_at(i)->path,s_cover_cache[i].path));}
        assert(live_bytes==7u*176*240);
    }
    seed();book(0,5);book(1,5);book(2,-1);book(3,2);book(4,0);book(5,-1);book(6,8);restore_cover_cache();
    assert(book_at(0)->cover&&!book_at(1)->cover&&!book_at(2)->cover&&book_at(3)->cover&&book_at(4)->cover&&!book_at(5)->cover&&!book_at(6)->cover);
    assert(live_bytes==3u*176*240);
    seed();missing=2;modified=3;strcpy(book_at(4)->author,"changed");restore_cover_cache();assert(!book_at(2)->cover&&!book_at(3)->cover&&!book_at(4)->cover&&live_bytes==4u*176*240);
    missing=modified=-1;app_home_cover_mode_changed();assert(!live_bytes);
    fast=true;book(0,0);assert(load_cover(book_at(0),0));assert(live_bytes==5280&&book_at(0)->cover_fast);
    book_crop_t crop=book_cover_crop(176,240,176,240);
    for(int y=0;y<240;++y)for(int x=0;x<176;++x){unsigned sx=crop.x+(uint64_t)(unsigned)x*crop.width/176,sy=crop.y+(uint64_t)(unsigned)y*crop.height/240;assert(!!(book_at(0)->cover[y*22+x/8]&(1u<<(x&7)))==!!ui_image_dither_bw(ui_contrast_gray(sample(sy*176+sx)),36+x,292+y));}
    restore_cover_cache();assert(live_bytes==5280&&book_at(0)->cover_fast);
    unsigned before=loads, writes=disk_writes;
    disk_hit=true;
    for(int i=0;i<80;++i){app_home_cover_mode_changed();assert(load_cover(book_at(0),0));assert(loads==before&&disk_writes==writes&&live_bytes==5280&&!memcmp(disk_bits,book_at(0)->cover,5280));}
    app_home_cover_mode_changed();corrupt_disk=true;
    assert(load_cover(book_at(0),0)&&loads==before+1&&live_bytes==5280&&disk_writes==writes+1);
    for(int y=0;y<240;++y)for(int x=0;x<176;++x){unsigned sx=crop.x+(uint64_t)(unsigned)x*crop.width/176,sy=crop.y+(uint64_t)(unsigned)y*crop.height/240;assert(!!(book_at(0)->cover[y*22+x/8]&(1u<<(x&7)))==!!ui_image_dither_bw(ui_contrast_gray(sample(sy*176+sx)),36+x,292+y));}
    corrupt_disk=false;app_home_cover_mode_changed();psram=512*1024;
    before=loads;assert(!load_cover(book_at(0),0)&&!live_bytes&&loads==before);psram=4*1024*1024;
    fast=false;app_home_cover_mode_changed();assert(!live_bytes&&!book_at(0)->cover);assert(load_cover(book_at(0),0)&&live_bytes==176u*240&&!book_at(0)->cover_fast);
    app_home_cover_mode_changed();alloc_fail=true;assert(!load_cover(book_at(0),0)&&!live_bytes);alloc_fail=false;asset_allowed=false;before=loads;assert(!load_cover(book_at(0),0)&&loads==before);
    puts("PASS: 300 actual in-place Home reorderings; duplicate/missing/modified identities; no stale pointer or double free; fast 5,280-byte BW only; gray-only ordinary/ripple; checked OOM and recovery");
}
'''
    with tempfile.TemporaryDirectory(dir=ROOT/'build') as folder:
        c, exe = Path(folder)/'test.c',Path(folder)/'test'
        c.write_text(unit)
        subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-error=misleading-indentation','-fsanitize=address,undefined','-I'+str(ROOT/'main/ui'),'-I'+str(ROOT/'main/book'),str(c),str(ROOT/'main/ui/ui_image_dither.c'),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
