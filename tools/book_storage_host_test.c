/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * NVS 故障替身验证进度、序号及遗忘重试。/ Fault-injected NVS tests for progress, sequence and forget retries.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "book_progress.h"
#include "book_store.h"
#include "nvs.h"
typedef struct { char key[16]; unsigned char data[512]; size_t len; } record_t;
static record_t records[8], *latest;
static char last[BOOK_STORE_PATH_MAX];
static bool has_last, has_sequence;
static uint32_t sequence;
static int blob_failure, seq_failure, last_failure, erase_failure, commit_failure, commits;
static record_t* record(const char* key, bool create) {
    for (int i=0;i<8;++i) if(records[i].len&&!strcmp(records[i].key,key))return &records[i];
    if(create)for(int i=0;i<8;++i)if(!records[i].len){strcpy(records[i].key,key);return &records[i];}
    return NULL;
}
esp_err_t nvs_open(const char* ns,int mode,nvs_handle_t* h){(void)mode;assert(!strcmp(ns,"rp_books"));*h=1;return ESP_OK;}
void nvs_close(nvs_handle_t h){(void)h;}
esp_err_t nvs_get_blob(nvs_handle_t h,const char* key,void* out,size_t* len){
    (void)h;record_t*r=record(key,false);if(!r)return ESP_ERR_NVS_NOT_FOUND;
    if(*len<r->len)return ESP_ERR_NVS_INVALID_LENGTH;
    memcpy(out,r->data,r->len);*len=r->len;return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char* key,const void* data,size_t len){
    (void)h;if(blob_failure)return blob_failure;record_t*r=record(key,true);assert(r&&len<=sizeof(r->data));
    memcpy(r->data,data,len);r->len=len;latest=r;return ESP_OK;
}
esp_err_t nvs_get_str(nvs_handle_t h,const char* key,char* out,size_t* len){
    (void)h;assert(!strcmp(key,"last"));if(!has_last)return ESP_ERR_NVS_NOT_FOUND;
    if(*len<strlen(last)+1)return ESP_ERR_NVS_INVALID_LENGTH;
    strcpy(out,last);*len=strlen(last)+1;return ESP_OK;
}
esp_err_t nvs_set_str(nvs_handle_t h,const char* key,const char* value){
    (void)h;assert(!strcmp(key,"last"));if(last_failure)return last_failure;strcpy(last,value);has_last=true;return ESP_OK;
}
esp_err_t nvs_get_u32(nvs_handle_t h,const char* key,uint32_t* value){
    (void)h;assert(!strcmp(key,"seq"));if(!has_sequence)return ESP_ERR_NVS_NOT_FOUND;*value=sequence;return ESP_OK;
}
esp_err_t nvs_set_u32(nvs_handle_t h,const char* key,uint32_t value){
    (void)h;assert(!strcmp(key,"seq"));if(seq_failure)return seq_failure;sequence=value;has_sequence=true;return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t h,const char* key){
    (void)h;if(!strcmp(key,"last")){if(last_failure)return last_failure;if(!has_last)return ESP_ERR_NVS_NOT_FOUND;has_last=false;last[0]=0;return ESP_OK;}
    if(erase_failure)return erase_failure;
    record_t*r=record(key,false);if(!r)return ESP_ERR_NVS_NOT_FOUND;r->len=0;return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h){(void)h;++commits;return commit_failure;}
int main(void){
    const char*sd="/sdcard/books/同名.txt",*flash="/flash/books/同名.txt";
    book_progress_watch_t* watch_a=book_progress_watch_create(sd);
    book_progress_watch_t* watch_b=book_progress_watch_create(flash);
    assert(watch_a&&watch_b&&!book_progress_watch_invalidated(watch_a));
    erase_failure=ESP_FAIL;
    assert(book_progress_forget(flash)==ESP_FAIL);
    assert(!book_progress_watch_invalidated(watch_a)&&book_progress_watch_invalidated(watch_b));
    assert(book_progress_forget(sd)==ESP_FAIL&&book_progress_watch_invalidated(watch_a));
    erase_failure=0;
    book_progress_watch_destroy(watch_b);book_progress_watch_destroy(watch_a);
    watch_a=book_progress_watch_create(sd);assert(watch_a&&!book_progress_watch_invalidated(watch_a));
    book_progress_watch_destroy(watch_a);
    book_progress_t p={.file_size=123456,.chapter=2047,.byte_off=9876,.px=48,.pct=83,.last_open_s=UINT32_MAX},q={.file_size=999};
    assert(!book_progress_load(sd,p.file_size,&q));
    assert(book_progress_save(sd,&p)==ESP_OK);
    assert(book_progress_load(sd,p.file_size,&q)&&q.last_open_s==1);
    assert(q.chapter==p.chapter&&q.byte_off==p.byte_off&&q.px==48&&q.pct==83);
    assert(!book_progress_load(sd,p.file_size+1,&q)&&q.file_size==p.file_size);
    p.last_open_s=0;assert(book_progress_save(sd,&p)==ESP_OK);
    assert(book_progress_load(sd,p.file_size,&q)&&q.last_open_s==2);
    latest->data[3]=1;assert(book_progress_load(sd,p.file_size,&q)&&q.last_open_s==0&&q.byte_off==p.byte_off);
    latest->data[3]=3;assert(!book_progress_load(sd,p.file_size,&q));assert(book_progress_save(sd,&p)==ESP_ERR_INVALID_STATE);
    latest->data[3]=2;assert(book_progress_set_last_path(sd)==ESP_OK);latest->data[24]='!';
    assert(book_progress_forget(sd)==ESP_ERR_INVALID_STATE&&!strcmp(last,sd));assert(book_progress_clear(sd)==ESP_ERR_INVALID_STATE);latest->data[24]='/';
    seq_failure=ESP_ERR_NVS_NOT_ENOUGH_SPACE;assert(book_progress_save(sd,&p)==seq_failure&&sequence==2);
    seq_failure=0;blob_failure=ESP_ERR_NVS_NOT_ENOUGH_SPACE;assert(book_progress_save(sd,&p)==blob_failure&&sequence==3);
    assert(book_progress_load(sd,p.file_size,&q)&&q.last_open_s==2);blob_failure=0;
    assert(book_progress_save(sd,&p)==ESP_OK);assert(book_progress_load(sd,p.file_size,&q)&&q.last_open_s==4);
    commit_failure=ESP_FAIL;assert(book_progress_save(sd,&p)==ESP_FAIL);
    assert(book_progress_load(sd,p.file_size,&q)&&q.last_open_s==4);commit_failure=0;
    assert(book_progress_save(flash,&p)==ESP_OK);assert(book_progress_set_last_path(flash)==ESP_OK);
    assert(book_progress_forget(sd)==ESP_OK);assert(!book_progress_load(sd,p.file_size,&q));
    assert(book_progress_load(flash,p.file_size,&q)&&!strcmp(last,flash));
    assert(book_progress_save(sd,&p)==ESP_OK);assert(book_progress_set_last_path(sd)==ESP_OK);
    last_failure=ESP_FAIL;assert(book_progress_forget(sd)==ESP_FAIL&&has_last);assert(book_progress_load(sd,p.file_size,&q));
    assert(book_progress_set_last_path(flash)==ESP_FAIL&&!strcmp(last,sd));last_failure=0;erase_failure=ESP_FAIL;
    assert(book_progress_forget(sd)==ESP_FAIL&&!has_last);assert(book_progress_load(sd,p.file_size,&q));erase_failure=0;
    assert(book_progress_forget(sd)==ESP_OK);assert(book_progress_forget(sd)==ESP_OK);assert(book_progress_load(flash,p.file_size,&q));
    assert(book_progress_save(sd,&p)==ESP_OK);assert(book_progress_set_last_path(sd)==ESP_OK);commit_failure=ESP_FAIL;
    assert(book_progress_forget(sd)==ESP_FAIL);commit_failure=0;assert(book_progress_forget(sd)==ESP_OK);
    assert(!book_progress_load(sd,p.file_size,&q)&&!has_last);
    char long_path[BOOK_STORE_PATH_MAX+1];memset(long_path,'x',sizeof(long_path));long_path[BOOK_STORE_PATH_MAX]=0;
    assert(book_progress_save(long_path,&p)==ESP_ERR_INVALID_ARG);long_path[BOOK_STORE_PATH_MAX-1]=0;
    assert(book_progress_save(long_path,&p)==ESP_OK);assert(book_progress_load(long_path,p.file_size,&q));
    assert(book_progress_set_last_path(long_path)==ESP_OK);char out[BOOK_STORE_PATH_MAX];
    assert(book_progress_last_path(out,sizeof(out))&&!strcmp(out,long_path));assert(!book_progress_last_path(out,2)&&!out[0]);
    assert(book_progress_forget(long_path)==ESP_OK&&!has_last);
    assert(book_progress_set_last_path("")==ESP_OK);assert(!book_progress_last_path(out,sizeof(out))&&!out[0]);
    p.px=37;assert(book_progress_save(sd,&p)==ESP_ERR_INVALID_ARG);p.px=48;p.pct=101;assert(book_progress_save(sd,&p)==ESP_ERR_INVALID_ARG);
    p.pct=83;sequence=UINT32_MAX;int before=commits;
    assert(book_progress_save(sd,&p)==ESP_ERR_INVALID_STATE&&commits==before&&sequence==UINT32_MAX);
    puts("book progress: v1/v2, sequence, collision, two roots, NVS-full, partial forget and retry passed");
}
