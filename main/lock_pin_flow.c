/* SPDX-License-Identifier: Apache-2.0
 * 中文：设置事务独立于界面，只有最终确认才能写入。/ English: UI-independent setup transaction; only final confirmation writes.
 */
#include "lock_pin_flow.h"
#include <string.h>
void lock_pin_flow_end(lock_pin_flow_t *flow){lock_pin_wipe(flow,sizeof(*flow));}
void lock_pin_flow_begin(lock_pin_flow_t *flow,bool disable){lock_pin_flow_end(flow);flow->disable=disable;flow->step=lock_pin_enabled()?PIN_FLOW_OLD:PIN_FLOW_NEW;}
const char *lock_pin_flow_title(const lock_pin_flow_t *flow){
    return flow->step==PIN_FLOW_OLD?"验证原密码":flow->step==PIN_FLOW_NEW?"设置新密码":flow->step==PIN_FLOW_CONFIRM?"再次输入密码":"锁屏密码";
}
esp_err_t lock_pin_flow_input(lock_pin_flow_t *flow,const char pin[5]){
    if(!pin)return ESP_ERR_INVALID_ARG;
    for(unsigned i=0;i<4;++i)if(pin[i]<'0'||pin[i]>'9')return ESP_ERR_INVALID_ARG;
    if(pin[4])return ESP_ERR_INVALID_ARG;
    if(flow->step==PIN_FLOW_OLD){
        if(!lock_pin_verify(pin))return ESP_ERR_INVALID_ARG;
        memcpy(flow->old,pin,5);
        if(flow->disable){esp_err_t e=lock_pin_replace(flow->old,NULL);if(e==ESP_OK){lock_pin_flow_end(flow);flow->step=PIN_FLOW_DONE;}return e;}
        flow->step=PIN_FLOW_NEW;return ESP_OK;
    }
    if(flow->step==PIN_FLOW_NEW){memcpy(flow->next,pin,5);flow->step=PIN_FLOW_CONFIRM;return ESP_OK;}
    if(flow->step!=PIN_FLOW_CONFIRM)return ESP_ERR_INVALID_STATE;
    if(memcmp(flow->next,pin,5)){lock_pin_wipe(flow->next,5);flow->step=PIN_FLOW_NEW;return ESP_ERR_INVALID_RESPONSE;}
    esp_err_t e=lock_pin_replace(flow->old[0]?flow->old:NULL,flow->next);
    if(e==ESP_OK){lock_pin_flow_end(flow);flow->step=PIN_FLOW_DONE;}
    return e;
}
