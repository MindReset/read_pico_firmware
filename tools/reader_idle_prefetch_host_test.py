#!/usr/bin/env python3
"""中文：真实线程验证推屏重叠预绘、逐行交还与连续翻页复用。/ English: Real-thread checks of overlapped prefetch, row handoff and rapid-turn reuse.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
root=Path(__file__).resolve().parents[1];book=root/'main/apps/app_book.c'
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>
#include <time.h>
typedef struct{int x,y,width,height;}EpdRect;
typedef struct{bool touched;}touch_t;
typedef struct{bool consumed,request_app,request_menu,request_return;touch_t *touch;}app_ctx_t;
typedef struct{uint32_t generation;size_t page,offset;bool started;}book_layout_draw_cursor_t;
enum{READING,SHELF,READER_PANEL_NONE,READER_PANEL_FONT};
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define BOOK_PREP_RESERVE (1024u*1024u)
#define BOOK_PREP_IDLE_US 4000
#define portMAX_DELAY 999999
#define pdTRUE 1
#define pdPASS 1
typedef struct{pthread_mutex_t lock;pthread_cond_t cond;unsigned value;}sem_t;
typedef sem_t* SemaphoreHandle_t;
typedef struct{pthread_t thread;sem_t notify;void(*run)(void*);bool quit;}task_t;
typedef task_t* TaskHandle_t;
static _Thread_local task_t *current_task;
static bool fail_task,fail_sem;
static void sem_init(sem_t *s,unsigned value){pthread_mutex_init(&s->lock,NULL);pthread_cond_init(&s->cond,NULL);s->value=value;}
static SemaphoreHandle_t semaphore(unsigned value){if(fail_sem)return NULL;sem_t *s=malloc(sizeof(*s));assert(s);sem_init(s,value);return s;}
static SemaphoreHandle_t xSemaphoreCreateMutex(void){return semaphore(1);}
static SemaphoreHandle_t xSemaphoreCreateBinary(void){return semaphore(0);}
static int xSemaphoreTake(sem_t *s,int timeout){pthread_mutex_lock(&s->lock);while(!s->value&&timeout)pthread_cond_wait(&s->cond,&s->lock);bool ok=s->value!=0;if(ok)--s->value;pthread_mutex_unlock(&s->lock);return ok;}
static void xSemaphoreGive(sem_t *s){pthread_mutex_lock(&s->lock);++s->value;pthread_cond_signal(&s->cond);pthread_mutex_unlock(&s->lock);}
static void vSemaphoreDelete(sem_t *s){pthread_cond_destroy(&s->cond);pthread_mutex_destroy(&s->lock);free(s);}
static unsigned ulTaskNotifyTake(int clear,int timeout){assert(clear==1&&timeout==portMAX_DELAY);task_t *t=current_task;pthread_mutex_lock(&t->notify.lock);while(!t->notify.value&&!t->quit)pthread_cond_wait(&t->notify.cond,&t->notify.lock);if(t->quit){pthread_mutex_unlock(&t->notify.lock);pthread_exit(NULL);}unsigned n=t->notify.value;t->notify.value=0;pthread_mutex_unlock(&t->notify.lock);return n;}
static void *task_start(void *p){current_task=p;current_task->run(NULL);return NULL;}
static int xTaskCreatePinnedToCore(void(*run)(void*),const char *name,int stack,void *arg,int priority,task_t **out,int core){assert(!strcmp(name,"book_prep")&&stack==12288&&!arg&&priority==3&&core==1);if(fail_task)return 0;task_t *t=calloc(1,sizeof(*t));assert(t);sem_init(&t->notify,0);t->run=run;*out=t;assert(!pthread_create(&t->thread,NULL,task_start,t));return pdPASS;}
static void xTaskNotifyGive(task_t *task){xSemaphoreGive(&task->notify);}
static void vTaskDelete(task_t *task){pthread_mutex_lock(&task->notify.lock);task->quit=true;pthread_cond_signal(&task->notify.cond);pthread_mutex_unlock(&task->notify.lock);pthread_join(task->thread,NULL);pthread_mutex_destroy(&task->notify.lock);pthread_cond_destroy(&task->notify.cond);free(task);}
static int s_view=READING,s_reader_panel=READER_PANEL_NONE;static const char *s_text="text";
static bool s_toolbar,s_clear_confirm;
static size_t s_page;
static int s_next_page=-1,s_prep_page=-1;
static uint32_t s_next_generation,generation=1;
static uint8_t *s_next_fb;
static book_layout_draw_cursor_t s_prep_cursor;
static SemaphoreHandle_t s_draw_lock,s_prep_done;
static TaskHandle_t s_prep_task;
static atomic_bool s_prep_stop;
static unsigned work,allocs;static bool fail_alloc,fail_draw,wallpaper,images;
static size_t free_space=4u*1024u*1024u;
static size_t fb_bytes(void){return 128;}
static size_t book_layout_page_count(void){return 500;}
static unsigned book_layout_page_image_count(size_t page){(void)page;return images;}
static uint32_t book_layout_generation(void){return generation;}
static const char *app_settings_book_background_path(void){return wallpaper?"wallpaper":"";}
static EpdRect body_rect(void){return(EpdRect){1,2,3,4};}
static size_t heap_caps_get_free_size(int caps){assert(caps==3);return free_space;}
static void *heap_caps_aligned_alloc(size_t align,size_t n,int caps){assert(align==16&&caps==3);++allocs;return fail_alloc?NULL:malloc(n);}
static atomic_llong fake_us;
static int64_t esp_timer_get_time(void){return atomic_fetch_add(&fake_us,1000);}
static pthread_mutex_t row_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t row_cond=PTHREAD_COND_INITIALIZER;
static bool gate_rows;
static unsigned row_entries,row_permits;
static int book_layout_draw_slice(uint8_t *fb,size_t page,EpdRect area,int px,book_layout_draw_cursor_t *cursor){
 (void)area;assert(px==48&&(page==s_page+1||page==s_page));if(fail_draw)return -1;
 if(cursor->generation&&cursor->generation!=generation)return -1;
 if(cursor->offset==36)return 1;
 pthread_mutex_lock(&row_lock);++row_entries;pthread_cond_broadcast(&row_cond);
 while(gate_rows&&!row_permits)pthread_cond_wait(&row_cond,&row_lock);
 if(gate_rows)--row_permits;
 pthread_mutex_unlock(&row_lock);
 cursor->generation=generation;cursor->page=page;cursor->started=true;
 assert(cursor->offset<36);fb[cursor->offset]=(uint8_t)(page+cursor->offset);++cursor->offset;++work;
 return cursor->offset==36;
}
static int s_px=48;
'''
for name in ['lock_draw','unlock_draw','invalidate_prep','reader_prefetch_step','reader_prefetch_buffer','prep_task','kick_prep','reader_prefetch_end','reader_prefetch_resume','reader_prefetch_idle']:
 unit+=function(name,book)+'\n'
assert 'reader_prefetch_end(prep);' in function('present',book)
assert 'reader_prefetch_resume(s_page)' in function('paint_reading',book)
unit+=r'''
static void destroy_worker(void){if(s_prep_task){vTaskDelete(s_prep_task);s_prep_task=NULL;}if(s_prep_done){vSemaphoreDelete(s_prep_done);s_prep_done=NULL;}if(s_draw_lock){vSemaphoreDelete(s_draw_lock);s_draw_lock=NULL;}}
static void wait_row(unsigned count){pthread_mutex_lock(&row_lock);while(row_entries<count)pthread_cond_wait(&row_cond,&row_lock);pthread_mutex_unlock(&row_lock);}
static void *end_presentation(void *unused){(void)unused;reader_prefetch_end(true);return NULL;}
int main(void){touch_t touch={0};app_ctx_t ctx={.touch=&touch};
 free_space=BOOK_PREP_RESERVE;assert(!kick_prep()&&!s_next_fb&&!s_prep_task);
 free_space=4u*1024u*1024u;fail_alloc=true;assert(!kick_prep()&&!s_next_fb);fail_alloc=false;
 fail_task=true;assert(!kick_prep()&&s_next_fb&&!s_prep_task);fail_task=false;
 // 确定性地阻塞第二行，推屏结束只交还该行，不能等待整页。
 // Gate the second row: output completion hands back that row, never the rest of the page.
 pthread_mutex_lock(&row_lock);gate_rows=true;row_permits=1;pthread_mutex_unlock(&row_lock);
 assert(kick_prep());wait_row(2);assert(work==1&&s_next_page==-1);
 pthread_t end;assert(!pthread_create(&end,NULL,end_presentation,NULL));
 while(!atomic_load(&s_prep_stop)){}
 pthread_mutex_lock(&row_lock);row_permits=1;pthread_cond_broadcast(&row_cond);pthread_mutex_unlock(&row_lock);
 pthread_join(end,NULL);assert(work==2&&s_next_page==-1&&s_prep_cursor.offset==2);
 pthread_mutex_lock(&row_lock);gate_rows=false;pthread_mutex_unlock(&row_lock);
 // 连按命中部分预绘时只画余下34行，不能丢弃已有两行。
 // A rapid turn completes only 34 remaining rows, preserving the two rows already prepared.
 ++s_page;assert(reader_prefetch_resume(s_page));assert(work==36&&s_next_page==1);
 for(unsigned row=0;row<36;++row)assert(s_next_fb[row]==(uint8_t)(1+row));
 // 40次连续翻页：模拟每次同步推屏时让预绘完成，下一次无需绘制任何正文行。
 // Forty rapid turns: work overlaps each synchronous scan; the next turn draws no body rows.
 for(unsigned turn=0;turn<40;++turn){
  unsigned old=work;assert(kick_prep());assert(xSemaphoreTake(s_prep_done,portMAX_DELAY));xSemaphoreGive(s_prep_done);
  reader_prefetch_end(true);assert(work==old+36&&s_next_page==(int)s_page+1);
  ++s_page;old=work;assert(reader_prefetch_resume(s_page)&&work==old);
 }
 assert(kick_prep());reader_prefetch_end(true);
 unsigned old=work;ctx.consumed=true;reader_prefetch_idle(&ctx);assert(work==old);ctx.consumed=false;
 touch.touched=true;reader_prefetch_idle(&ctx);assert(work==old);touch.touched=false;
 ctx.request_app=true;reader_prefetch_idle(&ctx);assert(work==old);ctx.request_app=false;
 destroy_worker();invalidate_prep();fail_task=true;assert(!kick_prep());fail_task=false;
 old=work;reader_prefetch_idle(&ctx);assert(work>old&&work<=old+8&&s_next_page==-1);
 ++generation;assert(!reader_prefetch_resume(s_prep_page));invalidate_prep();
 fail_task=true;assert(!kick_prep());fail_task=false;fail_draw=true;reader_prefetch_idle(&ctx);assert(s_prep_page==-1&&s_next_page==-1);fail_draw=false;
 invalidate_prep();images=true;assert(!kick_prep());images=false;wallpaper=true;assert(!kick_prep());wallpaper=false;
 s_toolbar=true;assert(!kick_prep());s_toolbar=false;s_reader_panel=READER_PANEL_FONT;assert(!kick_prep());s_reader_panel=READER_PANEL_NONE;
 s_page=499;assert(!kick_prep());
 destroy_worker();free(s_next_fb);
 puts("PASS: real-thread scan overlap, row-boundary stop instead of whole-page join, partial-page continuation, 40 rapid cache hits, input/navigation priority and generation/OOM/image/background/panel guards");
}
'''
with tempfile.TemporaryDirectory(dir='/private/tmp') as d:
 c=Path(d)/'test.c';exe=Path(d)/'test';c.write_text(unit)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-pthread','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True,timeout=30)
