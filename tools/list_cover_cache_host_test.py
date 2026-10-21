#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 中文：列表封面尺寸/缓存/低内存边界，执行实际加载函数。
# English: Execute actual list-cover loading across aspect, cache and memory boundaries.
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
source = ROOT / 'main/book/book_cover.c'
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <errno.h>
#define BOOK_COVER_W 176
#define BOOK_COVER_H 240
#define BOOK_AUTO_COVER_VERSION 1
#define COVER_CACHE_MAGIC 1234
#define COVER_CACHE_SCHEMA 1
#define ESP_OK 0
#define ESP_ERR_NOT_FOUND 2
#define ESP_ERR_NO_MEM 3
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
typedef int esp_err_t;
typedef struct {uint32_t magic,schema,template_version,width,height;uint64_t file_size;int64_t modified;uint64_t path_hash,text_hash;} cover_cache_header_t;
static unsigned source_w=300,source_h=600,requests,decoded;
static size_t available=8u*1024u*1024u;
static int error;
static bool decode_fail;
static bool book_title_from_path(const char *p,char *out,size_t n){(void)p;snprintf(out,n,"title");return true;}
static size_t heap_caps_get_free_size(int flags){assert(flags==3);return available;}
static esp_err_t book_epub_cover_bounded(const char *p,uint8_t **data,size_t *bytes,bool *png,size_t budget){(void)p;assert(budget<=4u*1024u*1024u&&budget+1536u*1024u<=available);++requests;if(error)return error;*data=malloc(1);assert(*data);**data=1;*bytes=1;*png=false;return ESP_OK;}
static bool book_image_dimensions(const uint8_t *data,size_t bytes,bool png,unsigned *w,unsigned *h){(void)png;assert(data&&bytes==1);*w=source_w;*h=source_h;return true;}
static bool book_image_grayscale(const uint8_t *data,size_t bytes,bool png,unsigned w,unsigned h,uint8_t *out){(void)png;assert(data&&bytes==1&&w&&h&&w<=176&&h<=240);++decoded;if(decode_fail)return false;memset(out,61,w*h);return true;}
static bool book_auto_cover_render(const char *p,const char *t,const char *a,unsigned w,unsigned h,uint8_t *out){(void)p;(void)t;(void)a;assert(w==176&&h==240);memset(out,191,w*h);return true;}
'''
for name in ('cover_hash', 'cover_cache_key', 'book_cover_load_list_gray_impl', 'book_cover_load_list_gray'):
    unit += function(name, source) + '\n'
unit += r'''
int main(int argc,char **argv){
 assert(argc==2);char path[160],cache[120];snprintf(path,sizeof(path),"%s/book.epub",argv[1]);FILE *f=fopen(path,"wb");assert(f&&fputs("EPUB",f)>=0&&!fclose(f));
 uint8_t out[176*240+32];memset(out,0xab,sizeof(out));unsigned w,h;bool pending;
 assert(!book_cover_load_list_gray(path,"title","author",out,false,&pending,&w,&h)&&pending&&!requests);
 assert(book_cover_load_list_gray(path,"title","author",out,true,&pending,&w,&h)&&!pending&&w==120&&h==240&&requests==1&&decoded==1);
 for(unsigned i=w*h;i<sizeof(out);++i)assert(out[i]==0xab);
 for(int i=0;i<30;++i)assert(book_cover_load_list_gray(path,"title","author",out,false,&pending,&w,&h)&&w==120&&h==240&&requests==1);
 cover_cache_header_t key;assert(cover_cache_key(path,"title","author",cache,sizeof(cache),&key));strcpy(strrchr(cache,'.'),"-list.bin");
 f=fopen(cache,"r+b");assert(f);assert(fread(&key,1,sizeof(key),f)==sizeof(key));key.width=0;rewind(f);assert(fwrite(&key,1,sizeof(key),f)==sizeof(key)&&!fclose(f));
 assert(!book_cover_load_list_gray(path,"title","author",out,false,&pending,&w,&h)&&pending);
 source_w=1000;source_h=250;assert(book_cover_load_list_gray(path,"title","author",out,true,&pending,&w,&h)&&w==176&&h==44);assert(!remove(cache));
 source_w=500;source_h=500;assert(book_cover_load_list_gray(path,"title","author",out,true,&pending,&w,&h)&&w==176&&h==176);assert(!remove(cache));
 available=1536u*1024u;unsigned before=requests;assert(book_cover_load_list_gray(path,"title","author",out,true,&pending,&w,&h)&&w==176&&h==240&&requests==before);assert(stat(cache,&(struct stat){0})!=0);
 available=8u*1024u*1024u;error=ESP_ERR_NO_MEM;assert(book_cover_load_list_gray(path,"title","author",out,true,&pending,&w,&h));assert(stat(cache,&(struct stat){0})!=0);
 error=ESP_OK;decode_fail=true;assert(book_cover_load_list_gray(path,"title","author",out,true,&pending,&w,&h));assert(stat(cache,&(struct stat){0})!=0);
 decode_fail=false;assert(book_cover_load_list_gray(path,"title","author",out,true,&pending,&w,&h));assert(!remove(cache));
 error=ESP_ERR_NOT_FOUND;assert(book_cover_load_list_gray(path,"title","author",out,true,&pending,&w,&h)&&w==176&&h==240);assert(book_cover_load_list_gray(path,"title","author",out,false,&pending,&w,&h));
 for(unsigned i=176*240;i<sizeof(out);++i)assert(out[i]==0xab);
 puts("PASS: uncropped wide/tall/square covers, exact cache dimensions, 30 warm reads, corrupt metadata, 1.5MiB reserve, OOM/decode failure retry and generated fallback bounds");
}
'''
with tempfile.TemporaryDirectory(prefix='plc-', dir='/private/tmp') as folder:
    p = Path(folder)
    unit = unit.replace('/sdcard/.readpico', str(p/'card'))
    (p/'test.c').write_text(unit)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    str(p/'test.c'), '-o', str(p/'test')], check=True)
    subprocess.run([str(p/'test'), folder], check=True)
