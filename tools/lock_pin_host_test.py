#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 中文：真实密码/设置事务的故障与复位测试；PSA适配真实PBKDF2而不是假摘要。
# English: Actual credentials/setup fault and reboot tests; PSA shim delegates real PBKDF2 instead of dummy hashes.
from pathlib import Path
import hashlib
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as folder:
    p=Path(folder);(p/'psa').mkdir()
    (p/'esp_err.h').write_text('''#pragma once
#include <stdbool.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_RESPONSE 0x108
#define ESP_ERR_NVS_NOT_FOUND 0x1102
''')
    (p/'nvs.h').write_text('''#pragma once
#include <stddef.h>
#include "esp_err.h"
typedef unsigned nvs_handle_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1
esp_err_t nvs_open(const char*,int,nvs_handle_t*);
esp_err_t nvs_get_blob(nvs_handle_t,const char*,void*,size_t*);
esp_err_t nvs_set_blob(nvs_handle_t,const char*,const void*,size_t);
esp_err_t nvs_erase_key(nvs_handle_t,const char*);
esp_err_t nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
''')
    (p/'esp_random.h').write_text('#pragma once\n#include <stddef.h>\nvoid esp_fill_random(void*,size_t);\n')
    (p/'psa/crypto.h').write_text('''#pragma once
#include <stdint.h>
#include <stddef.h>
typedef int psa_status_t;
typedef struct {int stage;uint64_t cost;uint8_t salt[16],pin[4];} psa_key_derivation_operation_t;
#define PSA_SUCCESS 0
#define PSA_ALG_SHA_256 1
#define PSA_ALG_PBKDF2_HMAC(x) (2+(x))
#define PSA_KEY_DERIVATION_INPUT_COST 1
#define PSA_KEY_DERIVATION_INPUT_SALT 2
#define PSA_KEY_DERIVATION_INPUT_PASSWORD 3
#define PSA_KEY_DERIVATION_OPERATION_INIT {0}
psa_status_t psa_crypto_init(void);
psa_status_t psa_key_derivation_setup(psa_key_derivation_operation_t*,int);
psa_status_t psa_key_derivation_input_integer(psa_key_derivation_operation_t*,int,uint64_t);
psa_status_t psa_key_derivation_input_bytes(psa_key_derivation_operation_t*,int,const uint8_t*,size_t);
psa_status_t psa_key_derivation_output_bytes(psa_key_derivation_operation_t*,uint8_t*,size_t);
psa_status_t psa_key_derivation_abort(psa_key_derivation_operation_t*);
''')
    expected=hashlib.pbkdf2_hmac('sha256',b'7391',bytes(range(1,17)),10000).hex()
    unit=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef __APPLE__
#include <CommonCrypto/CommonKeyDerivation.h>
#else
#include <openssl/evp.h>
#endif
#include "lock_pin.c"
#include "lock_pin_flow.c"
static pin_record_t durable,staged;
static bool present,staged_present,ns_present,fail_read,fail_write,fail_commit;
static unsigned generation,commits,aborts;
static int crypto_fail;
esp_err_t nvs_open(const char *ns,int mode,nvs_handle_t *h){assert(!strcmp(ns,"pico_lock"));if(fail_read)return ESP_FAIL;if(mode==NVS_READONLY&&!ns_present)return ESP_ERR_NVS_NOT_FOUND;*h=1;staged=durable;staged_present=present;return ESP_OK;}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *n){assert(h==1&&!strcmp(key,"credential"));if(!present)return ESP_ERR_NVS_NOT_FOUND;assert(*n==sizeof(durable));memcpy(out,&durable,*n);return ESP_OK;}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *in,size_t n){assert(h==1&&!strcmp(key,"credential")&&n==sizeof(staged));if(fail_write)return ESP_FAIL;memcpy(&staged,in,n);staged_present=true;return ESP_OK;}
esp_err_t nvs_erase_key(nvs_handle_t h,const char *key){assert(h==1&&!strcmp(key,"credential"));if(fail_write)return ESP_FAIL;staged_present=false;return present?ESP_OK:ESP_ERR_NVS_NOT_FOUND;}
esp_err_t nvs_commit(nvs_handle_t h){assert(h==1);++commits;if(fail_commit)return ESP_FAIL;durable=staged;present=staged_present;ns_present=true;return ESP_OK;}
void nvs_close(nvs_handle_t h){assert(h==1);memset(&staged,0,sizeof(staged));}
void esp_fill_random(void *out,size_t n){assert(n==16);uint8_t *p=out;for(unsigned i=0;i<n;++i)p[i]=i+1+generation;generation+=16;}
psa_status_t psa_crypto_init(void){return crypto_fail==1?-1:0;}
psa_status_t psa_key_derivation_setup(psa_key_derivation_operation_t *o,int alg){assert(alg==PSA_ALG_PBKDF2_HMAC(PSA_ALG_SHA_256));o->stage=1;return crypto_fail==2?-1:0;}
psa_status_t psa_key_derivation_input_integer(psa_key_derivation_operation_t *o,int which,uint64_t n){assert(o->stage==1&&which==PSA_KEY_DERIVATION_INPUT_COST&&n==10000);o->cost=n;o->stage=2;return crypto_fail==3?-1:0;}
psa_status_t psa_key_derivation_input_bytes(psa_key_derivation_operation_t *o,int which,const uint8_t *p,size_t n){if(which==PSA_KEY_DERIVATION_INPUT_SALT){assert(o->stage==2&&n==16);memcpy(o->salt,p,n);o->stage=3;return crypto_fail==4?-1:0;}assert(which==PSA_KEY_DERIVATION_INPUT_PASSWORD&&o->stage==3&&n==4);memcpy(o->pin,p,n);o->stage=4;return crypto_fail==5?-1:0;}
psa_status_t psa_key_derivation_output_bytes(psa_key_derivation_operation_t *o,uint8_t *d,size_t n){assert(o->stage==4&&n==32);if(crypto_fail==6)return -1;
#ifdef __APPLE__
return CCKeyDerivationPBKDF(kCCPBKDF2,(const char*)o->pin,4,o->salt,16,kCCPRFHmacAlgSHA256,(unsigned)o->cost,d,n);
#else
return PKCS5_PBKDF2_HMAC((const char*)o->pin,4,o->salt,16,(int)o->cost,EVP_sha256(),(int)n,d)==1?0:-1;
#endif
}
psa_status_t psa_key_derivation_abort(psa_key_derivation_operation_t *o){memset(o,0,sizeof(*o));++aborts;return 0;}
static void reboot(void){s_loaded=s_available=s_enabled=false;memset(&s_record,0,sizeof(s_record));}
static bool zero(const void *data,size_t size){const uint8_t *p=data;for(size_t i=0;i<size;++i)if(p[i])return false;return true;}
int main(void){
 assert(!lock_pin_enabled()&&lock_pin_available());assert(!lock_pin_verify("7391"));
 lock_pin_flow_t f;lock_pin_flow_begin(&f,false);unsigned before=commits;
 assert(lock_pin_flow_input(&f,"7391")==0&&f.step==PIN_FLOW_CONFIRM&&!present);lock_pin_flow_end(&f);assert(commits==before&&zero(&f,sizeof(f)));
 lock_pin_flow_begin(&f,false);assert(lock_pin_flow_input(&f,"7391")==0);assert(lock_pin_flow_input(&f,"1234")==ESP_ERR_INVALID_RESPONSE&&f.step==PIN_FLOW_NEW&&!present);
 assert(lock_pin_flow_input(&f,"7391")==0&&lock_pin_flow_input(&f,"7391")==0&&f.step==PIN_FLOW_DONE&&zero(f.next,5));
 assert(lock_pin_enabled()&&lock_pin_verify("7391")&&!lock_pin_verify("7390")&&!lock_pin_verify("a391")&&!lock_pin_verify("73911"));
 char digest[65];for(unsigned i=0;i<32;++i)snprintf(digest+2*i,65-2*i,"%02x",durable.digest[i]);assert(!strcmp(digest,"EXPECTED"));
 for(unsigned i=0;i+4<=sizeof(durable);++i)assert(memcmp((uint8_t*)&durable+i,"7391",4));
 pin_record_t old=durable;reboot();assert(lock_pin_enabled()&&lock_pin_verify("7391"));
 assert(lock_pin_replace("0000","4321")!=0&&!memcmp(&old,&durable,sizeof(old)));
 lock_pin_flow_begin(&f,false);assert(lock_pin_flow_input(&f,"0000")!=0&&f.step==PIN_FLOW_OLD);assert(lock_pin_flow_input(&f,"7391")==0&&f.step==PIN_FLOW_NEW);assert(lock_pin_flow_input(&f,"4321")==0);lock_pin_flow_end(&f);assert(lock_pin_verify("7391"));
 for(int i=1;i<=6;++i){crypto_fail=i;assert(!lock_pin_verify("7391")&&lock_pin_replace("7391","4321")!=0);crypto_fail=0;assert(lock_pin_verify("7391")&&!memcmp(&old,&durable,sizeof(old)));}
 for(unsigned i=0;i<2;++i){fail_write=i==0;fail_commit=i==1;assert(lock_pin_replace("7391","4321")!=0);fail_write=fail_commit=false;reboot();assert(lock_pin_verify("7391"));}
 lock_pin_flow_begin(&f,false);assert(lock_pin_flow_input(&f,"7391")==0&&lock_pin_flow_input(&f,"4321")==0&&lock_pin_flow_input(&f,"4321")==0);assert(lock_pin_verify("4321")&&!lock_pin_verify("7391")&&memcmp(old.salt,durable.salt,16));
 old=durable;durable.magic^=1;reboot();assert(lock_pin_enabled()&&!lock_pin_available()&&!lock_pin_verify("4321")&&lock_pin_replace(NULL,"1234")!=0);durable=old;
 fail_read=true;reboot();assert(lock_pin_enabled()&&!lock_pin_available());fail_read=false;reboot();assert(lock_pin_verify("4321"));
 lock_pin_flow_begin(&f,true);assert(lock_pin_flow_input(&f,"0000")!=0&&lock_pin_enabled());lock_pin_flow_end(&f);assert(lock_pin_enabled());
 lock_pin_flow_begin(&f,true);assert(lock_pin_flow_input(&f,"4321")==0&&f.step==PIN_FLOW_DONE&&!lock_pin_enabled());reboot();assert(!lock_pin_enabled()&&lock_pin_available());
 assert(aborts>10);puts("PASS: real PBKDF2 vector, random salt/no plaintext, constant-time digest, 4 digits; confirm/mismatch/cancel; old-PIN change/disable; NVS/crypto faults, cold reload and corrupt storage fail closed");
}
'''.replace('EXPECTED',expected)
    (p/'test.c').write_text(unit)
    crypto_flags=[] if sys.platform=='darwin' else ['-lcrypto']
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(p),'-I'+str(ROOT/'main'),str(p/'test.c'),'-o',str(p/'test')]+crypto_flags,check=True)
    subprocess.run([str(p/'test')],check=True)
