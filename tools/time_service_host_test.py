#!/usr/bin/env python3
"""中文：真实 RTC 恢复、后台校准与 SNTP 所有权回归。/ English: Actual RTC restore, scheduler and SNTP ownership regressions.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import subprocess, tempfile
from home_cover_cache_host_test import function
root=Path(__file__).resolve().parents[1]
common=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <time.h>
#include <sys/time.h>
#define ESP_OK 0
#define ESP_FAIL 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_INVALID_ARG 3
#define ESP_ERR_TIMEOUT 4
#define ESP_ERR_INVALID_RESPONSE 5
#define pdMS_TO_TICKS(ms) (ms)
#define pdPASS 1
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define portENTER_CRITICAL(...) ((void)0)
#define portEXIT_CRITICAL(...) ((void)0)
typedef int esp_err_t;
static int64_t mono;
static time_t wall=1800000000;
static int64_t esp_timer_get_time(void){return mono;}
#define time(...) wall
static void vTaskDelay(int ms){mono+=(int64_t)ms*1000;}
'''
restore=common+r'''
#define PMU_CMD_TIME_GET 7
typedef struct {uint32_t unix_sec;uint16_t unix_millis;bool time_ok,time_synced;} pmu_snapshot_t;
static pmu_snapshot_t pmu;
static struct timeval current,assigned;
static int reads,writes,pmu_error;
static esp_err_t read_pico_pmu_cmd(int cmd,void *p,size_t n){(void)cmd;(void)p;(void)n;++reads;return pmu_error;}
static const pmu_snapshot_t *read_pico_pmu_get(void){return &pmu;}
static int fake_gettimeofday(struct timeval *tv,void *zone){(void)zone;*tv=current;return 0;}
static int fake_settimeofday(const struct timeval *tv,void *zone){(void)zone;assigned=*tv;++writes;return 0;}
#define gettimeofday fake_gettimeofday
#define settimeofday fake_settimeofday
'''+function('restore_time_from_pmu',root/'main/app_main.c')+r'''
int main(void){
 pmu=(pmu_snapshot_t){1800000000,987,true,true};restore_time_from_pmu();
 assert(reads==1&&writes==1&&assigned.tv_sec==1800000000&&assigned.tv_usec==987000);
 current=(struct timeval){1800000001,123456};restore_time_from_pmu();assert(reads==1&&writes==1);
 current.tv_sec=0;pmu.unix_millis=1000;restore_time_from_pmu();assert(writes==1);
 pmu.unix_millis=0;pmu.time_ok=false;restore_time_from_pmu();assert(writes==1);
 pmu.time_ok=true;pmu.time_synced=false;restore_time_from_pmu();assert(writes==1);
 pmu.time_synced=true;pmu.unix_sec=1;restore_time_from_pmu();assert(writes==1);
 pmu.unix_sec=1800000000;pmu_error=ESP_FAIL;restore_time_from_pmu();assert(writes==1);
 puts("PASS: retained S3 time survives reset; cold boot uses fresh PMU milliseconds; invalid/unsynced PMU cannot overwrite it");
}
'''
scheduler=common+r'''
typedef int nvs_handle_t;
enum {READ_PICO_TRANSFER_STOPPED,READ_PICO_TRANSFER_STARTING,READ_PICO_TRANSFER_READY,READ_PICO_TRANSFER_UPLOADING,READ_PICO_TRANSFER_ERROR};
enum {READ_PICO_TRANSFER_MODE_AP,READ_PICO_TRANSFER_MODE_STA};
typedef struct {int state,mode;bool network_ready;} read_pico_transfer_status_t;
static read_pico_transfer_status_t network;
static uint64_t stored;
static uint32_t pending;
static int jobs,task_ok=1,pmu_syncs,saves,worker_result,credential_reads;
static bool creds=true;
static void (*job)(void *);
static int nvs_open(const char *ns,int mode,int *h){(void)mode;assert(!strcmp(ns,"kiiko_clock"));*h=1;return ESP_OK;}
static int nvs_get_u64(int h,const char *k,uint64_t *v){(void)h;(void)k;*v=stored;return ESP_OK;}
static int nvs_set_u64(int h,const char *k,uint64_t v){(void)h;(void)k;stored=v;++saves;return ESP_OK;}
static int nvs_commit(int h){(void)h;return ESP_OK;}
static void nvs_close(int h){(void)h;}
static bool read_pico_transfer_take_time_sync(uint32_t *v){*v=pending;pending=0;return *v!=0;}
static void read_pico_transfer_get_status(read_pico_transfer_status_t *s){*s=network;}
static int read_pico_transfer_get_saved_wifi(char *s,bool *b){*s=0;++credential_reads;*b=creds;return ESP_OK;}
static int read_pico_transfer_sync_time_auto(uint32_t *v){*v=wall;if(!worker_result)pending=wall;return worker_result;}
static int app_sync_time_to_pmu(void){++pmu_syncs;return ESP_OK;}
static int xTaskCreate(void (*fn)(void*),const char *n,int stack,void *arg,int pri,void *handle){
 (void)n;(void)arg;(void)handle;assert(stack==6144&&pri==1);job=fn;if(task_ok)++jobs;return task_ok;
}
static void vTaskDelete(void *t){(void)t;}
'''
scheduler+='\n'.join(x for x in (root/'main/time_service.c').read_text().splitlines() if not x.startswith('#include'))
scheduler+=r'''
static void reset(void){
 s_loaded=false;s_last_sync=0;s_next_attempt_us=15000000;atomic_store(&s_running,false);
 stored=pending=jobs=pmu_syncs=saves=credential_reads=worker_result=0;task_ok=1;creds=true;mono=15000000;
 network=(read_pico_transfer_status_t){READ_PICO_TRANSFER_STOPPED,READ_PICO_TRANSFER_MODE_STA,false};
}
int main(void){
 reset();app_time_service_poll(false);assert(!jobs&&!s_loaded);app_time_service_poll(true);assert(jobs==1);
 app_time_service_poll(true);assert(jobs==1);job(NULL);assert(!atomic_load(&s_running));
 app_time_service_poll(true);assert(pmu_syncs==1&&saves==1&&stored==(uint64_t)wall&&jobs==1);
 mono+=3600000000LL;wall+=3600;app_time_service_poll(true);assert(jobs==1); // offline waits6h
 network=(read_pico_transfer_status_t){READ_PICO_TRANSFER_READY,READ_PICO_TRANSFER_MODE_STA,true};
 app_time_service_poll(true);assert(jobs==2);job(NULL);app_time_service_poll(true);assert(pmu_syncs==2);
 reset();stored=wall-21600;app_time_service_poll(true);assert(jobs==1); // overdue offline
 reset();stored=wall-21599;app_time_service_poll(true);assert(!jobs);
 for(int state=1;state<=4;++state){reset();network.state=state;network.mode=READ_PICO_TRANSFER_MODE_AP;network.network_ready=true;app_time_service_poll(true);assert(!jobs);}
 reset();creds=false;app_time_service_poll(true);assert(!jobs&&credential_reads==1);app_time_service_poll(true);assert(credential_reads==1);
 reset();task_ok=0;app_time_service_poll(true);assert(!jobs&&!atomic_load(&s_running));
 task_ok=1;app_time_service_poll(true);assert(!jobs);mono+=CLOCK_RETRY_US;app_time_service_poll(true);assert(jobs==1);
 reset();worker_result=ESP_FAIL;app_time_service_poll(true);job(NULL);app_time_service_poll(true);assert(!pmu_syncs&&!saves&&jobs==1);
 pending=wall;app_time_service_poll(false);assert(pending);app_time_service_poll(true);assert(!pending&&pmu_syncs==1&&saves==1);
 puts("PASS: actual scheduler hourly online/6h offline, idle gating, single temporary worker, AP/upload exclusion, no-credential/OOM/error backoff and real-sync-only PMU/NVS handoff");
}
'''
lease=common+r'''
static bool s_config_busy,s_time_active,s_time_automatic,s_time_cancel,s_stopping;
static uint32_t s_time_pending_utc;
enum {READ_PICO_TRANSFER_STOPPED,READ_PICO_TRANSFER_READY};
static struct {int state;} s_status;
static int impl_result,impl_calls,cancel_reads;
static bool cancel_time_sync(bool automatic_only){++cancel_reads;return !s_time_active||(!automatic_only||s_time_automatic);}
static esp_err_t sync_time_impl(uint32_t *utc){assert(s_time_active&&s_config_busy);++impl_calls;*utc=wall;return impl_result;}
static esp_err_t sync_time_online_impl(uint32_t *utc){return sync_time_impl(utc);}
'''+function('sync_time_run',root/'components/read_pico_transfer/read_pico_transfer.c')+r'''
int main(void){uint32_t utc;
 assert(sync_time_run(NULL,false,true)==ESP_ERR_INVALID_ARG);
 assert(sync_time_run(&utc,false,true)==ESP_OK&&impl_calls==1&&s_time_pending_utc==wall);
 assert(!s_time_active&&!s_config_busy);
 s_config_busy=true;assert(sync_time_run(&utc,false,true)==ESP_ERR_INVALID_STATE&&impl_calls==1);s_config_busy=false;
 s_time_active=true;assert(sync_time_run(&utc,true,true)==ESP_ERR_INVALID_STATE);assert(sync_time_run(&utc,false,false)==ESP_ERR_INVALID_STATE);s_time_active=false;
 s_time_pending_utc=0;impl_result=ESP_FAIL;assert(sync_time_run(&utc,true,false)==ESP_FAIL&&!s_time_pending_utc&&!s_time_active&&!s_config_busy);
 s_stopping=true;s_status.state=READ_PICO_TRANSFER_READY;assert(sync_time_run(&utc,false,true)==ESP_ERR_INVALID_STATE);
 s_status.state=READ_PICO_TRANSFER_STOPPED;assert(sync_time_run(&utc,false,true)==ESP_FAIL);
 puts("PASS: SNTP/config share one admission lock; active operations cannot be reentered, failed work publishes no success, stopped network can calibrate");
}
'''
ntp=common+r'''
static bool s_time_cancel;
static int inits,deinits,waits,cancel_at,success_at,init_error;
typedef struct {int count;const char *servers[3];} esp_sntp_config_t;
#define ESP_SNTP_SERVER_LIST(...) {__VA_ARGS__}
#define ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(n,list) {.count=n,.servers=list}
static bool time_sync_cancelled(void){return s_time_cancel;}
static int esp_netif_sntp_init(esp_sntp_config_t *c){assert(c->count==3&&!strcmp(c->servers[0],"ntp.aliyun.com"));++inits;return init_error;}
static int esp_netif_sntp_sync_wait(int ms){mono+=ms*1000;++waits;if(waits==cancel_at)s_time_cancel=true;return waits==success_at?ESP_OK:ESP_ERR_TIMEOUT;}
static void esp_netif_sntp_deinit(void){++deinits;}
'''+function('time_sync_ntp',root/'components/read_pico_transfer/read_pico_transfer.c')+r'''
static void reset(void){mono=0;s_time_cancel=false;inits=deinits=waits=cancel_at=success_at=init_error=0;}
int main(void){uint32_t utc=0;
 reset();success_at=2;assert(time_sync_ntp(&utc)==ESP_OK&&utc==wall&&inits==1&&deinits==1&&waits==2);
 reset();s_time_cancel=true;assert(time_sync_ntp(&utc)==ESP_ERR_INVALID_STATE&&!inits&&!deinits);
 reset();cancel_at=2;assert(time_sync_ntp(&utc)==ESP_ERR_INVALID_STATE&&deinits==1&&mono==200000);
 reset();assert(time_sync_ntp(&utc)==ESP_ERR_TIMEOUT&&deinits==1&&mono==12000000);
 reset();init_error=ESP_FAIL;assert(time_sync_ntp(&utc)==ESP_FAIL&&!deinits&&!waits);
 reset();success_at=1;wall=1;assert(time_sync_ntp(&utc)==ESP_ERR_INVALID_RESPONSE&&deinits==1);
 puts("PASS: actual SNTP helper has one init/deinit, valid UTC only,100ms cancellation checkpoints and12s bounded timeout; failed init never tears down an unowned singleton");
}
'''
with tempfile.TemporaryDirectory(prefix='kiiko-clock-test-',dir='/private/tmp') as folder:
 for name,unit in [('restore',restore),('scheduler',scheduler),('lease',lease),('ntp',ntp)]:
  c=Path(folder)/(name+'.c');binary=Path(folder)/name;c.write_text(unit)
  subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-g','-fsanitize=address,undefined',str(c),'-o',str(binary)],check=True)
  subprocess.run([str(binary)],check=True)
