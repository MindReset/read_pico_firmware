#pragma once

/* 显式启用的本地 USB 测时入口；普通构建不含命令读取。/ Explicit local USB bench; no command reader in product builds. */
#ifdef KIIKO_LOCAL_LATENCY
#include <fcntl.h>
#include "hal/usb_serial_jtag_ll.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"

static void local_latency_init(void) {
    int flags=fcntl(STDIN_FILENO,F_GETFL,0);
    if(flags>=0)fcntl(STDIN_FILENO,F_SETFL,flags|O_NONBLOCK);
    ESP_LOGI("latency","LOCAL_USB_PROBE ready version=1");
}

static app_redraw_t local_latency_poll(app_ctx_t *ctx,const app_desc_t *current) {
    static char line[80];static unsigned used;char c;
    while(usb_serial_jtag_ll_read_rxfifo((uint8_t*)&c,1)==1) {
        if(c=='\r')continue;
        if(c!='\n') { if(used<sizeof(line)-1)line[used++]=c;else used=0;continue; }
        line[used]=0;used=0;
        int a=-1,b=-1;
        ctx->user_activity=true;ctx->consumed=false;ctx->now_ms=esp_timer_get_time()/1000;
        if(!strcmp(line,"perf info")) {
            ESP_LOGI("latency","INFO page=%d mode=%d auto_lock=%d widget=%d heap=%u internal=%u largest=%u stack=%u",
                app_index_of(current),app_settings_main_refresh_mode(),app_settings_auto_lock_minutes(),app_settings_home_component(),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),(unsigned)uxTaskGetStackHighWaterMark(NULL));
        } else if(sscanf(line,"perf page %d",&a)==1&&a>=0&&a<4) {
            ui_nav_request(ctx,a);ESP_LOGI("latency","REQUEST page=%d",a);
        } else if(sscanf(line,"perf mode %d",&a)==1&&a>=0&&a<=2) {
            app_settings_set_main_refresh_mode(a);ESP_LOGI("latency","MODE %d",a);
        } else if(sscanf(line,"perf tap %d %d",&a,&b)==2&&a>=0&&a<684&&b>=0&&b<1216&&current->on_gesture) {
            ui_gesture_event_t ev={.type=UI_GESTURE_TAP,.x0=a,.y0=b,.x=a,.y=b,.hold_ms=50};
            int64_t t=esp_timer_get_time();
            app_redraw_t result=current->on_gesture(ctx,&ev);
            ESP_LOGI("latency","HANDLER tap=%d,%d redraw=%d us=%lld",a,b,result,(long long)(esp_timer_get_time()-t));
            return result;
        } else if(!strcmp(line,"perf back")&&current->on_key) {
            return current->on_key(ctx,UI_KEY_1);
        } else if(!strcmp(line,"perf repaint"))return APP_REDRAW_PAGE;
        else ESP_LOGI("latency","UNKNOWN");
    }
    return APP_REDRAW_NONE;
}
#endif
