#!/usr/bin/env python3
"""Fault-inject the production fallible WiFi constructor; no ESP hardware required.
对生产 WiFi 构造器注入分配/绑定/注册故障，保证返回错误而不重启。
"""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="pico-netif-") as directory:
    work = Path(directory)
    (work / 'esp_netif.h').write_text('''#pragma once
#include <stdbool.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 101
#define ESP_FAIL 102
typedef struct {int mode;} esp_netif_config_t;
typedef struct {int mode;} esp_netif_t;
#define ESP_NETIF_DEFAULT_WIFI_AP() {.mode=1}
#define ESP_NETIF_DEFAULT_WIFI_STA() {.mode=0}
esp_netif_t *esp_netif_new(const esp_netif_config_t*);
''')
    (work / 'esp_wifi_default.h').write_text('''#pragma once
#include "esp_netif.h"
esp_err_t esp_netif_attach_wifi_ap(esp_netif_t*);
esp_err_t esp_netif_attach_wifi_station(esp_netif_t*);
esp_err_t esp_wifi_set_default_wifi_ap_handlers(void);
esp_err_t esp_wifi_set_default_wifi_sta_handlers(void);
''')
    (work / 'test.c').write_text('''#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "transfer_netif.h"
static int failure,attached,handlers,mode;
esp_netif_t *esp_netif_new(const esp_netif_config_t *c){mode=c->mode;if(failure==1)return NULL;esp_netif_t *n=malloc(sizeof(*n));n->mode=mode;return n;}
static esp_err_t attach(esp_netif_t *n,int m){assert(n&&n->mode==m);attached++;return failure==2?ESP_FAIL:ESP_OK;}
esp_err_t esp_netif_attach_wifi_ap(esp_netif_t *n){return attach(n,1);}
esp_err_t esp_netif_attach_wifi_station(esp_netif_t *n){return attach(n,0);}
static esp_err_t setup(int m){assert(mode==m);handlers++;return failure==3?ESP_FAIL:ESP_OK;}
esp_err_t esp_wifi_set_default_wifi_ap_handlers(void){return setup(1);}
esp_err_t esp_wifi_set_default_wifi_sta_handlers(void){return setup(0);}
int main(void){
 for(int ap=0;ap<2;ap++){
 for(failure=0;failure<4;failure++){
 attached=handlers=0;esp_netif_t *n=(void*)1;esp_err_t e=transfer_create_netif(ap,&n);
 assert(e==(failure==1?ESP_ERR_NO_MEM:failure?ESP_FAIL:ESP_OK));
 assert(attached==(failure==1?0:1));assert(handlers==((failure==1||failure==2)?0:1));
 if(failure==1)assert(!n);else {assert(n);free(n);}
 }
 }
 puts("PASS: AP/STA allocation, attach and handler failures return safely with cleanup handles");}
''')
    binary = work / 'test'
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I' + str(work), '-I' + str(ROOT / 'components/read_pico_transfer'),
                    str(work / 'test.c'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
