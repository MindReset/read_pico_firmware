#!/usr/bin/env python3
"""中文：运行真实头像任务的代次、非阻塞入口与生命周期。/ English: Execute actual avatar generation, nonblocking entry and media ownership.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
typedef enum {UI_AVATAR_UNAVAILABLE=-1,UI_AVATAR_PENDING,UI_AVATAR_READY} ui_avatar_state_t;
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
#define pdMS_TO_TICKS(x) (x)
#define pdPASS 1
typedef int portMUX_TYPE;
static int64_t mono;
static unsigned book_store_revision(void){return 0;}
static int jobs,decodes,allocations,fail_alloc,task_ok=1,fail_decode,mode;
static jmp_buf task_exit;
static void (*task_fn)(void *);
static void *heap_caps_malloc(size_t n,int caps){assert(caps==3);if(fail_alloc)return NULL;++allocations;return malloc(n);}
static void fake_free(void *p){if(p)--allocations;free(p);}
#define free fake_free
static int64_t esp_timer_get_time(void){return mono;}
static void vTaskDelay(int ms){mono+=(int64_t)ms*1000;}
static void vTaskDelete(void *arg){(void)arg;longjmp(task_exit,1);}
static int xTaskCreate(void (*fn)(void *),const char *name,int stack,void *arg,int pri,void *h){
 (void)name;(void)arg;(void)h;assert(stack==12288&&pri==1);task_fn=fn;if(task_ok)++jobs;return task_ok;
}
static bool ui_wallpaper_prepare_avatar(const char *path,uint8_t *out,int side);
'''
unit+='\n'.join(x for x in (root/'main/ui/ui_avatar.c').read_text().splitlines() if not x.startswith('#include'))
unit+=r'''
static bool ui_wallpaper_prepare_avatar(const char *path,uint8_t *out,int side){
 ++decodes;assert(side==60||side==82);memset(out,path[0]=='A'?60:82,(size_t)side*side);
 if(mode==1){mode=0;ui_avatar_request("B",82);} // switch page/path while decoder owns old input
 if(mode==2){mode=0;ui_avatar_invalidate();} // card removed during decode
 if(mode==3){mode=0;assert(!ui_avatar_quiesce(0));} // unmount must wait for its handles
 return !fail_decode;
}
static void run(void){if(!setjmp(task_exit))task_fn(NULL);}
int main(void){uint8_t pixels[82*82];
 ui_avatar_request("A",60);assert(jobs==1&&!decodes&&ui_avatar_poll("A",60,pixels)==UI_AVATAR_PENDING);
 ui_avatar_request("A",82);assert(jobs==1&&!decodes);run();assert(decodes==2&&!s_running);
 assert(ui_avatar_poll("A",60,pixels)==UI_AVATAR_READY&&pixels[0]==60);
 assert(ui_avatar_poll("A",82,pixels)==UI_AVATAR_READY&&pixels[0]==60);
 ui_avatar_request("A",60);assert(jobs==1&&decodes==2); // navigation reuses result, no IO/join
 ui_avatar_request("C",60);mode=1;run();assert(ui_avatar_poll("C",60,pixels)==UI_AVATAR_UNAVAILABLE);
 assert(ui_avatar_poll("B",82,pixels)==UI_AVATAR_READY&&pixels[0]==82);
 ui_avatar_request("C",60);mode=2;run();assert(ui_avatar_poll("C",60,pixels)==UI_AVATAR_UNAVAILABLE&&!s_running);
 ui_avatar_request("D",82);mode=3;run();assert(ui_avatar_quiesce(1)&&ui_avatar_poll("D",82,pixels)==UI_AVATAR_UNAVAILABLE);
 ui_avatar_request("E",60);fail_decode=1;run();assert(ui_avatar_poll("E",60,pixels)==UI_AVATAR_UNAVAILABLE);fail_decode=0;
 ui_avatar_request("F",82);fail_alloc=1;int old=decodes;run();assert(decodes==old&&ui_avatar_poll("F",82,pixels)==UI_AVATAR_UNAVAILABLE);fail_alloc=0;
 task_ok=0;ui_avatar_request("G",82);assert(!s_running&&ui_avatar_poll("G",82,pixels)==UI_AVATAR_UNAVAILABLE);task_ok=1;
 ui_avatar_request("H",60);int64_t began=mono;assert(!ui_avatar_quiesce(25)&&mono-began<=30000);run();assert(ui_avatar_quiesce(0));
 ui_avatar_request("",60);assert(!allocations);
 puts("PASS: real avatar entry/poll does no file IO or joining; single worker/two bounded sizes, fast repeat navigation, stale-page/media results discarded, unmount ownership and OOM/decode/task failures safe");
}
'''
with tempfile.TemporaryDirectory(prefix='kiiko-avatar-',dir='/private/tmp') as folder:
 c=Path(folder)/'test.c';binary=Path(folder)/'test';c.write_text(unit)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',str(c),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
