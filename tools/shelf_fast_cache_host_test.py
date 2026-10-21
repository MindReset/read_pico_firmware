"""中文：运行真实封面缓存，验证内存上限、失效与重复切页复用。
English: Exercise actual cover caching, its memory bound, invalidation and repeated-tab reuse.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(name, path):
    text = path.read_text()
    scan = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"',
                  lambda m: ' ' * len(m[0]), text)
    start = re.search(r'^(?:static )?[^\n]+\b' + name + r'\([^;{}]*?\)\s*\{', scan, re.M)
    assert start, name
    at, depth = start.end(), 1
    while depth:
        depth += (scan[at] == '{') - (scan[at] == '}')
        at += 1
    return text[start.start():at]


book = ROOT / 'main/apps/app_book.c'
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui_image_dither.h"
#define BOOK_ROWS 13
#define BOOK_GRID_ROWS 9
#define BOOK_STORE_PATH_MAX 288
#define BOOK_COVER_W 176
#define BOOK_COVER_H 240
#define SHELF_FAST_COVER_WIDTH 164
#define SHELF_FAST_COVER_HEIGHT 214
#define SHELF_FAST_COVER_STRIDE ((SHELF_FAST_COVER_WIDTH+7)/8)
#define SHELF_FAST_COVER_BYTES (SHELF_FAST_COVER_STRIDE*SHELF_FAST_COVER_HEIGHT)
#define SHELF_FAST_CACHE_RESERVE (512u*1024u)
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {unsigned x,y,width,height;} book_crop_t;
enum {SHELF,MANAGE};
static int s_view=SHELF,rows=9,contrast=100,phase_shift,style=2;
static bool fast=true,alloc_fail;
static size_t free_psram=4*1024*1024,allocated_bytes;
static void release_guard_cache(void){}
static unsigned allocations,s_cover_pending_mask,s_cover_decode_mask,s_cover_warm_mask,s_cover_dirty_mask,s_cover_present_mask;
static struct {int index;uint8_t *gray,*fast_bits;unsigned width,height;uint8_t fast_contrast,fast_phase;bool fast_white_edge;} s_covers[BOOK_ROWS];
static int app_settings_shelf_style(void){return style;}
static int shelf_rows(void){return rows;}
static bool app_settings_main_fast_refresh(void){return fast;}
static uint8_t app_settings_system_contrast(void){return contrast;}
static struct {void *ptr;size_t bytes;} live[40];
static void* remember(void *ptr,size_t bytes){if(!ptr)return NULL;for(int i=0;i<40;++i)if(!live[i].ptr){live[i].ptr=ptr;live[i].bytes=bytes;allocated_bytes+=bytes;return ptr;}assert(0);return NULL;}
static void tracked_free(void *ptr){if(!ptr)return;for(int i=0;i<40;++i)if(live[i].ptr==ptr){allocated_bytes-=live[i].bytes;live[i].ptr=NULL;free(ptr);return;}assert(0);}
#define free tracked_free
static uint8_t source_pixel(int row,int p){return (uint8_t)(p*19+row*31);}
static void seed_gray(int row){assert(!s_covers[row].gray);s_covers[row].gray=remember(malloc(176*240),176*240);for(int p=0;p<176*240;++p)s_covers[row].gray[p]=source_pixel(row,p);}
// r93 新增的封面批次协作件：本测试只关心缓存记账，取消批次按空实现记账。
// r93's cover-batch collaborators: this test only tracks cache accounting, so cancelling
// a batch is a no-op.
static unsigned s_cover_generation;
unsigned book_cover_jobs_begin(void){return 1;}
void book_cover_jobs_cancel(unsigned generation){(void)generation;}
static size_t heap_caps_get_free_size(unsigned caps){assert(caps==3);return free_psram;}
static void* heap_caps_calloc(size_t count,size_t bytes,unsigned caps){
    assert(caps==3 && count==1 && bytes==4494);++allocations;
    if(alloc_fail)return NULL;
    return remember(calloc(count,bytes),bytes);
}
static EpdRect row_rect(int row){return (EpdRect){36+(row%3)*210+phase_shift,224+(row/3)*282,192,260};}
'''
unit += function('ui_contrast_gray', ROOT / 'main/ui/ui_kit.c') + '\n'
unit += function('book_cover_crop', ROOT / 'main/book/book_cover.c') + '\n'
unit += function('cover_favorite_needs_white_edge', book) + '\n'
for name in ('shelf_cover_image', 'invalidate_covers', 'release_fast_covers', 'fast_cover_matches', 'prepare_fast_covers', 'app_book_cover_mode_changed'):
    unit += function(name, book) + '\n'
unit += r'''
typedef struct {int leaf;} app_ctx_t;
typedef struct {uint8_t pct;uint32_t chapter,last_open_s;} book_progress_t;
typedef struct {char path[288];bool removed,has_progress;uint32_t size,chapter,recent;uint8_t pct;} shelf_entry_t;
static shelf_entry_t s_shelf[1];
static int s_count=1,sorts;
static uint32_t s_progress_revision=UINT32_MAX,progress_revision=1;
static uint32_t book_progress_revision(void){return progress_revision;}
static void shelf_category_restore(app_ctx_t* ctx){(void)ctx;}
static void shelf_categories_recount(void){}
static bool s_recent_sort=true,added,last_valid=true,progress_valid=true;
static char s_latest_path[288];
static book_progress_t test_progress={37,9,42};
static bool shelf_backfill_read_books(bool* trunc){assert(!trunc);return added;}
static bool book_progress_last_path(char* out,size_t cap){assert(cap>=10);strcpy(out,"/test.epub");return last_valid;}
static bool book_progress_load(const char* path,uint32_t size,book_progress_t* p){assert(!strcmp(path,"/test.epub") && size==100);*p=test_progress;return progress_valid;}
static void copy_text(char* dst,size_t cap,const char* src){assert(strlen(src)<cap);strcpy(dst,src);}
static void sort_shelf(app_ctx_t* ctx){(void)ctx;++sorts;}
'''
unit += function('refresh_cached_progress', book) + '\n'
unit += r'''
static void verify_bitmap(int row) {
    EpdRect card=row_rect(row);
    int image_x=card.x+(card.width-164)/2,image_y=card.y;
    book_crop_t crop=book_cover_crop(176,240,164,214);
    assert(fast_cover_matches(row,(EpdRect){image_x,image_y,164,214}));
    for(int y=0;y<214;++y)for(int x=0;x<164;++x) {
        unsigned sx=crop.x+(uint64_t)(unsigned)x*crop.width/164;
        unsigned sy=crop.y+(uint64_t)(unsigned)y*crop.height/214;
        uint8_t tone=(ui_contrast_gray(source_pixel(row,sy*176+sx))>>4)*17u;
        int expected=ui_image_dither_cover_bw(tone,image_x+x,image_y+y)!=0;
        assert(!!(s_covers[row].fast_bits[y*21+x/8]&(1u<<(x&7)))==expected);
    }
    assert(fast_cover_matches(row,(EpdRect){image_x,image_y-16,164,214}));
}
int main(void) {
    for(int i=0;i<BOOK_ROWS;++i) {s_covers[i].index=i;seed_gray(i);}
    rows=13;prepare_fast_covers();
    assert(allocations==9 && allocated_bytes==40446);
    for(int row=0;row<9;++row)verify_bitmap(row);
    for(int row=0;row<13;++row)assert(!s_covers[row].gray);
    for(int row=9;row<13;++row)assert(!s_covers[row].fast_bits);
    unsigned before=allocations;
    for(int i=0;i<80;++i)prepare_fast_covers();
    assert(allocations==before && allocated_bytes==40446);
    // 原灰阶已经释放，修改对比度/位置的真实准备流程会重新提供解码缓冲。
    // Gray has been released; re-preparation supplies decoding buffers for changed contrast/position.
    contrast=200;for(int i=0;i<9;++i)seed_gray(i);prepare_fast_covers();assert(allocations==before+9);
    for(int row=0;row<9;++row){verify_bitmap(row);assert(!s_covers[row].gray);}
    phase_shift=1;before=allocations;for(int i=0;i<9;++i)seed_gray(i);prepare_fast_covers();assert(allocations==before+9);
    for(int row=0;row<9;++row)verify_bitmap(row);
    // 有效黑白不会因内存减少被丢弃；灰阶释放后仍保持同一份热缓存。
    // Memory pressure does not evict already valid packed artwork.
    free_psram=1;prepare_fast_covers();assert(allocated_bytes==40446);
    app_book_cover_mode_changed();assert(!allocated_bytes);
    for(int i=0;i<9;++i)seed_gray(i);prepare_fast_covers();
    for(int row=0;row<13;++row)assert(!s_covers[row].fast_bits && !s_covers[row].gray);
    assert(!allocated_bytes);
    free_psram=4*1024*1024;alloc_fail=true;for(int i=0;i<9;++i)seed_gray(i);prepare_fast_covers();
    for(int row=0;row<13;++row)assert(!s_covers[row].fast_bits && !s_covers[row].gray);
    assert(!allocated_bytes);
    alloc_fail=false;rows=9;for(int i=0;i<9;++i)seed_gray(i);prepare_fast_covers();
    fast=false;prepare_fast_covers();assert(!allocated_bytes);
    // 普通/水波纹只保留灰阶；模式变化释放，快刷再按需生成。
    // Ordinary/ripple retain gray only; mode changes release the obsolete representation.
    for(int i=0;i<9;++i)seed_gray(i);prepare_fast_covers();assert(allocated_bytes==9u*176*240);
    for(int row=0;row<13;++row)assert(!s_covers[row].fast_bits);
    app_book_cover_mode_changed();assert(!allocated_bytes);
    fast=true;for(int i=0;i<9;++i)seed_gray(i);prepare_fast_covers();s_view=MANAGE;prepare_fast_covers();assert(!allocated_bytes);
    s_view=SHELF;for(int i=0;i<9;++i)seed_gray(i);prepare_fast_covers();invalidate_covers();assert(!allocated_bytes);
    for(int row=0;row<13;++row)assert(!s_covers[row].gray && !s_covers[row].fast_bits && s_covers[row].index==-1);
    // 快刷封面白底纯白，黑区少量白点；字形与收藏在缓存之外绘制。
    // Pure white cover backgrounds remain white, while solid blacks contain sparse white dots; glyphs and favorites are outside the cache.
    int black=0,midgray=0;
    for(int y=0;y<4;++y)for(int x=0;x<4;++x){
        assert(ui_image_dither_cover_bw((ui_contrast_gray(255)>>4)*17u,x,y)==255);
        black+=ui_image_dither_cover_bw(0,x,y)==0;
        midgray+=ui_image_dither_cover_bw(128,x,y)==0;
    }
    assert(black==15 && midgray==8);
    invalidate_covers();style=5;rows=4;
    for(int i=0;i<4;++i){seed_gray(i);s_covers[i].width=i==0?176:i==1?70:160;s_covers[i].height=i==0?100:i==1?240:160;}
    prepare_fast_covers();assert(allocated_bytes==4u*4494);
    for(int row=0;row<4;++row){EpdRect image=shelf_cover_image(row);assert(image.width<=120&&image.height<=156&&image.width>0&&image.height>0);assert(fast_cover_matches(row,image));assert(!s_covers[row].gray);assert(image.x==71&&image.width==120&&image.height==156);}
    invalidate_covers();style=2;rows=9;
    app_ctx_t ctx={0};strcpy(s_shelf[0].path,"/test.epub");s_shelf[0].size=100;s_shelf[0].recent=42;
    for(int i=0;i<80;++i)refresh_cached_progress(&ctx);
    assert(sorts==0 && s_shelf[0].pct==37 && s_shelf[0].chapter==9);
    ++test_progress.last_open_s;++progress_revision;refresh_cached_progress(&ctx);assert(sorts==1);
    refresh_cached_progress(&ctx);assert(sorts==1);
    progress_valid=false;++progress_revision;refresh_cached_progress(&ctx);assert(sorts==2 && s_shelf[0].recent==0);
    refresh_cached_progress(&ctx);assert(sorts==2);
    added=true;s_recent_sort=false;++progress_revision;refresh_cached_progress(&ctx);assert(sorts==3);
    added=false;last_valid=false;++progress_revision;refresh_cached_progress(&ctx);assert(sorts==3);
    puts("shelf fast cache: 40,446-byte PSRAM bound; 80 hot reuses without allocation; exact cover pixels, contrast/phase invalidation, mutually exclusive gray/BW ownership, immediate gray release, low-memory/OOM fallback, reader/media release and unchanged-progress sort avoidance passed");
}
'''
with tempfile.TemporaryDirectory(dir=ROOT / 'build') as directory:
    src, exe = Path(directory) / 'test.c', Path(directory) / 'test'
    src.write_text(unit)
    # 抽取进来的产品源码有 gcc 认为缩进误导的写法（产品侧不动），只降这一级。
    # Pulled-in product sources trip gcc's misleading-indentation; downgrade just that check.
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-error=misleading-indentation', '-g', '-fsanitize=address,undefined',
                    '-I' + str(ROOT / 'main/ui'), str(src), str(ROOT / 'main/ui/ui_image_dither.c'),
                    '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
