"""实际快捷层/点击缓存/汉字缩放的像素与内存检查。
Actual quick-layer, pressed-tile and Han scaling pixel/memory checks.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import re, subprocess, tempfile
root = Path(__file__).resolve().parents[1]

def body(path):
    return re.sub(r'^#include[^\n]*\n', '', (root/path).read_text(), flags=re.M)

def function(path, name):
    text=(root/path).read_text()
    start=re.search(r'^(?:static )?[^\n]+\b'+name+r'\([^;{}]*\)\s*\{',text,re.M)
    assert start, name
    at=start.end(); depth=1
    while depth:
        depth += (text[at]=='{')-(text[at]=='}'); at+=1
    return text[start.start():at]+'\n'

code=r'''
// 本测试自带按旋转换算的尺寸替身，必须在任何头被拉进来之前关掉 stub 的 static inline 版，
// 否则后到的 include 会先定义它、与下面的实现重定义。
// This test ships its own rotation-aware size shim, so the static inline one has to be
// switched off before any header is pulled in, or a later include defines it first.
#define EPDIY_STUB_NO_ROTATED_SIZE
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "ui/ui_nav_layout.h"
#include "ui/ui_image_dither.h"
#include "assets/ui_icons.h"
#include "font/ui_hanzi_scale.h"
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
#define UI_TOUCH_SLOP_PX 24
#define UI_GRAY_WHITE 255
#define UI_GRAY_BLACK 0
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
typedef struct {int x,y,width,height;} EpdRect;
typedef enum {UI_CLICK_NAV,UI_CLICK_BACK,UI_CLICK_BACK_SMALL} ui_click_kind_t;
enum {EPD_ROT_LANDSCAPE,EPD_ROT_PORTRAIT,EPD_ROT_INVERTED_LANDSCAPE,EPD_ROT_INVERTED_PORTRAIT};
static int rotation;
#define W 1216
#define H 684
#define BYTES (W*H/2)
static uint8_t frame[BYTES],saved[BYTES],again[BYTES];
static int live,allocs,max_bytes;
static bool oom,fast;
static int epd_width(void){return W;}
static int epd_height(void){return H;}
static int epd_get_rotation(void){return rotation;}
static int epd_rotated_display_width(void){return rotation&1?H:W;}
static int epd_rotated_display_height(void){return rotation&1?W:H;}
static uint8_t epd_get_pixel(int x,int y,int w,int h,const uint8_t *fb){assert(w==W&&h==H&&x>=0&&x<w&&y>=0&&y<h);unsigned value=fb[(size_t)y*w/2+x/2];return (uint8_t)((x&1?value>>4:value&15)<<4);}
static void epd_draw_pixel(int x,int y,uint8_t gray,uint8_t *fb){
 if(x<0||y<0||x>=epd_rotated_display_width()||y>=epd_rotated_display_height())return;
 int px=x,py=y;
 switch(rotation){case 1:px=W-y-1;py=x;break;case 2:px=W-x-1;py=H-y-1;break;case 3:px=y;py=H-x-1;break;}
 size_t i=(size_t)py*W/2+px/2;unsigned shift=(px&1)*4;
 fb[i]=(uint8_t)((fb[i]&~(15u<<shift))|((gray>>4)<<shift));
}
static void epd_fill_rect(EpdRect r,uint8_t gray,uint8_t *fb){for(int y=0;y<r.height;++y)for(int x=0;x<r.width;++x)epd_draw_pixel(r.x+x,r.y+y,gray,fb);}
static void epd_draw_line(int x,int y,int xx,int yy,uint8_t gray,uint8_t *fb){assert(x==xx);for(;y<=yy;++y)epd_draw_pixel(x,y,gray,fb);}
static void epd_draw_circle(int cx,int cy,int r,uint8_t gray,uint8_t *fb){for(int y=-r;y<=r;++y)for(int x=-r;x<=r;++x)if(x*x+y*y<=r*r&&x*x+y*y>(r-1)*(r-1))epd_draw_pixel(cx+x,cy+y,gray,fb);}
static void epd_fill_circle(int cx,int cy,int r,uint8_t gray,uint8_t *fb){for(int y=-r;y<=r;++y)for(int x=-r;x<=r;++x)if(x*x+y*y<=r*r)epd_draw_pixel(cx+x,cy+y,gray,fb);}
static void ui_fill_round_rect(uint8_t *fb,EpdRect r,int radius,uint8_t gray){(void)radius;epd_fill_rect(r,gray,fb);}
static void ui_draw_round_rect(uint8_t *fb,EpdRect r,int radius,uint8_t gray){(void)radius;for(int x=0;x<r.width;++x){epd_draw_pixel(r.x+x,r.y,gray,fb);epd_draw_pixel(r.x+x,r.y+r.height-1,gray,fb);}for(int y=0;y<r.height;++y){epd_draw_pixel(r.x,r.y+y,gray,fb);epd_draw_pixel(r.x+r.width-1,r.y+y,gray,fb);}}
static uint8_t ui_contrast_gray(uint8_t v){return v;}
static void ui_nav_status(uint8_t *fb){epd_fill_rect((EpdRect){36,29,52,16},0,fb);}
static bool app_settings_main_fast_refresh(void){return fast;}
static void *heap_caps_malloc(size_t n,unsigned caps){assert(caps==3&&n<=4096);++allocs;if((int)n>max_bytes)max_bytes=(int)n;if(oom)return NULL;++live;return malloc(n);}
static void heap_caps_free(void *p){if(p){--live;free(p);}assert(live>=0);}
'''
code+='static void ui_fill_rect_fast(EpdRect r,uint8_t gray,uint8_t *fb){epd_fill_rect(r,gray,fb);}\n'
header=(root/'main/ui/ui_quick_menu.h').read_text()
code+='\n'.join(re.findall(r'^#define UI_QUICK[^\n]*',header,re.M))+'\n'
for name in ('ui_read_pixel','ui_image_bw_rect','icon_nibble','ui_draw_icon'):
    code+=function('main/ui/ui_kit.c',name)
code+=body('main/ui/ui_quick_menu.c')+body('main/ui/ui_click_feedback.c')
code+=r'''
static void patterned(void){for(size_t i=0;i<BYTES;++i)frame[i]=(uint8_t)(i*13u+7u);memcpy(saved,frame,BYTES);}
int main(void){
 for(rotation=0;rotation<4;++rotation){
  for(int policy=0;policy<3;++policy){
   patterned();int before=allocs;ui_quick_menu_draw(frame,true,false);
   assert(allocs==before&&!live);int grays=0;
   for(int y=0;y<epd_rotated_display_height();++y)for(int x=0;x<epd_rotated_display_width();++x){
    bool mask=y<UI_QUICK_HEIGHT&&layer_pixel(x,y,epd_rotated_display_width());
    uint8_t p=pixel(frame,x,y);
    if(!mask)assert(p==pixel(saved,x,y));
    else grays+=p!=0&&p!=255;
   }
   assert(grays>1000);
   memcpy(again,frame,BYTES);memcpy(frame,saved,BYTES);ui_quick_menu_draw(frame,true,false);assert(!memcmp(frame,again,BYTES));
  }
  for(fast=false;;fast=true){
   patterned();ui_click_feedback_begin(frame);
   ui_click_feedback_register(again,(EpdRect){26,82,64,64},UI_CLICK_BACK,UI_ICON_CHEVRON_LEFT);
   EpdRect r;assert(!ui_click_feedback_press(frame,58,114,&r));
   ui_click_feedback_register(frame,(EpdRect){26,82,64,64},UI_CLICK_BACK,UI_ICON_CHEVRON_LEFT);
   assert(ui_click_feedback_press(frame,58,114,&r)&&live==1&&max_bytes==4096);
   assert(memcmp(frame,saved,BYTES));assert(!ui_click_feedback_press(frame,58,114,&r));
   for(int y=0;y<epd_rotated_display_height();++y)for(int x=0;x<epd_rotated_display_width();++x)
    if(x<26||x>=90||y<82||y>=146)assert(pixel(frame,x,y)==pixel(saved,x,y));
   assert(!ui_click_feedback_cancel_at(58,114)&&ui_click_feedback_cancel_at(100,114));
   assert(ui_click_feedback_release(frame,&r)&&!live&&!memcmp(frame,saved,BYTES));
   assert(!ui_click_feedback_release(frame,&r));
   oom=true;assert(!ui_click_feedback_press(frame,58,114,&r)&&!live&&!memcmp(frame,saved,BYTES));oom=false;
   ui_click_feedback_reset();assert(!ui_click_feedback_press(frame,58,114,&r));
   patterned();ui_click_feedback_begin(frame);
   ui_click_feedback_register(frame,(EpdRect){36,84,44,44},UI_CLICK_BACK_SMALL,UI_ICON_CHEVRON_LEFT);
   assert(ui_click_feedback_press(frame,58,106,&r));
   assert(r.x>=36&&r.y>=84&&r.x+r.width<=80&&r.y+r.height<=128);
   for(int yy=0;yy<epd_rotated_display_height();++yy)for(int xx=0;xx<epd_rotated_display_width();++xx)
    if(xx<36||xx>=80||yy<84||yy>=128)assert(pixel(frame,xx,yy)==pixel(saved,xx,yy));
   assert(ui_click_feedback_release(frame,&r)&&!live&&!memcmp(frame,saved,BYTES));
   if(fast)break;
  }
 }
 rotation=3;patterned();ui_click_feedback_begin(frame);
 ui_click_feedback_register(frame,(EpdRect){55,1111,60,60},UI_CLICK_NAV,UI_ICON_HOUSE);
 EpdRect r;assert(ui_click_feedback_press(frame,85,1188,&r));
 assert(!ui_click_feedback_cancel_at(90,1188)&&ui_click_feedback_cancel_at(258,1188));
 assert(ui_click_feedback_release(frame,&r)&&!memcmp(frame,saved,BYTES));
 assert(ui_click_feedback_press(frame,85,1188,&r));ui_click_feedback_begin(frame);assert(!live&&!ui_click_feedback_release(frame,&r));
 // 三种主页模式的底栏共用灰底：按压不能改外沿、文字、横条或相邻图标。
 // All three main modes share a gray footer: presses must preserve its perimeter, labels, marker and neighboring icons.
 static const ui_icon_t tabs[]={UI_ICON_HOUSE,UI_ICON_LIBRARY_BIG,UI_ICON_FOLDER_OPEN,UI_ICON_SLIDERS_HORIZONTAL};
 for(int tab=0;tab<4;++tab)for(int policy=0;policy<3;++policy)for(int edge=0;edge<2;++edge){
  fast=policy==1;memset(frame,0xee,BYTES);
  int cx=(tab*2+1)*684/8;
  epd_fill_rect((EpdRect){0,1097,684,5},0,frame);
  epd_fill_rect((EpdRect){0,1170,684,2},0x70,frame);
  ui_draw_icon(frame,cx,1141,44,tabs[tab],0);
  memcpy(saved,frame,BYTES);ui_click_feedback_begin(frame);
  ui_click_feedback_register(frame,(EpdRect){cx-30,1111,60,60},UI_CLICK_NAV,tabs[tab]);
  assert(!ui_click_feedback_press(frame,cx,UI_NAV_TOUCH_TOP-1,&r));
  assert(!ui_click_feedback_press(frame,cx,1216,&r));
  assert(!ui_click_feedback_press(frame,-1,UI_NAV_TOUCH_TOP,&r));
  assert(!ui_click_feedback_press(frame,684,UI_NAV_TOUCH_TOP,&r));
  assert(ui_click_feedback_press(frame,cx,edge?UI_NAV_TOUCH_TOP:1188,&r));
  assert(!ui_click_feedback_cancel_at(cx,edge?UI_NAV_TOUCH_TOP+12:1188));
  for(int y=0;y<1216;++y)for(int x=0;x<684;++x){
   if(x<cx-25||x>=cx+25||y<1118||y>=1168)assert(pixel(frame,x,y)==pixel(saved,x,y));
  }
  if(policy==0)memcpy(again,frame,BYTES);else assert(!memcmp(frame,again,BYTES));
  assert(r.y>=UI_NAV_MARKER_SCAN_END&&r.y+r.height<=1170);
  EpdRect pressed=r;
  assert(ui_click_feedback_release(frame,&r)&&!memcmp(&r,&pressed,sizeof(r))&&!live&&!memcmp(frame,saved,BYTES));
 }
 // 原尺寸保持精确位图；缩放产生中间覆盖率且不改变空白与满墨端点。
 // Native sizes remain exact; scaling produces intermediate coverage with preserved blank/full endpoints.
 uint8_t han[UI_HANZI_RECORD]={0};han[2]=han[3]=24;int intermediate=0;
 for(int y=0;y<24;++y)for(int x=0;x<24;++x)if(x==y){unsigned b=(unsigned)y*24+x;han[5+b/8]|=1u<<(b&7);}
 for(int y=0;y<24;++y)for(int x=0;x<24;++x)assert(ui_hanzi_coverage(han,24,24,x,y)==(x==y?255:0));
 for(int y=0;y<39;++y)for(int x=0;x<39;++x){unsigned v=ui_hanzi_coverage(han,39,39,x,y);intermediate+=v>0&&v<255;}
 assert(intermediate>100);
 memset(han+5,0,sizeof(han)-5);for(int y=0;y<72;++y)for(int x=0;x<72;++x)assert(!ui_hanzi_coverage(han,72,72,x,y));
 memset(han+5,255,sizeof(han)-5);assert(ui_hanzi_coverage(han,39,39,19,19)==255);
 puts("PASS: actual quick layer uses no allocations, grayscale in all three modes and deterministic masks across four rotations; 12 navigation presses preserve gray backgrounds/labels/marker/neighbors in all modes; click cache <=4096 bytes restores exact pixels, ignores prefetch and tolerates OOM; Han scaling retains native bits and smooths enlarged edges");
 return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src=Path(tmp)/'test.c';src.write_text(code)
    exe=Path(tmp)/'test'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',
                    '-I'+str(root/'main'),str(src),str(root/'main/ui/ui_image_dither.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
