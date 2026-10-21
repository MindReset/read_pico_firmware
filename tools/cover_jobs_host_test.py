#!/usr/bin/env python3
"""中文：真实封面队列的所有权、页面代次、缓存直读与UI字体隔离。/ English: Execute real cover jobs, ownership, stale generations, direct packed hits and UI-only fonts.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
unit=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <setjmp.h>
#include <sys/stat.h>
#include "book_cover_jobs.h"
#include "ui_image_dither.h"
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
#define pdMS_TO_TICKS(x) (x)
#define pdPASS 1
typedef int portMUX_TYPE;
static int64_t mono;
static int jobs,decodes,fonts,writes,reads,mode,fail_alloc,task_ok=1,worker_context;
static unsigned replaced;
static size_t live,largest;
static struct{void *p;size_t n;} memory[64];
static void *heap_caps_malloc(size_t n,int caps){assert(caps==3);if(fail_alloc)return NULL;void *p=malloc(n);if(!p)return NULL;for(unsigned i=0;i<64;++i)if(!memory[i].p){memory[i].p=p;memory[i].n=n;live+=n;if(n>largest)largest=n;return p;}abort();}
static size_t heap_caps_get_free_size(int caps){assert(caps==3);return 8u*1024u*1024u-live;}
static void *heap_caps_calloc(size_t n,size_t size,int caps){void *p=heap_caps_malloc(n*size,caps);if(p)memset(p,0,n*size);return p;}
static void release(void *p){if(!p)return;for(unsigned i=0;i<64;++i)if(memory[i].p==p){live-=memory[i].n;memory[i].p=NULL;free(p);return;}assert(false);}
#define free release
static jmp_buf done;
static void (*task_fn)(void *);
static int xTaskCreate(void(*fn)(void *),const char *name,int stack,void *arg,int priority,void *handle){(void)name;(void)arg;(void)handle;assert(stack==12288&&priority==1);task_fn=fn;if(task_ok)++jobs;return task_ok;}
static void vTaskDelete(void *arg){(void)arg;longjmp(done,1);}
static void vTaskDelay(int ms){mono+=ms*1000;}
static int64_t esp_timer_get_time(void){return mono;}
static int mock_stat(const char *p,struct stat *st){assert(worker_context);++reads;if(*p=='X')return -1;memset(st,0,sizeof(*st));st->st_mode=S_IFREG;st->st_size=100;st->st_mtime=200;return 0;}
#define stat(p,s) mock_stat(p,s)
static book_cover_job_t request(const char *p){book_cover_job_t q={.width=164,.height=214,.stride=21,.contrast=100};strcpy(q.path,p);strcpy(q.title,"title");return q;}
static bool book_auto_cover_render(const char*,const char*,const char*,unsigned,unsigned,uint8_t*);
static bool book_title_from_path(const char *p,char *out,size_t cap){(void)p;(void)out;(void)cap;return false;}
'''
unit+='\n'.join(x for x in (root/'main/book/book_cover_jobs.c').read_text().splitlines() if not x.startswith('#include'))
unit+=r'''
book_crop_t book_cover_crop(unsigned sw,unsigned sh,unsigned w,unsigned h){(void)w;(void)h;return(book_crop_t){0,0,sw,sh};}
bool book_cover_list_cache_dimensions(const char *p,const char*t,const char*a,unsigned*w,unsigned*h){(void)t;(void)a;assert(worker_context);++reads;*w=80;*h=120;return *p=='P';}
bool book_cover_fast_cache_read(const char*p,const char*t,const char*a,book_fast_cover_key_t k,uint8_t*out,bool*edge){(void)t;(void)a;assert(worker_context);++reads;if(*p!='P')return false;assert(k.width==80&&k.height==120);memset(out,255,(size_t)k.stride*k.height);*edge=false;return true;}
void book_cover_fast_cache_write_checked(const char*p,const char*t,const char*a,book_fast_cover_key_t k,const uint8_t*out,bool edge,int64_t size,int64_t modified){(void)p;(void)t;(void)a;(void)k;(void)out;(void)edge;assert(size==100&&modified==200&&worker_context);++writes;}
void book_cover_store_generated(const char*p,const char*t,const char*a,const uint8_t*out,bool list,int64_t size,int64_t modified){(void)p;(void)t;(void)a;(void)out;(void)list;assert(size==100&&modified==200&&worker_context);++writes;}
int book_cover_load_io(const char*p,const char*t,const char*a,uint8_t*out,bool list,unsigned*w,unsigned*h,bool*cache){(void)t;(void)a;(void)list;assert(worker_context);++decodes;*w=176;*h=240;memset(out,*p,176*240);*cache=false;
 if(mode==1){mode=0;replaced=book_cover_jobs_begin();book_cover_job_t q=request("B");assert(book_cover_jobs_submit(replaced,0,&q));}
 if(mode==2){mode=0;assert(!book_cover_jobs_quiesce(0));}
 return *p=='N'?0:1;
}
static bool book_auto_cover_render(const char*p,const char*t,const char*a,unsigned w,unsigned h,uint8_t*out){(void)p;(void)t;(void)a;assert(!worker_context&&w==176&&h==240);++fonts;memset(out,33,w*h);return true;}
static void run(void){worker_context=1;if(!setjmp(done))task_fn(NULL);worker_context=0;}
int main(void){
 unsigned generation=book_cover_jobs_begin();book_cover_result_t result={0};
 book_cover_job_t q=request("A");assert(book_cover_jobs_submit(generation,0,&q));
 assert(!decodes&&!reads&&book_cover_jobs_take(generation,0,&result)==0);run();
 assert(book_cover_jobs_take(generation,0,&result)==1&&result.pixels[0]=='A'&&!result.fast);free(result.pixels);
 assert(book_cover_jobs_take(generation,0,&result)==-1);
 q=request("B");assert(!book_cover_jobs_submit(generation,0,&q));
 generation=book_cover_jobs_begin();q=request("P");q.list=q.natural=q.fast=true;largest=0;int before=decodes;
 assert(book_cover_jobs_submit(generation,0,&q));run();assert(decodes==before&&largest<42240);
 assert(book_cover_jobs_take(generation,0,&result)==1&&result.fast&&result.width==80&&result.height==120);free(result.pixels);
 for(int natural=0;natural<2;++natural){
  generation=book_cover_jobs_begin();q=request("A");q.fast=true;q.natural=natural;
  assert(book_cover_jobs_submit(generation,0,&q));run();assert(book_cover_jobs_take(generation,0,&result)==1&&result.fast);
  unsigned width=natural?176:164,height=natural?240:214,stride=natural?22:21;
  assert(result.width==width&&result.height==height);
  for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
    bool expected=natural?ui_image_dither_bw(64,x,y):ui_image_dither_cover_bw(68,x,y);
    assert(!!(result.pixels[y*stride+x/8]&(1u<<(x&7)))==expected);
  }free(result.pixels);
 }
 generation=book_cover_jobs_begin();q=request("A");assert(book_cover_jobs_submit(generation,0,&q));mode=1;run();
 assert(book_cover_jobs_take(generation,0,&result)==-1);assert(book_cover_jobs_take(replaced,0,&result)==1&&result.pixels[0]=='B');free(result.pixels);
 generation=book_cover_jobs_begin();q=request("N");assert(book_cover_jobs_submit(generation,0,&q));run();assert(!fonts);
 int old_writes=writes;assert(book_cover_jobs_take(generation,0,&result)==0&&fonts==1);run();
 assert(book_cover_jobs_take(generation,0,&result)==1&&result.pixels[0]==33&&writes==old_writes);free(result.pixels);
 generation=book_cover_jobs_begin();q=request("A");assert(book_cover_jobs_submit(generation,0,&q));mode=2;run();assert(book_cover_jobs_quiesce(1));
 generation=book_cover_jobs_begin();assert(book_cover_jobs_submit(generation,0,&q));fail_alloc=1;run();assert(book_cover_jobs_take(generation,0,&result)==-1);fail_alloc=0;
 generation=book_cover_jobs_begin();task_ok=0;assert(book_cover_jobs_submit(generation,0,&q));assert(book_cover_jobs_take(generation,0,&result)==-1);task_ok=1;
 generation=book_cover_jobs_begin();assert(book_cover_jobs_submit(generation,0,&q));assert(!book_cover_jobs_quiesce(20));run();assert(book_cover_jobs_quiesce(0));
 generation=book_cover_jobs_begin();for(unsigned slot=0;slot<9;++slot){q=request("A");assert(book_cover_jobs_submit(generation,slot,&q));}run();
 for(unsigned slot=0;slot<9;++slot){assert(book_cover_jobs_take(generation,slot,&result)==1);free(result.pixels);}
 assert(book_cover_jobs_quiesce(0));free(s_jobs);s_jobs=NULL;assert(!live);
 puts("PASS: real bounded cover worker has no UI IO on queue/poll, packed hits skip gray/decode, stale generations and media cancellation discard results, UI-only auto glyphs, transient fallback not cached, all9 slots and OOM/task failures release resources");
}
'''
with tempfile.TemporaryDirectory(prefix='kiiko-cover-jobs-',dir='/private/tmp') as folder:
 c=Path(folder)/'test.c';binary=Path(folder)/'test';c.write_text(unit)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined','-I'+str(root/'main/book'),'-I'+str(root/'main/ui'),str(c),str(root/'main/ui/ui_image_dither.c'),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
