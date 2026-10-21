"""Exercise actual finished-cover disk cache and shelf reuse without JPEG/gray work.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
ROOT=Path(__file__).resolve().parents[1]
cover=ROOT/'main/book/book_cover.c';book=ROOT/'main/apps/app_book.c'
s=cover.read_text()
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#define BOOK_COVER_W 176
#define BOOK_COVER_H 240
#define BOOK_AUTO_COVER_VERSION 1
static const char *root;
static bool write_fail;
static void map(const char *p,char out[1024]){assert(!strncmp(p,"/sdcard",7));assert(snprintf(out,1024,"%s%s",root,p+7)<1024);}
static FILE *mapped_open(const char *p,const char *m){char out[1024];map(p,out);return write_fail&&strchr(m,'w')?NULL:fopen(out,m);}
static int mapped_stat(const char *p,struct stat *s){char out[1024];map(p,out);return stat(out,s);}
static int mapped_mkdir(const char *p,int mode){char out[1024];map(p,out);return mkdir(out,mode);}
static int mapped_rename(const char *p,const char *q){char a[1024],b[1024];map(p,a);map(q,b);return rename(a,b);}
static int mapped_remove(const char *p){char a[1024];map(p,a);return remove(a);}
#define fopen mapped_open
#define stat(p,s) mapped_stat(p,s)
#define mkdir mapped_mkdir
#define rename mapped_rename
#define remove mapped_remove
static bool book_title_from_path(const char *p,char *out,size_t cap){(void)p;snprintf(out,cap,"canonical");return true;}
typedef struct {unsigned width,height,stride,contrast,phase;} book_fast_cover_key_t;
'''
unit+=s[s.index('#define COVER_CACHE_MAGIC'):s.index('static uint64_t cover_hash')]
unit+=s[s.index('#define FAST_COVER_REVISION'):s.index('static uint64_t fast_cover_checksum')]
for n in ('cover_hash','cover_cache_key','fast_cover_checksum','fast_cover_key','book_cover_fast_cache_read','fast_cache_write','book_cover_fast_cache_write','book_cover_fast_cache_write_checked'):
 unit+=function(n,cover)+'\n'
unit+=r'''
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define BOOK_GRID_ROWS 9
#define SHELF_FAST_COVER_STRIDE 21
#define SHELF_FAST_COVER_BYTES 4494
#define SHELF_FAST_CACHE_RESERVE (512u*1024u)
enum {SHELF,MANAGE};
typedef struct {int x,y,width,height;} EpdRect;
static int s_view=SHELF,style=2,contrast=100;
static bool fast=true,allowed=true,oom;
static size_t psram=4*1024*1024,live;
static struct {char path[288],name[32],author[32];} s_shelf[9];
static struct {uint8_t *fast_bits;uint8_t fast_contrast,fast_phase;bool fast_white_edge;} s_covers[9];
static bool app_settings_main_fast_refresh(void){return fast;}
static bool pico_boot_asset_allowed(const char *p){(void)p;return allowed;}
static unsigned app_settings_system_contrast(void){return contrast;}
static EpdRect shelf_cover_image(int row){return style==5?(EpdRect){71,244+row*220,120,156}:(EpdRect){50+(row%3)*212,224+(row/3)*282,164,214};}
static size_t heap_caps_get_free_size(unsigned caps){assert(caps==3);return psram;}
static void *heap_caps_calloc(size_t n,size_t size,unsigned caps){assert(caps==3&&n==1&&size==4494);if(oom)return NULL;void *p=calloc(n,size);if(p)live+=size;return p;}
static void tracked_free(void *p){if(p){assert(live>=4494);live-=4494;}free(p);}
#define free tracked_free
'''
for n in ('load_shelf_cover_fast','save_shelf_cover_fast'):unit+=function(n,book)+'\n'
unit+=r'''
int main(int argc,char **argv){
 assert(argc==2);root=argv[1];const char *path="/sdcard/books/a.epub";
 strcpy(s_shelf[0].path,path);strcpy(s_shelf[0].name,"name");strcpy(s_shelf[0].author,"author");
 uint8_t bits[4494],out[4494+16];for(int i=0;i<4494;++i)bits[i]=(uint8_t)(i*31+7);
 book_fast_cover_key_t key={164,214,21,100,2};bool edge=false;
 assert(!book_cover_fast_cache_read(path,"name","author",key,out,&edge));
 book_cover_fast_cache_write(path,"name","author",key,bits,true);
 memset(out,0xaa,sizeof(out));assert(book_cover_fast_cache_read(path,"name","author",key,out,&edge)&&edge&&!memcmp(bits,out,4494));
 for(int i=4494;i<4510;++i)assert(out[i]==0xaa);
 for(int attempt=0;attempt<80;++attempt){assert(load_shelf_cover_fast(0,0)&&live==4494);assert(!memcmp(bits,s_covers[0].fast_bits,4494));free(s_covers[0].fast_bits);s_covers[0].fast_bits=NULL;}
 assert(!live);
 book_fast_cover_key_t changed=key;changed.contrast=110;assert(!book_cover_fast_cache_read(path,"name","author",changed,out,&edge));
 changed=key;changed.phase=3;assert(!book_cover_fast_cache_read(path,"name","author",changed,out,&edge));
 changed=key;changed.width=120;changed.height=156;assert(!book_cover_fast_cache_read(path,"name","author",changed,out,&edge));
 assert(!book_cover_fast_cache_read(path,"name","other author",key,out,&edge));
 char cache[120];fast_cover_header_t h;assert(fast_cover_key(path,"name","author",key,cache,&h));
 uint8_t stale[4494];memset(stale,0,sizeof(stale));
 book_cover_fast_cache_write_checked(path,"name","author",key,stale,false,(int64_t)h.source.file_size+1,h.source.modified);
 assert(book_cover_fast_cache_read(path,"name","author",key,out,&edge)&&!memcmp(out,bits,4494));
 FILE *f=fopen(cache,"r+b");assert(f&&fseek(f,sizeof(h)+8,SEEK_SET)==0);int byte=fgetc(f);fseek(f,-1,SEEK_CUR);assert(fputc(byte^1,f)!=EOF);fclose(f);
 assert(!book_cover_fast_cache_read(path,"name","author",key,out,&edge)&&!load_shelf_cover_fast(0,0)&&!live);
 book_cover_fast_cache_write(path,"name","author",key,bits,true);assert(load_shelf_cover_fast(0,0));
 save_shelf_cover_fast(0,0);free(s_covers[0].fast_bits);s_covers[0].fast_bits=NULL;
 f=fopen(cache,"ab");assert(f&&fputc(1,f)!=EOF);fclose(f);assert(!load_shelf_cover_fast(0,0)&&!live);
 book_cover_fast_cache_write(path,"name","author",key,bits,true);
 f=fopen(path,"ab");assert(f&&fputc(2,f)!=EOF);fclose(f);assert(!load_shelf_cover_fast(0,0)&&!live);
 book_cover_fast_cache_write(path,"name","author",key,bits,false);assert(load_shelf_cover_fast(0,0)&&!s_covers[0].fast_white_edge);free(s_covers[0].fast_bits);s_covers[0].fast_bits=NULL;
 oom=true;assert(!load_shelf_cover_fast(0,0)&&!live);oom=false;
 psram=512*1024;assert(!load_shelf_cover_fast(0,0)&&!live);psram=4*1024*1024;
 allowed=false;assert(!load_shelf_cover_fast(0,0)&&!live);allowed=true;
 fast=false;assert(!load_shelf_cover_fast(0,0));fast=true;s_view=MANAGE;assert(!load_shelf_cover_fast(0,0));s_view=SHELF;
 style=5;key=(book_fast_cover_key_t){120,156,21,100,3};book_cover_fast_cache_write(path,"name","author",key,bits,false);assert(load_shelf_cover_fast(0,0));assert(!memcmp(bits,s_covers[0].fast_bits,21*156));for(int i=21*156;i<4494;++i)assert(!s_covers[0].fast_bits[i]);free(s_covers[0].fast_bits);s_covers[0].fast_bits=NULL;
 // 首页尺寸使用独立固定文件，与列表/网格缓存切换互不覆盖。
 // Home geometry has its own fixed file and cannot overwrite list/grid cached pixels.
 book_fast_cover_key_t home={172,250,22,100,0};uint8_t home_bits[5500],home_out[5500];
 for(unsigned i=0;i<sizeof(home_bits);++i)home_bits[i]=(uint8_t)(i*7+19);
 book_cover_fast_cache_write(path,"name","author",home,home_bits,false);
 assert(book_cover_fast_cache_read(path,"name","author",home,home_out,&edge)&&!memcmp(home_bits,home_out,sizeof(home_bits)));
 assert(book_cover_fast_cache_read(path,"name","author",key,out,&edge)&&!memcmp(bits,out,21*156));
 char home_cache[120];fast_cover_header_t home_header;assert(fast_cover_key(path,"name","author",home,home_cache,&home_header)&&strcmp(home_cache,cache));
 home.height=251;assert(!book_cover_fast_cache_read(path,"name","author",home,home_out,&edge));
 key.width=200;book_cover_fast_cache_write(path,"name","author",key,bits,true);assert(!book_cover_fast_cache_read(path,"name","author",key,out,&edge));
 write_fail=true;key=(book_fast_cover_key_t){164,214,21,100,2};book_cover_fast_cache_write(path,"name","author",key,bits,true);write_fail=false;
 remove(path);assert(!load_shelf_cover_fast(0,0)&&!live);
 puts("PASS: real TF raster reuse (80 reads, no gray/JPEG workspace); grid/list exact pixels and one 4494B resident representation; source/contrast/phase/dimension invalidation; CRC, trailing bytes, reserve/OOM, blocked book, mode isolation, write failure and missing source");
}
'''
with tempfile.TemporaryDirectory(dir=ROOT/'build') as directory:
 d=Path(directory);(d/'books').mkdir();(d/'books/a.epub').write_bytes(b'book');(d/'test.c').write_text(unit)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(d/'test.c'),'-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test'),str(d)],check=True)
