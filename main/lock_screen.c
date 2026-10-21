/* SPDX-License-Identifier: Apache-2.0
 * 中文：独立锁屏输入循环；禁止触摸键、蓝牙、拿起或冷启动绕过密码。
 * English: Isolated locked-input loop; touch keys, BLE, pickup and cold starts cannot bypass authentication.
 * 冻结：只有摘要校验成功才能返回；模糊缓存只建一次，入场只推一次灰阶，按键只推变化区域。
 * Frozen: Return only after digest verification; build frost once, present entry once and update only changed controls while typing.
 */
#include "lock_screen.h"
#include "lock_pin.h"
#include "settings.h"
#include "ui_pinpad.h"
#include "ui_kit.h"
#include "sleep.h"
#include "boot_state.h"
#include "display.h"
#include "e0470_epaper_waveform.h"
#include "read_pico_pmu.h"
#include "ble_page_turner.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#define FRAME_BYTES (684u*1216u/2u)
#ifndef LOCK_SCREEN_CACHE_PARENT
#define LOCK_SCREEN_CACHE_PARENT "/sdcard/.readpico"
#endif
#define LOCK_ART_PATH LOCK_SCREEN_CACHE_PARENT "/locks/pin-background.bin"
#define ART_MAGIC UINT32_C(0x50414432)
typedef struct {uint32_t magic,rotation,style,checksum;uint64_t wallpaper;} art_header_t;
extern const uint8_t lock_4bpp_bin_start[] asm("_binary_lock_4bpp_bin_start");
static uint32_t checksum(const uint8_t *bytes){uint32_t h=2166136261u;for(unsigned i=0;i<FRAME_BYTES;++i)h=(h^bytes[i])*16777619u;return h;}
static uint64_t wallpaper_key(void){
    if(app_settings_lock_style()!=1)return 0;
    const char *path=app_settings_wallpaper_path();
    uint64_t key=UINT64_C(14695981039346656037);
    for(const unsigned char *p=(const unsigned char*)path;*p;++p)key=(key^*p)*UINT64_C(1099511628211);
    struct stat st;
    if(stat(path,&st)==0){
        uint64_t info[2]={(uint64_t)st.st_size,(uint64_t)st.st_mtime};
        const unsigned char *bytes=(const unsigned char*)info;
        for(unsigned i=0;i<sizeof(info);++i)key=(key^bytes[i])*UINT64_C(1099511628211);
    }
    return key;
}
bool lock_screen_restore(uint8_t *frame){
    if(!frame)return false;
    FILE *f=fopen(LOCK_ART_PATH,"rb");if(!f)return false;art_header_t h;
    // 跨样式/换壁纸后先拒绝旧图，再由锁屏入口绘制当前选择；不多分配一帧。
    // Reject old art before reading pixels after a style/wallpaper change; the lock entry draws the current choice without another frame.
    bool ok=fread(&h,1,sizeof(h),f)==sizeof(h)&&h.magic==ART_MAGIC&&h.rotation==(uint32_t)epd_get_rotation()&&
        h.style==app_settings_lock_style()&&h.wallpaper==wallpaper_key()&&
        fread(frame,1,FRAME_BYTES,f)==FRAME_BYTES&&fgetc(f)==EOF&&h.checksum==checksum(frame);
    fclose(f);return ok;
}
static void save_art(const uint8_t *frame){
    (void)mkdir(LOCK_SCREEN_CACHE_PARENT,0775);(void)mkdir(LOCK_SCREEN_CACHE_PARENT "/locks",0775);
    FILE *f=fopen(LOCK_ART_PATH ".tmp","wb");if(!f)return;
    art_header_t h={.magic=ART_MAGIC,.rotation=(uint32_t)epd_get_rotation(),
        .style=app_settings_lock_style(),.checksum=checksum(frame),.wallpaper=wallpaper_key()};
    bool ok=fwrite(&h,1,sizeof(h),f)==sizeof(h)&&fwrite(frame,1,FRAME_BYTES,f)==FRAME_BYTES&&fflush(f)==0;
    if(fclose(f))ok=false;
    if(ok&&rename(LOCK_ART_PATH ".tmp",LOCK_ART_PATH)==0)return;
    (void)remove(LOCK_ART_PATH ".tmp");
}
static void restore_art(uint8_t *fb,const uint8_t *original){
    if(original)memcpy(fb,original,FRAME_BYTES);
    else if(!lock_screen_restore(fb)){memset(fb,255,FRAME_BYTES);ui_draw_full_image(fb,lock_4bpp_bin_start);}
}
static void present(EpdiyHighlevelState *hl,EpdRect area){
    epd_poweron();enum EpdDrawError result=update_display_area_diff_with(hl,&E0470_WAVEFORM,MODE_GL16,area);
    guard_draw_result(hl,result);
    // 灰阶差分失败只重建同一密码画面，绝不进入未验证的页面。
    // On a display failure rebuild this same protected screen, never an unauthenticated page.
    if(result!=EPD_DRAW_SUCCESS)guard_draw_result(hl,update_display_full(hl));
}
static void present_input(EpdiyHighlevelState *hl,EpdRect area){
    enum EpdDrawError result=update_display_area_diff_with(hl,&E0470_FOLLOW_WAVEFORM,MODE_DU,area);
    guard_draw_result(hl,result);
    if(result!=EPD_DRAW_SUCCESS)present(hl,ui_pinpad_full());
}

void lock_screen_authenticate(EpdiyHighlevelState *hl,cst836u_handle_t touch,sc7a20h_handle_t acc,bool boot){
    uint8_t *fb=epd_hl_get_framebuffer(hl);
    (void)ble_pt_stop(2000);
    save_art(fb);
    uint8_t *original=heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)>FRAME_BYTES+1024u*1024u
        ?heap_caps_malloc(FRAME_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT):NULL;
    if(original)memcpy(original,fb,FRAME_BYTES);
    ui_pinpad_t pad={0};ui_pinpad_begin(&pad,fb,"输入密码");
    bool waiting=!boot;
    unsigned failures=0;int64_t retry_at=0;
    for(;;){
        if(waiting){
            restore_art(fb,original);present(hl,ui_pinpad_full());
            app_lock_wait_key_idle(800);epd_poweroff();
            if(app_light_sleep_wait_timed(acc,10u*60u*1000u)==APP_WAKE_TIMEOUT){
                ui_pinpad_end(&pad);free(original);app_enter_host_sleep(APP_SLEEP_DEEP);
                // 防御性处理：电源交接意外返回也继续验证。/ Defensive: a returning power handoff still requires verification.
                original=NULL;ui_pinpad_begin(&pad,fb,"输入密码");
            }
        }
        app_lock_wait_key_idle(800);
        ui_pinpad_reset(&pad,"输入密码",lock_pin_available()?"":"凭据读取失败，请重启后重试");
        if(!lock_pin_available())pad.blocked=true;
        else if(esp_timer_get_time()/1000<retry_at){pad.blocked=true;snprintf(pad.notice,sizeof(pad.notice),"请等待 10 秒后重试");}
        // 直接推送完整密码画面一次，不再补刷第二段背景。/ Present the complete PIN screen once, without a second backdrop stage.
        ui_pinpad_paint(fb,&pad,ui_pinpad_full());present(hl,ui_pinpad_full());
        // 密码等待不是启动失败；待验证时保留一次性恢复，不提前开书。
        // Waiting for a PIN is not a failed boot; hold one-shot resume without opening the book.
        if(boot){pico_boot_ready();pico_boot_hold_resume();boot=false;}
        ui_gesture_t gesture={0};bool was_down=false,draining=true;
        cst836u_touch_t latest={0};int64_t active_at=esp_timer_get_time()/1000,last_poll=active_at;
        bool cancel=false;
        while(!cancel){
            int64_t now=esp_timer_get_time()/1000;
            if(now-active_at>=30000){cancel=true;break;}
            if(now-last_poll>=40){last_poll=now;(void)read_pico_pmu_poll();if(read_pico_pmu_take_key_action()!=READ_PICO_PMU_KEY_NONE){cancel=true;break;}}
            if(pad.blocked&&lock_pin_available()&&now>=retry_at){ui_pinpad_reset(&pad,NULL,"请重新输入");ui_pinpad_paint(fb,&pad,ui_pinpad_entry_area());present(hl,ui_pinpad_entry_area());}
            cst836u_touch_t sample={0};esp_err_t error=cst836u_read(touch,&sample);
            bool down=error==ESP_OK&&sample.touched;
            if(draining){if(!down&&error==ESP_OK)draining=false;was_down=down;vTaskDelay(pdMS_TO_TICKS(5));continue;}
            if(down)latest=sample;else{latest.touched=false;latest.count=0;}
            app_ctx_t ctx={.now_ms=now,.touch=&latest,.pressed=down&&!was_down,.released=!down&&was_down,.consumed=error!=ESP_OK};
            ui_gesture_event_t event;
            if(ui_gesture_feed(&gesture,&ctx,&event)){
                active_at=now;EpdRect dirty;ui_pin_result_t r=ui_pinpad_handle(&pad,&event,&dirty);
                if(r==UI_PIN_CANCEL){cancel=true;break;}
                if(r!=UI_PIN_NONE){ui_pinpad_paint_input(fb,&pad);present_input(hl,dirty);}
                if(r==UI_PIN_COMPLETE){
                    bool verified=lock_pin_verify(pad.digits);
                    if(verified){ui_pinpad_end(&pad);free(original);read_pico_pmu_drain_events();ble_pt_event_t e;while(ble_pt_pop_key(&e)){}ble_pt_raw_t raw;while(ble_pt_pop_raw(&raw)){}return;}
                    ui_pinpad_reset(&pad,NULL,"密码不正确，请重试");
                    if(++failures>=3){failures=0;retry_at=now+10000;pad.blocked=true;snprintf(pad.notice,sizeof(pad.notice),"请等待 10 秒后重试");}
                    ui_pinpad_paint(fb,&pad,ui_pinpad_entry_area());present(hl,ui_pinpad_entry_area());
                }
            }
            was_down=down;vTaskDelay(pdMS_TO_TICKS(5));
        }
        ui_pinpad_reset(&pad,NULL,"");waiting=true;
    }
}
