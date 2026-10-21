/* SPDX-License-Identifier: Apache-2.0
 * 中文：四位数字密码采用随机盐及PBKDF2-HMAC-SHA256，单blob提交，不写明文。
 * English: Four-digit credentials use random salts and PBKDF2-HMAC-SHA256, committed as one blob without plaintext.
 * 冻结：损坏/分配/写入失败不得变成免密码；普通配置恢复不覆盖凭据。
 * Frozen: Corruption/allocation/write failures cannot disable authentication; ordinary restore does not replace credentials.
 */
#include "lock_pin.h"
#include <stdint.h>
#include <string.h>
#include "esp_random.h"
#include "nvs.h"
#include "psa/crypto.h"
#define PIN_ITERATIONS 10000u
#define PIN_MAGIC UINT32_C(0x50494e31)
typedef struct { uint32_t magic, iterations; uint8_t salt[16], digest[32]; } pin_record_t;
static pin_record_t s_record;
static bool s_loaded, s_enabled, s_available;
void lock_pin_wipe(void *bytes,unsigned length) {
    volatile uint8_t *p=bytes; while (length--) *p++=0;
}
static bool pin_valid(const char pin[5]) {
    if (!pin) return false;
    for (unsigned i=0;i<4;++i) if (pin[i]<'0'||pin[i]>'9') return false;
    return pin[4]==0;
}
static void load(void) {
    if (s_loaded) return;
    s_loaded=true;s_enabled=true;s_available=false;
    nvs_handle_t handle;
    esp_err_t error=nvs_open("pico_lock",NVS_READONLY,&handle);
    if (error==ESP_ERR_NVS_NOT_FOUND) {s_enabled=false;s_available=true;return;}
    if (error!=ESP_OK) return;
    size_t size=sizeof(s_record);
    error=nvs_get_blob(handle,"credential",&s_record,&size);
    nvs_close(handle);
    if (error==ESP_ERR_NVS_NOT_FOUND) {s_enabled=false;s_available=true;return;}
    if (error==ESP_OK && size==sizeof(s_record) && s_record.magic==PIN_MAGIC && s_record.iterations==PIN_ITERATIONS)
        s_available=true;
    if (!s_available) lock_pin_wipe(&s_record,sizeof(s_record));
}
static bool derive(const char pin[5],const pin_record_t *record,uint8_t digest[32]) {
    if (!pin_valid(pin)||psa_crypto_init()!=PSA_SUCCESS) return false;
    psa_key_derivation_operation_t operation=PSA_KEY_DERIVATION_OPERATION_INIT;
    psa_status_t status=psa_key_derivation_setup(&operation,PSA_ALG_PBKDF2_HMAC(PSA_ALG_SHA_256));
    if (status==PSA_SUCCESS) status=psa_key_derivation_input_integer(&operation,PSA_KEY_DERIVATION_INPUT_COST,record->iterations);
    if (status==PSA_SUCCESS) status=psa_key_derivation_input_bytes(&operation,PSA_KEY_DERIVATION_INPUT_SALT,record->salt,sizeof(record->salt));
    if (status==PSA_SUCCESS) status=psa_key_derivation_input_bytes(&operation,PSA_KEY_DERIVATION_INPUT_PASSWORD,(const uint8_t*)pin,4);
    if (status==PSA_SUCCESS) status=psa_key_derivation_output_bytes(&operation,digest,32);
    (void)psa_key_derivation_abort(&operation);
    return status==PSA_SUCCESS;
}
bool lock_pin_enabled(void) {load();return s_enabled;}
bool lock_pin_available(void) {load();return s_available;}
bool lock_pin_verify(const char pin[5]) {
    load();if (!s_available||!s_enabled||!pin_valid(pin)) return false;
    uint8_t digest[32]={0};bool computed=derive(pin,&s_record,digest);
    uint8_t different=0;for(unsigned i=0;i<32;++i) different|=digest[i]^s_record.digest[i];
    lock_pin_wipe(digest,sizeof(digest));return computed&&different==0;
}
esp_err_t lock_pin_replace(const char old_pin[5],const char new_pin[5]) {
    load();if (!s_available) return ESP_ERR_INVALID_STATE;
    if (s_enabled&&!lock_pin_verify(old_pin)) return ESP_ERR_INVALID_ARG;
    if (new_pin&&!pin_valid(new_pin)) return ESP_ERR_INVALID_ARG;
    pin_record_t next={.magic=PIN_MAGIC,.iterations=PIN_ITERATIONS};
    if (new_pin) {
        esp_fill_random(next.salt,sizeof(next.salt));
        if (!derive(new_pin,&next,next.digest)) {lock_pin_wipe(&next,sizeof(next));return ESP_FAIL;}
    }
    nvs_handle_t handle;esp_err_t error=nvs_open("pico_lock",NVS_READWRITE,&handle);
    if (error==ESP_OK) {
        error=new_pin?nvs_set_blob(handle,"credential",&next,sizeof(next)):nvs_erase_key(handle,"credential");
        if (!new_pin&&error==ESP_ERR_NVS_NOT_FOUND) error=ESP_OK;
        if (error==ESP_OK) error=nvs_commit(handle);
        nvs_close(handle);
    }
    if (error==ESP_OK) {s_record=next;s_enabled=new_pin!=NULL;}
    lock_pin_wipe(&next,sizeof(next));return error;
}
