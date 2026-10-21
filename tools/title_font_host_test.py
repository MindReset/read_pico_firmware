#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 中文：标题使用真实活动TTF，检查原字号、文件句柄、缓存复用和字库资产不变。
# English: Exercise real active TTF titles, original sizes, file handles, cache reuse and unchanged font assets.
import os
import struct
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
font = (ROOT / 'main/assets/builtin.ttf').read_bytes()
with tempfile.TemporaryDirectory(dir=ROOT / 'build') as folder:
    work = Path(folder)
    (work / 'system.ttf').write_bytes(font)
    # 同一轮廓、不同字宽的字体可验证切页后确实采用新字体，而不是缓存上一份。
    # Equal outlines with distinct advances verify the new face is used after switching, not stale cached metrics.
    reader = bytearray(font)
    for i in range(struct.unpack_from('>H', font, 4)[0]):
        at = 12 + 16 * i
        if font[at:at + 4] == b'hmtx':
            offset, size = struct.unpack_from('>II', font, at + 8)
            for p in range(offset, offset + size - 3, 4):
                advance = struct.unpack_from('>H', reader, p)[0]
                struct.pack_into('>H', reader, p, advance + advance // 4)
    (work / 'reader.ttf').write_bytes(reader)
    (work / 'esp_err.h').write_text('''#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_INVALID_RESPONSE 0x108
''')
    (work / 'esp_log.h').write_text('''#pragma once
static inline void title_test_log(const char *tag,const char *fmt,...){(void)tag;(void)fmt;}
#define ESP_LOGI(...) title_test_log(__VA_ARGS__)
#define ESP_LOGW(...) title_test_log(__VA_ARGS__)
#define ESP_LOGE(...) title_test_log(__VA_ARGS__)
''')
    (work / 'esp_timer.h').write_text('''#pragma once
#include <stdint.h>
static inline int64_t esp_timer_get_time(void){return 0;}
''')
    (work / 'esp_heap_caps.h').write_text('''#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
void *heap_caps_malloc(size_t n,int caps);
void *heap_caps_calloc(size_t n,size_t s,int caps);
void heap_caps_free(void *p);
static inline size_t heap_caps_get_largest_free_block(int caps){(void)caps;return 0;}
''')
    assets = '#include <stdint.h>\nconst uint8_t data[] asm("_binary_builtin_ttf_start")={'
    assets += ','.join(map(str, font)) + '};\n'
    assets += f'asm(".globl _binary_builtin_ttf_end\\n.set _binary_builtin_ttf_end, _binary_builtin_ttf_start + {len(font)}");\n'
    (work / 'assets.c').write_text(assets)
    unit = r'''
#include "ttf_font.h"
#include "ui_kit.h"
#include <assert.h>
#include <fcntl.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef union {max_align_t align;size_t bytes;} allocation_t;
static size_t live,peak;
static unsigned opens,active_fd,max_fd,painted;
void *heap_caps_malloc(size_t n,int caps){(void)caps;allocation_t *p=malloc(sizeof(*p)+n);assert(p);p->bytes=n;live+=n;if(live>peak)peak=live;return p+1;}
void *heap_caps_calloc(size_t n,size_t s,int caps){void *p=heap_caps_malloc(n*s,caps);memset(p,0,n*s);return p;}
void heap_caps_free(void *p){if(!p)return;allocation_t *a=(allocation_t *)p-1;live-=a->bytes;free(a);}
int title_test_open(const char *path,int flags,...){int fd=open(path,flags,0600);if(fd>=0){++opens;++active_fd;if(active_fd>max_fd)max_fd=active_fd;}return fd;}
int title_test_close(int fd){assert(active_fd);--active_fd;return close(fd);}
const char *app_settings_fonts_dir(void){return "/absent";}
const char *app_settings_font_path(void){return "builtin";}
bool ui_font_has_text(const char *s){(void)s;return true;}
int ui_font_title_px(int px,const char *s){(void)s;return px;}
int ui_font_text_width_px(int px,const char *s){(void)px;(void)s;assert(0);return 0;}
void ui_font_measure_line_px(int px,const char *s,int *above,int *below){(void)px;(void)s;(void)above;(void)below;assert(0);}
void ui_font_draw_title_px(uint8_t *fb,int x,int y,int px,const char *s,enum EpdFontFlags a){(void)fb;(void)x;(void)y;(void)px;(void)s;(void)a;assert(0);}
// r93 起 ui_text_title_vc 等入口引用 ui_font_draw_text_px（内建 UI 字体分支）。本测试活动字体
// 恒为导入 TTF（!ttf_font_is_builtin()），该分支运行期永不进入，与上面几个桩同一语义。
// Since r93 the title/text entries reference ui_font_draw_text_px for the builtin-UI-font
// branch; this test always runs an imported TTF, so that branch is never taken at runtime,
// same never-taken semantics as the stubs above.
void ui_font_draw_text_px(uint8_t *fb,int x,int baseline,int px,const char *s,enum EpdFontFlags a,uint8_t fg,uint8_t bg,bool bw){(void)fb;(void)x;(void)baseline;(void)px;(void)s;(void)a;(void)fg;(void)bg;(void)bw;assert(0);}
static uint8_t pixels[400*200];
static bool record_coverage;
void epd_draw_pixel(int x,int y,uint8_t c,uint8_t *fb){assert(x>=0&&x<400&&y>=0&&y<200);if(record_coverage)fb[y*400+x]=c>>4;else{assert(c==0);fb[y*400+x]=1;}++painted;}
int main(int argc,char **argv){
 assert(argc==3&&ttf_get_antialias());const char *text="字体设置";unsigned widths[2];
 for(int face=0;face<2;++face){
  assert(ttf_font_open(argv[face+1])==ESP_OK&&ttf_font_ready()&&!ttf_font_is_builtin());
  assert(ttf_font_has_text(text)&&!ttf_font_has_text("\xf4\x8f\xbf\xbf"));
  unsigned initial_opens=opens;size_t resident=live;unsigned old_height=0;
  ui_text_set_system_font(!face);ui_text_set_system_scale(!face);
  const int sizes[]={32,40,48};
  for(unsigned i=0;i<3;++i){
   int px=sizes[i];char title[64];strcpy(title,text);
   assert(ui_text_title_fit(title,px,300,text)==px&&!strcmp(title,text));
   int above,below;ttf_measure_line_px(px,text,&above,&below);
   assert((unsigned)(above+below)>old_height);old_height=above+below;
   // AA 契约新 oracle（2026-10-16 重造；KNOWN_FAILURES.md 第 2 条触发器击发——第 3 步
   // 章首题头双栏化已动本调用点的几何入参 center_x/fit_w）。旧判据「桩强制 c==0 + 与
   // ttf_draw_text_px_bw 逐位恒等」编码 BW 时代旧约；产品自 r92 起 ui_text_title_vc 走
   // ttf_set_antialias(true)+ttf_draw_text_px 灰度路径（ui_kit.c:373-377），BW 判据自此
   // 恒挂。新 oracle 三件：① 灰度域——4bpp 量化后墨(UI_INK_BLACK)/纸(UI_INK_WHITE)/中间灰
   // (1..14)三层俱全，证明走的是 AA 而非二值；② AA 参考对照——与同基线、同字号、同对齐的
   // AA 光栅化逐位一致（基线换算与产品路径同出 center_y+(above-below)/2）；③ 结构——
   // painted>0 且缓存/句柄复用不变（随后 111-112 行原样看守）。这是判据重造，不是补桩。
   // Rebuilt AA oracle (2026-10-16; KNOWN_FAILURES.md entry 2's trigger fired — step 3's
   // two-column heading work already moves this call site's center_x/fit_w geometry). The
   // old criterion "the stub forces c==0 plus bit-identity with ttf_draw_text_px_bw"
   // encodes the BW-era contract; since r92 ui_text_title_vc takes the
   // ttf_set_antialias(true)+ttf_draw_text_px grayscale path (ui_kit.c:373-377), so the BW
   // criterion fails by construction. Three parts: ① grayscale domain — ink(UI_INK_BLACK)/
   // paper(UI_INK_WHITE)/intermediate gray(1..14) all present after 4bpp quantization,
   // proving the AA path rather than a binary one; ② AA reference — bit-identical to the
   // same-baseline, same-size, same-align AA rasterization (the baseline conversion shares
   // the product path's center_y+(above-below)/2); ③ structure — painted>0 with cache and
   // handle reuse unchanged (still guarded right after at 111-112). This is a criterion
   // rebuild, not a stub patch.
   record_coverage=true;
   memset(pixels,15,sizeof(pixels));painted=0;
   ui_text_title_vc(pixels,200,80,px,title,text,EPD_DRAW_ALIGN_CENTER);assert(painted>0);
   uint8_t expected[sizeof(pixels)];memset(expected,15,sizeof(expected));
   bool prior_aa=ttf_get_antialias();ttf_set_antialias(true);
   ttf_draw_text_px(expected,200,80+(above-below)/2,px,title,EPD_DRAW_ALIGN_CENTER,UI_INK_BLACK,UI_INK_WHITE);
   ttf_set_antialias(prior_aa);
   assert(!memcmp(pixels,expected,sizeof(pixels)));
   unsigned ink_px=0,paper_px=0,gray_px=0;
   for(unsigned n=0;n<sizeof(pixels);++n){
    if(pixels[n]==UI_INK_BLACK)++ink_px;
    else if(pixels[n]==UI_INK_WHITE)++paper_px;
    else ++gray_px;
   }
   assert(ink_px&&paper_px&&gray_px);
   // 热身循环同区：AA 路径下灰度值同样过桩，coverage 区外会撞 BW 断言（本段整体在
   // record_coverage=true 区内收口）。/ The warm-up loop shares the zone: grayscale values
   // cross the stub the same way under the AA path, so the whole block closes inside the
   // record_coverage=true region.
   size_t warm=live;
   for(unsigned n=0;n<15;++n)ui_text_title_vc(pixels,200,80,px,title,text,EPD_DRAW_ALIGN_CENTER);
   assert(live==warm&&opens==initial_opens&&active_fd==1&&!strcmp(ttf_font_path(),argv[face+1]));
   record_coverage=false;
  }
  // Static imported faces must respond too, without changing advances or native normal strokes.
  int native_width=ttf_text_width_px(48,text),axis=ttf_get_weight();
  uint8_t native_mask[300*100],weighted[300*100];
  memset(native_mask,0,sizeof(native_mask));
  assert(ttf_text_mask_px(native_mask,300,100,20,70,48,text));
  size_t last_coverage=0;
  for(int level=-2;level<=2;++level){
   ttf_set_stroke_adjust(level);assert(ttf_get_stroke_adjust()==level&&ttf_get_weight()==axis);
   memset(weighted,0,sizeof(weighted));assert(ttf_text_mask_px(weighted,300,100,20,70,48,text));
   size_t coverage=0;for(unsigned n=0;n<sizeof(weighted);++n)coverage+=weighted[n];
   assert(coverage>last_coverage);last_coverage=coverage;
   assert(ttf_text_width_px(48,text)==native_width&&active_fd==1);
   if(!level)assert(!memcmp(native_mask,weighted,sizeof(weighted)));
   size_t warm=live;memset(weighted,0,sizeof(weighted));
   assert(ttf_text_mask_px(weighted,300,100,20,70,48,text)&&live==warm);
  }
  // 真实TTF普通、带字距、两端对齐绘制：关闭AA只改变覆盖率，字号/字宽/字库/缓存不变。
  record_coverage=true;
  for(int level=-2;level<=2;++level){
   ttf_set_stroke_adjust(level);ttf_set_antialias(true);
   memset(pixels,15,sizeof(pixels));ttf_draw_text_px(pixels,20,80,48,text,EPD_DRAW_ALIGN_LEFT,0,15);
   uint8_t smooth[sizeof(pixels)];memcpy(smooth,pixels,sizeof(smooth));size_t warm=live;
   unsigned mid=0;for(unsigned n=0;n<sizeof(pixels);++n)if(pixels[n]>0&&pixels[n]<15)++mid;
   assert(mid);
   ttf_set_antialias(false);assert(!ttf_get_antialias());
   for(int style=0;style<3;++style){
    memset(pixels,15,sizeof(pixels));
    if(!style)ttf_draw_text_px(pixels,20,80,48,text,EPD_DRAW_ALIGN_LEFT,0,15);
    else if(style==1)ttf_draw_text_px_spaced(pixels,20,80,48,text,2,0,15);
    else ttf_draw_text_px_fitted(pixels,20,80,48,text,0,native_width+9,0,15);
    unsigned ink=0;for(unsigned n=0;n<sizeof(pixels);++n){assert(pixels[n]==0||pixels[n]==15);ink+=pixels[n]==0;}
    assert(ink&&ttf_text_width_px(48,text)==native_width&&live==warm&&active_fd==1);
   }
   ttf_set_antialias(true);memset(pixels,15,sizeof(pixels));
   ttf_draw_text_px(pixels,20,80,48,text,EPD_DRAW_ALIGN_LEFT,0,15);assert(!memcmp(smooth,pixels,sizeof(smooth))&&live==warm);
  }
  record_coverage=false;
  ttf_set_stroke_adjust(0);assert(opens==initial_opens);
  widths[face]=ttf_text_width_px(40,text);
  assert(live-resident<128*1024&&peak<1024*1024);
 }
 assert(widths[1]>widths[0]&&max_fd==1);
 ttf_font_unload();assert(!active_fd&&!ttf_font_ready()&&!ttf_font_has_text(text));
 puts("PASS: actual imported system/reader TTF at 32/40/48px, AA oracle on the title entry (grayscale domain plus same-argument raster identity), five stroke levels with native normal identity and fixed advances, single file/face, bounded warm cache and no duplicate font load");
}
'''
    (work / 'test.c').write_text(unit)
    # ttf_font.c 的有界 snprintf 会被 gcc 判为可能截断；与 book_ui/lock_collage/ui_pinpad
    # 等测试一样只降这一级（项目惯例，不动产品码）。
    # gcc flags the bounded snprintf in ttf_font.c as possibly truncating; downgrade just
    # that one level, the same convention book_ui/lock_collage/ui_pinpad already follow.
    flags = ['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
             '-Wno-unused-variable', '-Wno-error=format-truncation', '-fsanitize=address,undefined', '-ffunction-sections', '-fdata-sections',
             '-I' + str(work), '-I' + str(ROOT / 'main/font'), '-I' + str(ROOT / 'main/ui'),
             '-I' + str(ROOT / 'main'), '-I' + str(ROOT / 'tools/ui_gesture_stubs')]
    subprocess.run(flags + ['-Dopen=title_test_open', '-Dclose=title_test_close', '-c',
                            str(ROOT / 'main/font/ttf_font.c'), '-o', str(work / 'ttf.o')], check=True)
    gc = '-Wl,-dead_strip' if os.uname().sysname == 'Darwin' else '-Wl,--gc-sections'
    subprocess.run(flags + [str(work / 'test.c'), str(work / 'assets.c'), str(work / 'ttf.o'),
                            str(ROOT / 'main/ui/ui_kit.c'), '-lm', gc, '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test'), str(work / 'system.ttf'), str(work / 'reader.ttf')], check=True)
