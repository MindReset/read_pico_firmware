#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 中文：执行真实拼贴/TTF绘制，检查原比例、同基线、缓存、损坏输入与低内存释放。
# English: Run real collage/TTF painting; check aspect, baselines, caches, corruption and low-memory cleanup.
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--font', type=Path, default=ROOT/'main/assets/builtin.ttf')
parser.add_argument('--covers', type=Path)
parser.add_argument('--output', type=Path)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='pico-lock-', dir='/private/tmp') as folder:
    work = Path(folder)
    (work/'books/nested').mkdir(parents=True)
    (work/'images').mkdir()
    dimensions = []
    if args.covers:
        from PIL import Image
    for i in range(15):
        if args.covers:
            image = Image.open(args.covers/f'{i+1:02}.png').convert('L')
            sw, sh = image.size
            image.thumbnail((380, 420))
            rw, rh = image.size
            gray = image.tobytes()
        else:
            sw, sh = 200+i*7, 300+i*4
            rw, rh = 60+i, 90
            gray = bytes((x*3+y+i*11)%256 for y in range(rh) for x in range(rw))
        dimensions.append((sw, sh))
        (work/f'images/{i:02}.raw').write_bytes(struct.pack('<4I', sw, sh, rw, rh)+gray)
        (work/f'books/nested/{i:02}.epub').write_bytes(bytes([i]))
    (work/'books/font.ttf').write_bytes(b'not a book')
    (work/'books/.hidden.epub').write_bytes(b'\0')
    (work/'books/link').symlink_to(work/'books', target_is_directory=True)
    (work/'esp_err.h').write_text('''#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_INVALID_RESPONSE 0x108
''')
    (work/'esp_log.h').write_text('''#pragma once
static inline void host_log(const char *tag,const char *fmt,...){(void)tag;(void)fmt;}
#define ESP_LOGI(...) host_log(__VA_ARGS__)
#define ESP_LOGW(...) host_log(__VA_ARGS__)
#define ESP_LOGE(...) host_log(__VA_ARGS__)
''')
    (work/'esp_timer.h').write_text('#pragma once\n#include <stdint.h>\nstatic inline int64_t esp_timer_get_time(void){return 0;}\n')
    (work/'esp_heap_caps.h').write_text('''#pragma once
#include <stddef.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
void *heap_caps_malloc(size_t n,int caps);
void *heap_caps_calloc(size_t n,size_t s,int caps);
void heap_caps_free(void *p);
size_t heap_caps_get_largest_free_block(int caps);
size_t heap_caps_get_free_size(int caps);
''')
    (work/'nvs.h').write_text('''#pragma once
#include <stddef.h>
#include "esp_err.h"
typedef unsigned nvs_handle_t;
#define NVS_READONLY 0
esp_err_t nvs_open(const char *,int,nvs_handle_t *);
esp_err_t nvs_get_str(nvs_handle_t,const char *,char *,size_t *);
void nvs_close(nvs_handle_t);
''')
    font = (ROOT/'main/assets/builtin.ttf').read_bytes()
    assets = '#include <stdint.h>\nconst uint8_t data[] asm("_binary_builtin_ttf_start")={' + ','.join(map(str,font)) + '};\n'
    assets += f'asm(".globl _binary_builtin_ttf_end\\n.set _binary_builtin_ttf_end, _binary_builtin_ttf_start + {len(font)}");\n'
    (work/'assets.c').write_text(assets)
    unit = r'''
// 本测试自带按旋转换算的尺寸替身，必须在任何头被拉进来之前关掉 stub 的 static inline 版，
// 否则后到的 include 会先定义它、与下面的实现重定义。
// This test ships its own rotation-aware size shim, so the static inline one has to be switched
// off before any header is pulled in, or a later include defines it first and clashes below.
#define EPDIY_STUB_NO_ROTATED_SIZE
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "ttf_font.h"
// 本测试自带按旋转换算的尺寸替身，关掉 stub 里的 static inline 版以免重定义。
// This test ships its own rotation-aware size shim, so switch off the static inline one.
#define EPDIY_STUB_NO_ROTATED_SIZE
#include "epdiy.h"
#include <sys/stat.h>
static struct {void *p;size_t size;} allocations[8192];
static size_t live,peak,capacity=8*1024*1024;
static int fail_after=-1;
static unsigned decoded,cover_requests;
static unsigned plotted,recent_bump;
static int promoted=-1;
static const char *fixtures;
static const char *profile="Kiiko";
static const char *hidden_path;
static enum EpdRotation rotation=EPD_ROT_PORTRAIT;
void *heap_caps_malloc(size_t n,int caps){(void)caps;if(fail_after==0)return NULL;if(fail_after>0)--fail_after;if(n>capacity-live)return NULL;void *p=malloc(n);if(!p)return NULL;for(unsigned i=0;i<8192;++i)if(!allocations[i].p){allocations[i].p=p;allocations[i].size=n;live+=n;if(live>peak)peak=live;return p;}abort();}
void *heap_caps_calloc(size_t n,size_t s,int caps){if(s&&n>SIZE_MAX/s)return NULL;void *p=heap_caps_malloc(n*s,caps);if(p)memset(p,0,n*s);return p;}
void host_free(void *p){if(!p)return;for(unsigned i=0;i<8192;++i)if(allocations[i].p==p){live-=allocations[i].size;allocations[i].p=NULL;break;}free(p);}
void heap_caps_free(void *p){host_free(p);}
size_t heap_caps_get_free_size(int caps){(void)caps;return capacity-live;}
size_t heap_caps_get_largest_free_block(int caps){(void)caps;return capacity-live;}
static unsigned font_stats,book_stats;
static size_t cache_payload_read;
static unsigned directory_scans;
#include <dirent.h>
static DIR *counted_opendir(const char *p){++directory_scans;return opendir(p);}
static int host_stat(const char *p,struct stat *s){if(!strcmp(p,ttf_font_path()))++font_stats;else ++book_stats;return stat(p,s);}
static size_t host_fread(void *p,size_t n,size_t c,FILE *f){size_t r=fread(p,n,c,f);if(n*c>512)cache_payload_read+=n*r;return r;}
#define stat(...) host_stat(__VA_ARGS__)
#define fread host_fread
#define free host_free
#define opendir counted_opendir
#include "book_lock_collage.c"
#undef opendir
#undef stat
#undef fread
#undef free
unsigned book_store_revision(void){return 0;}
const char *app_settings_device_name(void){return profile;}
const char *app_settings_fonts_dir(void){return "/absent";}
const char *app_settings_font_path(void){return "builtin";}
bool ui_font_has_text(const char *s){(void)s;return true;}
int ui_font_title_px(int px,const char *s){(void)s;return px;}
int ui_font_text_width_px(int px,const char *s){(void)px;(void)s;assert(0);return 0;}
void ui_font_measure_line_px(int px,const char *s,int *a,int *b){(void)px;(void)s;(void)a;(void)b;assert(0);}
void ui_font_draw_title_px(uint8_t *fb,int x,int y,int px,const char *s,enum EpdFontFlags a){(void)fb;(void)x;(void)y;(void)px;(void)s;(void)a;assert(0);}
int epd_width(void){return 1216;}int epd_height(void){return 684;}
int epd_rotated_display_width(void){return 684;}int epd_rotated_display_height(void){return 1216;}
enum EpdRotation epd_get_rotation(void){return rotation;}
void epd_draw_pixel(int x,int y,uint8_t c,uint8_t *fb){if(x<0||y<0||x>=684||y>=1216)return;++plotted;unsigned at=y*684+x;unsigned q=c>>4;if(at&1)fb[at/2]=(fb[at/2]&15)|(q<<4);else fb[at/2]=(fb[at/2]&240)|q;}
esp_err_t nvs_open(const char *s,int m,nvs_handle_t *h){(void)s;(void)m;*h=1;return ESP_OK;}
esp_err_t nvs_get_str(nvs_handle_t h,const char *k,char *s,size_t *n){(void)h;(void)k;if(!hidden_path)return ESP_ERR_NOT_FOUND;size_t z=strlen(hidden_path)+1;if(*n<z)return ESP_FAIL;memcpy(s,hidden_path,z);*n=z;return ESP_OK;}
void nvs_close(nvs_handle_t h){assert(h==1);}
// 形参写成数组形式以匹配 book_store.h 的声明：形式不一致会被 -Werror=array-parameter 拦下。
// Spell the parameter as an array to match book_store.h; a mismatched form trips
// -Werror=array-parameter.
esp_err_t book_store_roots(book_store_root_t r[BOOK_STORE_ROOT_MAX],int *n){snprintf(r[0].path,sizeof(r[0].path),"%s/books",fixtures);*n=1;return ESP_OK;}
bool book_store_roots_degraded(void){return false;}
esp_err_t book_progress_list(book_progress_visit_fn f,void *c){(void)f;(void)c;return ESP_OK;}
bool book_progress_load(const char *p,uint32_t s,book_progress_t *out){(void)s;const char *name=strrchr(p,'/');int id=atoi(name+1);out->last_open_s=100-(unsigned)id+recent_bump+(id==promoted?1000:0);return true;}
bool book_title_from_path(const char *p,char *s,size_t n){const char *name=strrchr(p,'/');snprintf(s,n,"%s",name?name+1:p);char *ext=strrchr(s,'.');if(ext)*ext=0;return true;}
bool pico_boot_asset_allowed(const char *p){(void)p;return true;}
esp_err_t book_epub_cover_bounded(const char *p,uint8_t **d,size_t *n,bool *png,size_t budget){++cover_requests;FILE *book=fopen(p,"rb");if(!book)return ESP_ERR_NOT_FOUND;int id=fgetc(book);fclose(book);char path[256];snprintf(path,sizeof(path),"%s/images/%02d.raw",fixtures,id);FILE *f=fopen(path,"rb");if(!f)return ESP_ERR_NOT_FOUND;assert(!fseek(f,0,SEEK_END));*n=ftell(f);rewind(f);if(*n>budget){fclose(f);return ESP_ERR_NO_MEM;}*d=heap_caps_malloc(*n,3);if(!*d){fclose(f);return ESP_ERR_NO_MEM;}assert(fread(*d,1,*n,f)==*n);fclose(f);*png=true;return ESP_OK;}
bool book_image_dimensions(const uint8_t *d,size_t n,bool png,unsigned *w,unsigned *h){(void)png;if(n<16)return false;uint32_t header[4];memcpy(header,d,16);*w=header[0];*h=header[1];return true;}
bool book_image_grayscale(const uint8_t *d,size_t n,bool png,unsigned w,unsigned h,uint8_t *out){(void)png;uint32_t header[4];memcpy(header,d,16);assert(n==16+(size_t)header[2]*header[3]);++decoded;for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)out[y*w+x]=d[16+(y*header[3]/h)*header[2]+x*header[2]/w];return true;}
bool book_auto_cover_render(const char *p,const char *t,const char *a,unsigned w,unsigned h,uint8_t *out){(void)p;(void)t;(void)a;memset(out,200,w*h);return true;}
static void corrupt_frame(void){FILE *f=fopen(LOCK_CACHE_DIR "/collage.bin","r+b");assert(f);assert(fseek(f,sizeof(lock_cache_t)+50,SEEK_SET)==0);int c=fgetc(f);assert(fseek(f,-1,SEEK_CUR)==0&&fputc(c^1,f)!=EOF);fclose(f);}
int main(int argc,char **argv){
 assert(argc==4);fixtures=argv[1];assert(ttf_font_open(argv[2])==ESP_OK);assert(!ttf_font_is_builtin());
 uint8_t warm_mask[128*80]={0};assert(ttf_text_mask_px(warm_mask,128,80,0,50,48,"本"));ttf_font_unload();size_t shared_io=live;assert(ttf_font_open(argv[2])==ESP_OK);
 uint8_t *fb=malloc(LOCK_FRAME_BYTES),*other=malloc(LOCK_FRAME_BYTES);assert(fb&&other);
 font_stats=book_stats=0;
 lock_library_t *lib=library_load();assert(lib&&lib->count==15&&lib->used==15&&lib->cache_complete);
 assert(font_stats==1&&book_stats==0);
 size_t cover_bytes=0;
 for(unsigned i=0;i<15;++i){uint8_t *g=cover_load(&lib->books[i],&lib->cache_complete);assert(g&&lib->books[i].source_h);cover_bytes+=(size_t)lib->books[i].width*lib->books[i].height;host_free(g);}
 uint8_t pale[16];memset(pale,255,sizeof(pale));lock_book_t sample={.width=4,.height=4};
 assert(pale_cover(pale,&sample));memset(pale,80,sizeof(pale));assert(!pale_cover(pale,&sample));
 assert(cover_edge(0.5f,30,220)&&cover_edge(219.5f,30,220)&&!cover_edge(110,100,220));
 assert(cover_edge(4.8f,4.8f,220)&&!inside_cover(0,0,220));
 lock_placement_t p[30];assert(make_placements(lib,p)==30);
 for(unsigned i=0;i<30;++i){const lock_book_t *b=&lib->books[p[i].index];assert(fabsf(p[i].w/334.f-(float)b->source_w/b->source_h)<.00001f);if(i%5)assert(fabsf(p[i].x-p[i-1].x-p[i-1].w-18.f)<.001f);}
 host_free(lib);assert(decoded==15);
 char hidden[256];snprintf(hidden,sizeof(hidden),"%s/books/nested/01.epub",fixtures);hidden_path=hidden;book_lock_collage_invalidate();
 lib=library_load();assert(lib&&lib->count==14&&lib->used==14);host_free(lib);
 hidden_path="different path with colliding key";book_lock_collage_invalidate();lib=library_load();assert(lib&&lib->count==15);host_free(lib);hidden_path=NULL;book_lock_collage_invalidate();
 // 书架空闲预生成不保留像素，正式锁屏读取同样的原比例缓存。
 // Idle shelf warming retains no pixels; the final lock consumes identical aspect-preserved caches.
 size_t before_warm=live;
 for(unsigned id=0;id<3;++id){char warm_path[512];snprintf(warm_path,sizeof(warm_path),"%s/books/nested/%02u.epub",fixtures,id);assert(book_lock_collage_warm_cover(warm_path));assert(live==before_warm);unsigned before_decode=decoded;assert(book_lock_collage_warm_cover(warm_path)&&decoded==before_decode&&live==before_warm);}
 assert(!book_lock_collage_warm_cover(NULL));capacity=live+2u*1024u*1024u;
 char blocked_warm[512];snprintf(blocked_warm,sizeof(blocked_warm),"%s/books/nested/04.epub",fixtures);assert(!book_lock_collage_warm_cover(blocked_warm));capacity=8*1024*1024;
 font_stats=book_stats=0;
 assert(book_lock_collage_draw(fb));assert(decoded==15);unsigned requests=cover_requests;size_t warm=live;
 // 提交前画面已完整，新整帧不落卡，尺寸阶段不重复读取整份封面像素。
 // Complete art precedes persistence, and the dimensions pass does not reread cover pixels.
 assert(remove(LOCK_CACHE_DIR "/collage.bin")==0);cache_payload_read=0;
 assert(book_lock_collage_prepare(other)&&!memcmp(fb,other,LOCK_FRAME_BYTES)&&live==warm);
 assert(cache_payload_read==cover_bytes&&s_pending_frame==other);
 FILE *missing=fopen(LOCK_CACHE_DIR "/collage.bin","rb");assert(!missing);
 book_lock_collage_cache_commit(fb);assert(!s_pending_frame);
 missing=fopen(LOCK_CACHE_DIR "/collage.bin","rb");assert(!missing);
 assert(book_lock_collage_prepare(other)&&!memcmp(fb,other,LOCK_FRAME_BYTES));
 assert(!book_lock_collage_prepare(NULL)&&!s_pending_frame);
 book_lock_collage_cache_commit(other);missing=fopen(LOCK_CACHE_DIR "/collage.bin","rb");assert(!missing);
 assert(book_lock_collage_prepare(other));book_lock_collage_cache_commit(other);assert(!s_pending_frame);
 unsigned plots_before=plotted;assert(book_lock_collage_draw(other)&&plotted==plots_before&&!memcmp(fb,other,LOCK_FRAME_BYTES));
 unsigned scans_before=directory_scans;
 for(unsigned i=0;i<4;++i){assert(book_lock_collage_draw(other));assert(!memcmp(fb,other,LOCK_FRAME_BYTES)&&live==warm&&cover_requests==requests);}
 assert(directory_scans==scans_before);
 // A timestamp-only progress save keeps identical artwork and bypasses rasterization entirely.
 unsigned previous_plots=plotted;recent_bump=100;
 assert(book_lock_collage_draw(other)&&!memcmp(fb,other,LOCK_FRAME_BYTES));
 assert(plotted==previous_plots&&live==warm&&cover_requests==requests);
 // A genuinely changed recency order must rebuild, then restoring it reproduces the original pixels.
 promoted=14;
 assert(book_lock_collage_draw(other)&&memcmp(fb,other,LOCK_FRAME_BYTES)&&plotted>previous_plots);
 promoted=-1;recent_bump=0;
 assert(book_lock_collage_draw(other)&&!memcmp(fb,other,LOCK_FRAME_BYTES));
 FILE *preview=fopen(argv[3],"wb");assert(preview&&fwrite(fb,1,LOCK_FRAME_BYTES,preview)==LOCK_FRAME_BYTES&&fclose(preview)==0);
 corrupt_frame();assert(book_lock_collage_draw(other));assert(!memcmp(fb,other,LOCK_FRAME_BYTES)&&decoded==15);
 profile="自定义名字";assert(book_lock_collage_draw(other)&&memcmp(fb,other,LOCK_FRAME_BYTES));assert(decoded==15);profile="名字很长名字很长名字很长名字很长名字很长";assert(book_lock_collage_draw(other));profile="Kiiko";
 // 损坏封面头不能把无效比例带入几何计算；重解码后重新建立缓存。
 // Invalid cover metadata cannot enter geometry; decode again and rebuild the cache.
 lib=library_load();assert(lib);char cached_cover[112];image_cache_path(&lib->books[0],cached_cover);
 FILE *bad=fopen(cached_cover,"r+b");assert(bad);lock_cache_t header;assert(fread(&header,1,sizeof(header),bad)==sizeof(header));header.source_h=0;rewind(bad);assert(fwrite(&header,1,sizeof(header),bad)==sizeof(header));fclose(bad);
 assert(!cover_dimensions_read(&lib->books[0]));
 unsigned prior_decoded=decoded;uint8_t *repaired=cover_load(&lib->books[0],&lib->cache_complete);assert(repaired&&decoded==prior_decoded+1&&lib->books[0].source_h);host_free(repaired);
 // 尺寸头可读时，损坏像素仍须在正式绘制时校验并修复。
 // Valid dimensions never skip checksum validation of damaged pixels in the drawing pass.
 bad=fopen(cached_cover,"r+b");assert(bad&&fseek(bad,sizeof(lock_cache_t)+4,SEEK_SET)==0);int badpixel=fgetc(bad);assert(fseek(bad,-1,SEEK_CUR)==0&&fputc(badpixel^1,bad)!=EOF);fclose(bad);
 assert(cover_dimensions_read(&lib->books[0]));host_free(lib);
 assert(remove(LOCK_CACHE_DIR "/collage.bin")==0);unsigned before_repair=decoded;
 assert(book_lock_collage_prepare(other)&&decoded==before_repair+1&&!memcmp(fb,other,LOCK_FRAME_BYTES));book_lock_collage_cache_commit(other);
 rotation=EPD_ROT_INVERTED_PORTRAIT;assert(book_lock_collage_draw(other));rotation=EPD_ROT_PORTRAIT;
 uint8_t small[64]={0};assert(!ttf_text_mask_px(small,700,1,0,0,48,"本"));assert(!ttf_text_mask_px(small,8,181,0,0,48,"本"));
 // 每个阶段强制分配失败；绘制失败不留持久内存，也不会对启动恢复状态进行写入。
 // Inject allocation failure at each stage; failed paints retain no workspace or boot-resume writes.
 ttf_font_cache_clear();size_t resident=live;
 for(int i=0;i<10;++i){fail_after=i;(void)remove(LOCK_CACHE_DIR "/collage.bin");(void)book_lock_collage_draw(other);fail_after=-1;ttf_font_cache_clear();assert(live==resident);}
 capacity=live+LOCK_RESERVE;assert(!book_lock_collage_draw(other));capacity=8*1024*1024;
 // 单本书仍平铺；空书架只显示真实的零本计数。
 // A single book still tiles; an empty library shows the actual zero count.
 char path[256];for(unsigned i=1;i<15;++i){snprintf(path,sizeof(path),"%s/books/nested/%02u.epub",fixtures,i);assert(remove(path)==0);}
 lib=library_load();assert(lib&&lib->count==1&&lib->used==1);host_free(lib);assert(book_lock_collage_draw(other));
 snprintf(path,sizeof(path),"%s/books/nested/00.epub",fixtures);assert(remove(path)==0);lib=library_load();assert(lib&&lib->count==0&&!lib->used);host_free(lib);assert(book_lock_collage_draw(other));
 printf("PASS: real imported TTF collage; 15 original ratios / 15-degree tilt / 18px gaps / 16-gray; cache reuse+corruption, 0/1/15 books and allocation failure cleanup. Peak PSRAM working allocation=%zu bytes\n",peak);
 book_lock_collage_invalidate();ttf_font_unload();printf("Retained shared TTF I/O workspace=%zu bytes\n",live);assert(live==shared_io);free(fb);free(other);
}
'''
    (work/'test.c').write_text(unit)
    # ttf_font.c 的有界 snprintf 会被 gcc 判为可能截断；与 book_ui 等测试一样只降这一级。
    # gcc flags the bounded snprintf in ttf_font.c as possibly truncating; downgrade just that one
    # check, the same way the book_ui and refresh-feedback tests already do.
    flags=['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable',
           '-Wno-missing-field-initializers','-Wno-error=format-truncation','-fsanitize=address,undefined','-ffunction-sections','-fdata-sections',
           '-I'+str(work),'-I'+str(ROOT/'main/book'),'-I'+str(ROOT/'main/font'),
           '-I'+str(ROOT/'main/ui'),'-I'+str(ROOT/'main'),'-I'+str(ROOT/'tools/ui_gesture_stubs')]
    subprocess.run(flags+['-c',str(ROOT/'main/font/ttf_font.c'),'-o',str(work/'ttf.o')],check=True)
    gc='-Wl,-dead_strip' if os.uname().sysname=='Darwin' else '-Wl,--gc-sections'
    subprocess.run(flags+[f'-DBOOK_LOCK_CACHE_PARENT="{work}/cache"',str(work/'test.c'),str(work/'assets.c'),
                          str(work/'ttf.o'),str(ROOT/'main/ui/ui_image_dither.c'),'-lm',gc,'-o',str(work/'test')],check=True)
    subprocess.run([str(work/'test'),str(work),str(args.font.resolve()),str(work/'frame.raw')],check=True)
    if args.output:
        from PIL import Image
        raw=(work/'frame.raw').read_bytes()
        image=Image.frombytes('L',(684,1216),bytes((raw[i//2]>>(4*(i%2))&15)*17 for i in range(684*1216)))
        args.output.parent.mkdir(parents=True,exist_ok=True)
        image.save(args.output)
        args.output.with_suffix('.raw').write_bytes(raw)
