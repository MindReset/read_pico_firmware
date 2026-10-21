"""Exercise real USB teardown with delayed writes and injected failures.

SPDX-FileCopyrightText: 2026 mindreset
SPDX-License-Identifier: Apache-2.0
中文：验证串口回收、独占存储卡、失败重试与启动失败收尾。
English: Verify console recovery, exclusive card ownership, retries and failed-start cleanup.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
STUB = r'''
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NOT_FINISHED 3
#define ESP_ERR_NO_MEM 4
#define ESP_LOGE(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGI(tag, ...) ((void)(tag))
#define pdMS_TO_TICKS(ms) (ms)
void vTaskDelay(int ms);
const char *esp_err_to_name(int err);
typedef struct {int slot,max_freq_khz;} sdmmc_host_t;
typedef struct {int width,clk,cmd,d0,d1,d2,d3,cd,wp,flags;} sdmmc_slot_config_t;
typedef struct {int marker;} sdmmc_card_t;
#define SDMMC_HOST_DEFAULT() ((sdmmc_host_t){.slot=1})
#define SDMMC_SLOT_CONFIG_DEFAULT() ((sdmmc_slot_config_t){0})
#define SDMMC_FREQ_DEFAULT 20000
#define SDMMC_SLOT_FLAG_INTERNAL_PULLUP 1
#define GPIO_NUM_38 38
#define GPIO_NUM_42 42
#define GPIO_NUM_44 44
#define GPIO_NUM_NC -1
int sdmmc_host_init(void);
int sdmmc_host_init_slot(int slot,const sdmmc_slot_config_t *config);
int sdmmc_host_deinit(void);
int sdmmc_card_init(const sdmmc_host_t *host,sdmmc_card_t *card);
typedef void *usb_phy_handle_t;
typedef struct {int controller,target;} usb_phy_config_t;
#define USB_PHY_CTRL_SERIAL_JTAG 1
#define USB_PHY_TARGET_INT 0
int usb_new_phy(const usb_phy_config_t *config,usb_phy_handle_t *handle);
int usb_del_phy(usb_phy_handle_t handle);
typedef struct {bool mounted;} read_pico_sd_info_t;
int read_pico_sd_get_info(read_pico_sd_info_t *info);
int read_pico_sd_sync(void);
int read_pico_sd_remount(void);
bool ttf_font_is_builtin(void);
int ttf_font_open_builtin(void);
typedef void *tinyusb_msc_storage_handle_t;
typedef struct {struct {int auto_mount_off;} user_flags;} tinyusb_msc_driver_config_t;
typedef struct {struct {sdmmc_card_t *card;} medium;int mount_point;
struct {bool do_not_format;struct {int max_files;} config;} fat_fs;} tinyusb_msc_storage_config_t;
#define TINYUSB_MSC_STORAGE_MOUNT_USB 1
typedef struct {int port;} tinyusb_config_t;
#define TINYUSB_DEFAULT_CONFIG() ((tinyusb_config_t){0})
int tinyusb_msc_install_driver(const tinyusb_msc_driver_config_t *config);
int tinyusb_msc_new_storage_sdmmc(const tinyusb_msc_storage_config_t *config,tinyusb_msc_storage_handle_t *handle);
int tinyusb_msc_delete_storage(tinyusb_msc_storage_handle_t handle);
int tinyusb_msc_uninstall_driver(void);
int tinyusb_driver_install(const tinyusb_config_t *config);
int tinyusb_driver_uninstall(void);
bool tud_mounted(void);
void tud_disconnect(void);
/* r93 的 usb_storage_start 先作废锁屏拼贴、交还封面与头像解码句柄再卸载卡。
   r93 usb_storage_start invalidates the lock collage and joins cover/avatar decoding
   before unmounting; stub the three product entry points it now calls. */
void book_lock_collage_invalidate(void);
bool book_cover_jobs_quiesce(unsigned timeout_ms);
bool ui_avatar_quiesce(unsigned timeout_ms);
/* gen18：USB 接管即取消推流接收（武装让位+留痕），见 usb_storage_start 顶部。
   gen18: USB takeover cancels push receiving (the arm yields with a trace),
   at the top of usb_storage_start. */
void push_sink_cancel_why(const char *why);
'''
UNIT = r'''
#include "usb_stub.h"
#include "usb_storage.c"
#include <stdio.h>
static bool mounted=true,host,driver,msc,connected;
static sdmmc_card_t *borrowed;
static int stage,delete_error,usb_error,msc_error,busy,deletes,remounts,phy_restores,push_cancel_calls;
static int failed(int where){return stage==where?ESP_FAIL:ESP_OK;}
void vTaskDelay(int ms){assert(ms>0);}
const char *esp_err_to_name(int e){(void)e;return "test";}
int read_pico_sd_get_info(read_pico_sd_info_t *i){i->mounted=mounted;return ESP_OK;}
int read_pico_sd_sync(void){assert(mounted);mounted=false;return ESP_OK;}
int read_pico_sd_remount(void){assert(!driver&&!borrowed&&!msc&&!host);mounted=true;++remounts;return ESP_OK;}
bool ttf_font_is_builtin(void){return true;}
int ttf_font_open_builtin(void){return ESP_OK;}
int sdmmc_host_init(void){assert(!mounted&&!host);if(failed(1))return ESP_FAIL;host=true;return ESP_OK;}
int sdmmc_host_init_slot(int slot,const sdmmc_slot_config_t *c){assert(slot==1&&c->width==1);return failed(2);}
int sdmmc_card_init(const sdmmc_host_t *h,sdmmc_card_t *c){assert(h->max_freq_khz==20000);c->marker=47;return failed(3);}
int sdmmc_host_deinit(void){assert(!borrowed&&!driver&&!msc);host=false;return ESP_OK;}
int usb_new_phy(const usb_phy_config_t *c,usb_phy_handle_t *h){assert(c->controller==USB_PHY_CTRL_SERIAL_JTAG);*h=malloc(1);assert(*h);++phy_restores;return ESP_OK;}
int usb_del_phy(usb_phy_handle_t h){free(h);return ESP_OK;}
int tinyusb_msc_install_driver(const tinyusb_msc_driver_config_t *c){assert(c->user_flags.auto_mount_off&&!mounted);if(failed(4))return ESP_FAIL;msc=true;return ESP_OK;}
int tinyusb_msc_new_storage_sdmmc(const tinyusb_msc_storage_config_t *c,tinyusb_msc_storage_handle_t *h){
    assert(msc&&c->medium.card&&c->fat_fs.do_not_format);
    if(failed(5))return ESP_FAIL;borrowed=c->medium.card;*h=malloc(1);assert(*h);return ESP_OK;
}
int tinyusb_driver_install(const tinyusb_config_t *c){(void)c;assert(borrowed&&!mounted&&!s_serial_phy);if(failed(6))return ESP_FAIL;driver=connected=true;return ESP_OK;}
int tinyusb_msc_delete_storage(tinyusb_msc_storage_handle_t h){
    assert(borrowed&&borrowed->marker==47&&!mounted);++deletes;
    if(busy>0){--busy;return ESP_ERR_INVALID_STATE;}if(delete_error)return delete_error;
    free(h);borrowed=NULL;return ESP_OK;
}
int tinyusb_driver_uninstall(void){assert(!borrowed&&!connected);if(usb_error)return usb_error;driver=false;return ESP_OK;}
int tinyusb_msc_uninstall_driver(void){assert(!borrowed&&!driver);if(msc_error)return msc_error;msc=false;return ESP_OK;}
bool tud_mounted(void){return connected;}
void tud_disconnect(void){assert(driver);connected=false;}
/* 无封面/头像任务在跑，交还立即成功，与产品在空闲下的行为一致。/ With no cover or
   avatar job in flight the joins succeed immediately, matching the idle product. */
void book_lock_collage_invalidate(void){}
bool book_cover_jobs_quiesce(unsigned timeout_ms){(void)timeout_ms;return true;}
bool ui_avatar_quiesce(unsigned timeout_ms){(void)timeout_ms;return true;}
void push_sink_cancel_why(const char *why){(void)why;++push_cancel_calls;}
static void stopped(void){assert(mounted&&!usb_storage_active()&&!usb_storage_connected());assert(!s_cleanup_pending&&!s_card&&!s_storage&&!host&&!driver&&!msc&&s_serial_phy);}
static void retained(void){assert(!mounted&&usb_storage_active()&&!usb_storage_connected()&&s_cleanup_pending&&s_card&&host&&s_serial_phy);assert(usb_storage_start()==ESP_ERR_INVALID_STATE);}
int main(void){
    usb_storage_phy_init();assert(phy_restores==1);usb_storage_phy_init();assert(phy_restores==1);
    assert(usb_storage_stop()==ESP_OK);stopped();
    for(int i=0;i<3;++i){int cancels=push_cancel_calls;assert(usb_storage_start()==ESP_OK&&usb_storage_connected());assert(push_cancel_calls==cancels+1);busy=3;assert(usb_storage_stop()==ESP_OK);stopped();}
    assert(usb_storage_start()==ESP_OK);delete_error=ESP_FAIL;int before=remounts;
    assert(usb_storage_stop()==ESP_FAIL);retained();assert(borrowed&&remounts==before);
    delete_error=0;assert(usb_storage_stop()==ESP_OK);stopped();
    assert(usb_storage_start()==ESP_OK);busy=25;int attempts=deletes;before=remounts;
    assert(usb_storage_stop()==ESP_ERR_INVALID_STATE&&deletes-attempts==20);retained();assert(remounts==before);
    assert(usb_storage_stop()==ESP_OK);stopped();
    assert(usb_storage_start()==ESP_OK);usb_error=ESP_FAIL;before=remounts;
    assert(usb_storage_stop()==ESP_FAIL);retained();assert(!borrowed&&driver&&remounts==before);
    usb_error=0;assert(usb_storage_stop()==ESP_OK);stopped();
    assert(usb_storage_start()==ESP_OK);msc_error=ESP_FAIL;before=remounts;
    assert(usb_storage_stop()==ESP_FAIL);retained();assert(!borrowed&&!driver&&msc&&remounts==before);
    msc_error=0;assert(usb_storage_stop()==ESP_OK);stopped();
    for(stage=1;stage<=6;++stage){assert(usb_storage_start()==ESP_FAIL);stopped();}stage=0;
    stage=6;delete_error=ESP_FAIL;before=remounts;assert(usb_storage_start()==ESP_FAIL);retained();assert(remounts==before);
    stage=delete_error=0;assert(usb_storage_stop()==ESP_OK);stopped();
    release_serial_phy();assert(!s_serial_phy);
    puts("USB: console recovery, delayed writes, 3 stop failures, 6 start failures, exclusive remount and retry passed");
}
'''
with tempfile.TemporaryDirectory() as d:
    p = Path(d)
    (p/'usb_stub.h').write_text(STUB)
    for name in ('esp_err.h','driver/sdmmc_host.h','esp_log.h','esp_private/usb_phy.h',
                 'freertos/FreeRTOS.h','freertos/task.h','push_sink.h','read_pico_sd.h','sdmmc_cmd.h',
                 'tinyusb.h','tinyusb_default_config.h','tinyusb_msc.h','ttf_font.h',
                 'ui_avatar.h','book_cover_jobs.h','book_lock_collage.h'):
        header=p/name;header.parent.mkdir(parents=True,exist_ok=True);header.write_text('#include "usb_stub.h"\n')
    (p/'test.c').write_text(UNIT)
    # -Wno-misleading-indentation：本文件 UNIT 的单行 if 写法与 test_transfer_wifi_forget
    # 同一风格，该 gcc 告警级测试已按惯例关闭。/ Same one-line if style as
    # test_transfer_wifi_forget, which already carries this project-convention flag.
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-g','-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer',f'-I{p}',f'-I{ROOT / "main/apps"}',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
