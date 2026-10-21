"""中文：真实亚克力缓存逐像素及内存故障回归。
English: Actual acrylic cache pixel equivalence and allocation-fault regressions.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import re,subprocess,tempfile
r=Path(__file__).resolve().parents[1]
def function(file,name):
 t=(r/file).read_text();scan=re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"',lambda m:' '*len(m[0]),t)
 m=re.search(r'^(?:static )?(?:void|int|bool|uint8_t|EpdRect) '+name+r'\([^;{}]*?\)\s*\{',scan,re.M);assert m,name
 i=m.end();depth=1
 while depth:depth+=(scan[i]=='{')-(scan[i]=='}');i+=1
 return t[m.start():i]+'\n'
quick=(r/'tools/quick_layer_raster_host_test.py').read_text()
a=quick.index("code=r'"+"''")+len("code=r'"+"''");b=quick.index("'"+"''",a)
code=quick[a:b].split('static uint8_t ui_contrast_gray')[0]
code=code.replace('assert(x==xx);for(;y<=yy;++y)epd_draw_pixel(x,y,gray,fb);','if(x==xx){for(;y<=yy;++y)epd_draw_pixel(x,y,gray,fb);}else{assert(y==yy);for(;x<=xx;++x)epd_draw_pixel(x,y,gray,fb);}')
code+=r"""
static void ui_fill_rect_fast(EpdRect r,uint8_t gray,uint8_t *fb){epd_fill_rect(r,gray,fb);}
static int contrast=100,guard_draws;
static size_t free_psram=5u*1024u*1024u;
static int s_view;
enum {SHELF,MANAGE};
static bool app_settings_main_fast_refresh(void){return fast;}
static int app_settings_system_contrast(void){return contrast;}
static size_t heap_caps_get_free_size(unsigned caps){assert(caps==3);return free_psram;}
static void *heap_caps_malloc(size_t bytes,unsigned caps){assert(caps==3);++allocs;if(oom)return NULL;return malloc(bytes);}
"""
for n in ('ui_contrast_gray','ui_read_pixel','ui_acrylic_bw_rect','ui_hairline','draw_frosted'):
 code+=function('main/ui/ui_kit.c',n)
code+='static void ui_draw_acrylic_guard(uint8_t *fb,EpdRect r){++guard_draws;draw_frosted(fb,r,false); }\n'
code+=function('main/ui/ui_kit.c','ui_rotate_rect_to_fb')
t=(r/'main/apps/app_book.c').read_text();a=t.index('#define SHELF_GUARD_CACHE_BYTES');b=t.index('static EpdRect row_rect',a);code+=t[a:b]
code+=r"""
static void seed(unsigned value){for(unsigned i=0;i<BYTES;++i)frame[i]=(uint8_t)(i*13+value*19);}
static void reference(EpdRect rect){ui_draw_acrylic_guard(saved,rect);if(fast)ui_acrylic_bw_rect(saved,rect);}
int main(void){
 for(rotation=0;rotation<4;++rotation)for(int mode=0;mode<2;++mode){
  fast=mode;release_guard_cache();
  for(int row=0;row<3;++row){
   EpdRect rect={20,328+row*104,644,90};
   seed(row);memcpy(again,frame,BYTES);memcpy(saved,frame,BYTES);reference(rect);
   draw_shelf_guard(frame,rect,row);assert(!memcmp(frame,saved,BYTES));
   memcpy(frame,again,BYTES);int before=guard_draws;draw_shelf_guard(frame,rect,row);
   assert(!memcmp(frame,saved,BYTES));assert(guard_draws==before);
   seed(row+1);memcpy(saved,frame,BYTES);reference(rect);before=guard_draws;draw_shelf_guard(frame,rect,row);
   assert(guard_draws==before+1&&!memcmp(frame,saved,BYTES));
   ++contrast;seed(row+1);memcpy(saved,frame,BYTES);reference(rect);before=guard_draws;draw_shelf_guard(frame,rect,row);
   assert(guard_draws==before+1&&!memcmp(frame,saved,BYTES));
  }
 }
 release_guard_cache();rotation=3;fast=true;
 for(int failure=0;failure<2;++failure){
  release_guard_cache();oom=failure==0;free_psram=failure?100000:5u*1024u*1024u;
  EpdRect rect={20,328,644,90};seed(42);memcpy(saved,frame,BYTES);reference(rect);draw_shelf_guard(frame,rect,0);
  assert(!s_guard_cache&&!memcmp(frame,saved,BYTES));
 }
 release_guard_cache();puts("PASS: actual acrylic output matches uncached pixels in four rotations, gray/BW, source/contrast invalidation, bounded memory and OOM fallback");
}
"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'cache.c';p.write_text(code);out=Path(d)/'cache'
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-O2','-fsanitize=address,undefined','-Imain',str(p),'main/ui/ui_image_dither.c','-o',str(out)],cwd=r,check=True)
 subprocess.run([str(out)],check=True)
