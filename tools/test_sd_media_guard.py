#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Mock driver regression for the real SD lifecycle / 实际 SD 生命周期的模拟驱动回归。"""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
out = root / 'build/sd-media-test'
out.mkdir(parents=True, exist_ok=True)
for name in ('driver/sdmmc_host.h', 'esp_log.h', 'esp_vfs_fat.h',
             'freertos/FreeRTOS.h', 'freertos/task.h', 'read_pico_board.h',
             'sdmmc_cmd.h', 'esp_err.h'):
    p = out / name
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text('#pragma once\n', encoding='utf-8')
source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_TIMEOUT 3
#define ESP_ERR_NOT_FOUND 4
#define ESP_ERR_NOT_FINISHED 5
#define ESP_ERR_NO_MEM 6
#define GPIO_NUM_38 38
#define GPIO_NUM_42 42
#define GPIO_NUM_44 44
#define GPIO_NUM_NC -1
#define SDMMC_FREQ_HIGHSPEED 40000
#define SDMMC_SLOT_FLAG_INTERNAL_PULLUP 1
#define SDMMC_HOST_DEFAULT() ((sdmmc_host_t){0})
#define SDMMC_SLOT_CONFIG_DEFAULT() ((sdmmc_slot_config_t){0})
typedef struct { int max_freq_khz; } sdmmc_host_t;
typedef struct { int width,clk,cmd,d0,d1,d2,d3,cd,wp,flags; } sdmmc_slot_config_t;
typedef struct { bool format_if_mount_failed; int max_files,allocation_unit_size; } esp_vfs_fat_sdmmc_mount_config_t;
typedef struct { struct { char name[8]; } cid; struct { int capacity,sector_size; } csd; } sdmmc_card_t;
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define portENTER_CRITICAL(p) assert(pthread_mutex_lock(p)==0)
#define portEXIT_CRITICAL(p) assert(pthread_mutex_unlock(p)==0)
typedef int BaseType_t;
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
static bool present=true, drop_during_mount=false;
static int mounts, unmounts, formats;
static sdmmc_card_t mock_card={.cid={"MOCK"},.csd={2048,512}};
static void (*pending)(void*);
static bool read_pico_sd_present(void) { return present; }
static void vTaskDelay(int n) {(void)n;}
static void vTaskDelete(void* p) {(void)p;}
static int xTaskCreate(void (*f)(void*), const char* n,int z,void* a,int pr,void* h) {
    (void)n;(void)z;(void)a;(void)pr;(void)h;assert(!pending);pending=f;return pdPASS;
}
static int esp_vfs_fat_info(const char* p,uint64_t* total,uint64_t* freeb) {(void)p;*total=1048576;*freeb=524288;return 0;}
static int esp_vfs_fat_sdmmc_mount(const char* p,const sdmmc_host_t* h,const sdmmc_slot_config_t* s,const esp_vfs_fat_sdmmc_mount_config_t* c,sdmmc_card_t** card) {
    (void)p;(void)h;(void)s;assert(!c->format_if_mount_failed);mounts++;*card=&mock_card;if(drop_during_mount)present=false;return 0;
}
static int esp_vfs_fat_sdcard_unmount(const char* p,sdmmc_card_t* c){(void)p;assert(c==&mock_card);unmounts++;return 0;}
static int esp_vfs_fat_sdcard_format(const char* p,sdmmc_card_t* c){(void)p;(void)c;formats++;return 0;}
static int mock_mkdir(const char* p,int mode){(void)p;(void)mode;return 0;}
#define mkdir mock_mkdir
#include "../../components/read_pico/read_pico_sd.c"
static void finish_probe(void) {assert(pending);void(*f)(void*)=pending;pending=NULL;f(NULL);}
static void* snapshot_reader(void* arg) {
    (void)arg;
    for(int i=0;i<20000;i++) {
        read_pico_sd_info_t info;
        int err=read_pico_sd_get_info(&info);
        if(err==ESP_OK) assert(info.mounted && info.capacity_bytes==1048576 && info.free_bytes==524288);
    }
    return NULL;
}
int main(void) {
    read_pico_sd_info_t info;
    assert(read_pico_sd_get_info(NULL)==ESP_ERR_INVALID_ARG);
    assert(read_pico_sd_start_probe()==ESP_ERR_NOT_FINISHED);
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED);
    assert(read_pico_sd_sync()==ESP_ERR_NOT_FINISHED);
    finish_probe();
    assert(read_pico_sd_get_info(&info)==ESP_OK && info.mounted && info.capacity_bytes);
    present=false;
    assert(read_pico_sd_get_info(&info)==ESP_ERR_NOT_FOUND);
    assert(!info.mounted && !info.needs_format && !info.capacity_bytes && !info.free_bytes && !info.name[0]);
    assert(unmounts==0 && card==&mock_card);
    present=true;
    assert(read_pico_sd_get_info(&info)==ESP_ERR_INVALID_STATE && info.present && !info.mounted);
    assert(read_pico_sd_start_probe()==ESP_ERR_INVALID_STATE && mounts==1);
    assert(read_pico_sd_format()==ESP_ERR_INVALID_STATE && formats==0);
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED && unmounts==1);
    finish_probe();
    assert(read_pico_sd_get_info(&info)==ESP_OK && info.mounted && mounts==2);
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED);
    present=false;
    assert(read_pico_sd_get_info(&info)==ESP_ERR_NOT_FOUND);
    present=true;
    finish_probe();
    assert(read_pico_sd_get_info(&info)==ESP_ERR_INVALID_STATE && !info.mounted && !info.needs_format);
    drop_during_mount=true;
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED);
    finish_probe();
    assert(read_pico_sd_get_info(&info)==ESP_ERR_NOT_FOUND && !info.mounted);
    present=true;
    assert(read_pico_sd_get_info(&info)==ESP_ERR_INVALID_STATE);
    drop_during_mount=false;
    assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED);
    finish_probe();
    assert(read_pico_sd_get_info(&info)==ESP_OK);
    assert(read_pico_sd_sync()==ESP_OK);
    assert(read_pico_sd_get_info(&info)==ESP_ERR_INVALID_STATE && !info.mounted && !info.capacity_bytes);
    pthread_t reader;
    assert(pthread_create(&reader,NULL,snapshot_reader,NULL)==0);
    for(int i=0;i<100;i++) {
        assert(read_pico_sd_remount()==ESP_ERR_NOT_FINISHED);
        finish_probe();
    }
    assert(pthread_join(reader,NULL)==0);
    assert(formats==0);
    puts("PASS: removal, stale reinsertion, busy lifecycle, mid-probe removal, explicit recovery, concurrent snapshots, no autoformat");
}
'''
(out / 'test.c').write_text(source, encoding='utf-8')
subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                '-Wno-unused-variable', '-fsanitize=address,undefined', '-g', '-pthread',
                '-I'+str(out), '-I'+str(root/'components/read_pico/include'),
                str(out/'test.c'), '-o', str(out/'test')], check=True)
subprocess.run([str(out/'test')], check=True)
