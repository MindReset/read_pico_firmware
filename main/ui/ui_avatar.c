/* SPDX-License-Identifier: Apache-2.0
 * 中文：单个低优先级任务准备首页/设置头像，按代次发布两个有界小图；UI只拷贝结果。
 * English: One low-priority task prepares two bounded avatars, publishes by generation and never renders UI.
 */
#include "ui_avatar.h"
#include "ui_wallpaper.h"
#include "book_store.h"
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static portMUX_TYPE s_avatar_lock=portMUX_INITIALIZER_UNLOCKED;
static const int s_sides[]={60,82};
static char s_path[288];
static uint8_t *s_pixels[2];
static unsigned s_generation,s_revision;
static ui_avatar_state_t s_states[2];
static bool s_running,s_paused;
static int slot_for(int side) { return side==60?0:side==82?1:-1; }
static void avatar_worker(void *arg) {
    (void)arg;
    for (;;) {
        char path[288];unsigned generation;int slot=-1;
        portENTER_CRITICAL(&s_avatar_lock);
        if (!s_paused) for(int i=0;i<2;++i) if(s_states[i]==UI_AVATAR_PENDING){slot=i;break;}
        if(slot<0){s_running=false;portEXIT_CRITICAL(&s_avatar_lock);break;}
        memcpy(path,s_path,sizeof(path));generation=s_generation;
        portEXIT_CRITICAL(&s_avatar_lock);
        int side=s_sides[slot];
        uint8_t *pixels=heap_caps_malloc((size_t)side*side,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        bool ready=pixels&&ui_wallpaper_prepare_avatar(path,pixels,side);
        portENTER_CRITICAL(&s_avatar_lock);
        bool current=!s_paused&&generation==s_generation&&!strcmp(path,s_path);
        uint8_t *old=NULL;
        if(current){
            s_states[slot]=ready?UI_AVATAR_READY:UI_AVATAR_UNAVAILABLE;
            if(ready){old=s_pixels[slot];s_pixels[slot]=pixels;pixels=NULL;}
        }
        portEXIT_CRITICAL(&s_avatar_lock);
        free(old);free(pixels);
        // UI优先；下一尺寸或新的请求都重新取最新代次。
        // Yield to UI; capture the latest generation before each size/new request.
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    vTaskDelete(NULL);
}
void ui_avatar_invalidate(void) {
    uint8_t *old[2];
    portENTER_CRITICAL(&s_avatar_lock);
    ++s_generation;s_path[0]=0;
    for(int i=0;i<2;++i){old[i]=s_pixels[i];s_pixels[i]=NULL;s_states[i]=UI_AVATAR_UNAVAILABLE;}
    portEXIT_CRITICAL(&s_avatar_lock);
    for(int i=0;i<2;++i)free(old[i]);
}
void ui_avatar_request(const char *path,int side) {
    int slot=slot_for(side);
    if(slot<0)return;
    if(!path||!path[0]||strnlen(path,sizeof(s_path))>=sizeof(s_path)){ui_avatar_invalidate();return;}
    // 路径切换仅释放小图，不 stat/fopen；缓存校验和解码都留给任务。
    // Path changes release thumbnails only; stat/open/cache checks and decoding stay in the worker.
    const unsigned revision=book_store_revision();
    if(strcmp(path,s_path)||s_revision!=revision){ui_avatar_invalidate();s_revision=revision;}
    portENTER_CRITICAL(&s_avatar_lock);
    s_paused=false;
    memcpy(s_path,path,strlen(path)+1);
    bool start=false;
    if(s_states[slot]!=UI_AVATAR_READY)s_states[slot]=UI_AVATAR_PENDING;
    if(!s_running&&s_states[slot]==UI_AVATAR_PENDING){s_running=true;start=true;}
    portEXIT_CRITICAL(&s_avatar_lock);
    if(start&&xTaskCreate(avatar_worker,"avatar",12288,NULL,1,NULL)!=pdPASS){
        portENTER_CRITICAL(&s_avatar_lock);s_running=false;s_states[slot]=UI_AVATAR_UNAVAILABLE;portEXIT_CRITICAL(&s_avatar_lock);
    }
}
ui_avatar_state_t ui_avatar_poll(const char *path,int side,uint8_t *pixels) {
    int slot=slot_for(side);if(slot<0||!path||!pixels)return UI_AVATAR_UNAVAILABLE;
    portENTER_CRITICAL(&s_avatar_lock);
    ui_avatar_state_t state=strcmp(path,s_path)?UI_AVATAR_UNAVAILABLE:s_states[slot];
    if(state==UI_AVATAR_READY&&s_pixels[slot])memcpy(pixels,s_pixels[slot],(size_t)side*side);
    portEXIT_CRITICAL(&s_avatar_lock);
    return state;
}
bool ui_avatar_quiesce(unsigned timeout_ms) {
    portENTER_CRITICAL(&s_avatar_lock);
    s_paused=true;++s_generation;
    for(int i=0;i<2;++i)if(s_states[i]==UI_AVATAR_PENDING)s_states[i]=UI_AVATAR_UNAVAILABLE;
    bool running=s_running;
    portEXIT_CRITICAL(&s_avatar_lock);
    int64_t deadline=esp_timer_get_time()+(int64_t)timeout_ms*1000;
    while(running){
        if(esp_timer_get_time()>=deadline)return false;
        vTaskDelay(pdMS_TO_TICKS(10));
        portENTER_CRITICAL(&s_avatar_lock);running=s_running;portEXIT_CRITICAL(&s_avatar_lock);
    }
    return true;
}
