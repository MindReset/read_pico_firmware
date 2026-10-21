#!/usr/bin/env python3
"""Compare production packed fills with the original per-pixel implementation."""
from pathlib import Path
import subprocess, tempfile, re
root=Path(__file__).resolve().parents[1]
def function(text,name):
    match=re.search(r'(?m)^(?:static )?(?:void|EpdRect|Coord_xy) '+re.escape(name)+r'\([\s\S]*?\)\s*\{',text)
    assert match,name
    start=match.start();opening=match.end()-1;depth=1;i=opening+1
    while depth:
        if text[i]=='{':depth+=1
        elif text[i]=='}':depth-=1
        i+=1
    return text[start:i]
ui=(root/'main/ui/ui_kit.c').read_text()
original=(root/'components/epdiy/src/epdiy.c').read_text()
prefix='''// 本测试自带按旋转换算的尺寸替身，必须在任何头被拉进来之前关掉 stub 的 static inline 版，
// 否则后到的 include 会先定义它、与下面的实现重定义。
// This test ships its own rotation-aware size shim, so the static inline one has to be
// switched off before any header is pulled in, or a later include defines it first.
#define EPDIY_STUB_NO_ROTATED_SIZE
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {uint16_t x,y;} Coord_xy;
enum {EPD_ROT_LANDSCAPE,EPD_ROT_PORTRAIT,EPD_ROT_INVERTED_LANDSCAPE,EPD_ROT_INVERTED_PORTRAIT};
static int display_rotation;
static int epd_width(void){return 64;}
static int epd_height(void){return 48;}
static int epd_get_rotation(void){return display_rotation;}
static int epd_rotated_display_width(void){return display_rotation&1?48:64;}
static int epd_rotated_display_height(void){return display_rotation&1?64:48;}
#define _swap_int(a,b) do {int t=(a);(a)=(b);(b)=t;} while(0)
'''
code=prefix+'\n'.join(function(original,n) for n in ('_rotate','epd_draw_pixel','epd_draw_hline','epd_fill_rect'))
code+='\n'+'\n'.join(function(ui,n) for n in ('ui_rotate_rect_to_fb','ui_fill_rect_fast','ui_blit_mono_bitmap'))
code+='''
static uint32_t seed=192;static unsigned rng(void){seed=seed*1664525u+1013904223u;return seed;}
int main(void){unsigned cases=0;uint8_t a[64*48/2+64],b[sizeof(a)];
 for(display_rotation=0;display_rotation<4;++display_rotation)for(int gray=0;gray<256;++gray)for(int i=0;i<32;++i){
  for(unsigned j=0;j<sizeof(a);++j)a[j]=b[j]=(uint8_t)rng();
  EpdRect r={(int)(rng()%100)-20,(int)(rng()%100)-20,(int)(rng()%100)-4,(int)(rng()%100)-4};
  epd_fill_rect(r,gray,a+32);ui_fill_rect_fast(r,gray,b+32);
  if(memcmp(a,b,sizeof(a))){fprintf(stderr,"rotation=%d gray=%d rect=%d,%d,%d,%d\\n",display_rotation,gray,r.x,r.y,r.width,r.height);return 1;}
  ++cases;
 }
 uint8_t bits[100*13];for(unsigned j=0;j<sizeof(bits);++j)bits[j]=(uint8_t)rng();
 for(display_rotation=0;display_rotation<4;++display_rotation)for(int trial=0;trial<512;++trial){
  for(unsigned j=0;j<sizeof(a);++j)a[j]=b[j]=(uint8_t)rng();
  EpdRect r={(int)(rng()%100)-20,(int)(rng()%100)-20,(int)(rng()%98)+1,(int)(rng()%98)+1};
  for(int y=0;y<r.height;++y)for(int x=0;x<r.width;++x)epd_draw_pixel(r.x+x,r.y+y,bits[y*13+x/8]&(1u<<(x&7))?255:0,a+32);
  ui_blit_mono_bitmap(b+32,r,bits,13);assert(!memcmp(a,b,sizeof(a)));
 }
 display_rotation=0;memset(a,37,sizeof(a));memcpy(b,a,sizeof(a));ui_fill_rect_fast((EpdRect){0,0,2147483647,2147483647},0x80,b+32);
 for(unsigned i=32;i<32+64*48/2;++i)assert(b[i]==0x88);
 assert(!memcmp(a,b,32)&&!memcmp(a+sizeof(a)-32,b+sizeof(b)-32,32));
 printf("PASS: %u rectangles, 256 grays, all rotations, clipping and exact boundary nibbles; 2048 exact mono blits; no framebuffer overrun\\n",cases);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src=Path(tmp)/'fill.c';binary=Path(tmp)/'fill';src.write_text(code)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-O2','-fsanitize=address,undefined',str(src),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
