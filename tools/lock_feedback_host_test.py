#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 中文：验证实际清白、圆锁顺序和后缓冲历史；四旋转边界、失败及忙时不误提交。
# English: Actual white/badge ordering and real history: rotation bounds, failures and busy guards.
from pathlib import Path
import subprocess
import tempfile
import sys
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* 本测试的替身按旋转/小窗动态换算尺寸（断言依赖），关掉 stub 的静态版本。
   This test's shims compute sizes from rotation/small-window state (assertions
   depend on it); turn off the stub's static pair. */
#define EPDIY_STUB_NO_ROTATED_SIZE
#include "ui_lock_feedback.h"
typedef struct {int id;} EpdiyHighlevelState;
#define MODE_DU 1
#define READ_PICO_TRANSFER_UPLOADING 3
typedef struct {int state;} read_pico_transfer_status_t;
static uint8_t frame[684u*1216u/2u], original[sizeof(frame)], back[sizeof(frame)];
static unsigned style=2, pushes, white_pushes, cancels, touches;
static int rotation, transfer, result, white_result;
static bool online, missing, small, s_lock_feedback_visible;
#define EPD_DRAW_SUCCESS 0
static const int E0470_FOLLOW_WAVEFORM=1;
int epd_rotated_display_width(void){return small?70:rotation&1?684:1216;}
int epd_rotated_display_height(void){return small?70:rotation&1?1216:684;}
static size_t offset(int x,int y){
 int px=x,py=y;
 if(rotation==1){px=1215-y;py=x;}
 else if(rotation==2){px=1215-x;py=683-y;}
 else if(rotation==3){px=y;py=683-x;}
 return (size_t)py*1216+px;
}
void epd_draw_pixel(int x,int y,uint8_t c,uint8_t *fb){
 assert(x>=0&&x<epd_rotated_display_width()&&y>=0&&y<epd_rotated_display_height());
 size_t at=offset(x,y);unsigned shift=(at&1)*4;
 fb[at/2]=(fb[at/2]&~(15u<<shift))|((c>>4)<<shift);++touches;
}
static unsigned pixel(const uint8_t *fb,int x,int y){size_t at=offset(x,y);return(fb[at/2]>>((at&1)*4))&15;}
static unsigned app_settings_lock_style(void){return style;}
static bool pico_online_busy(void){return online;}
static void read_pico_transfer_get_status(read_pico_transfer_status_t *s){s->state=transfer;}
static uint8_t* epd_hl_get_framebuffer(EpdiyHighlevelState *h){assert(h);return missing?NULL:frame;}
static void display_main_transition_cancel(void){++cancels;}
static void epd_hl_set_all_white(EpdiyHighlevelState *h){assert(h);memset(frame,255,sizeof(frame));}
static int update_display_full(EpdiyHighlevelState *h){
 assert(h&&cancels==1&&!pushes&&!white_pushes&&!memcmp(back,original,sizeof(frame)));
 for(size_t at=0;at<sizeof(frame);++at)assert(frame[at]==255);
 ++white_pushes;if(!white_result)memcpy(back,frame,sizeof(frame));return white_result;
}
static int update_display_area_diff_with(EpdiyHighlevelState *h,const int *w,int mode,EpdRect area){
 assert(h&&w==&E0470_FOLLOW_WAVEFORM&&mode==MODE_DU&&white_pushes==1&&!white_result&&!pushes);
 assert(area.width==97&&area.height==97&&area.x==(epd_rotated_display_width()-97)/2&&area.y==(epd_rotated_display_height()-97)/2);
 for(size_t at=0;at<sizeof(frame);++at)assert(back[at]==255);
 ++pushes;if(!result)memcpy(back,frame,sizeof(frame));return result;
}
'''
# r93 适配：产品源码并入本 TU（ui_pinpad 同款模式）。上游 stub 头只声明旋转尺寸、
# 由链接方提供替身；我方 stub 头改为内联默认定义后，产品若单独编译就绑到内联默认值
# （684x1216 固定），与测试 TU 的旋转感知替身割裂——rot=0/2 下居中几何断言错位
# （产品按 684 宽居中、测试按 1216 宽期望）。并入后产品与测试共用同一对替身，
# 四个旋转的几何/像素断言同模型。须排在 app_lock_feedback 抽取之前（其函数体调用
# ui_lock_feedback_draw）。
# r93 adaptation: pull the product source into this TU (the ui_pinpad pattern).
# Upstream's stub header only declares the rotated sizes for linkers to provide;
# after our header switched to inline defaults, a separately compiled product binds
# the fixed 684x1216 inline and splits from the test TU's rotation-aware shims --
# the centering geometry assertions mis land at rot 0/2 (product centers on 684 wide,
# the test expects 1216). Included, product and test share one shim pair and all four
# rotations assert against one model. Must precede the app_lock_feedback extraction
# (its body calls ui_lock_feedback_draw).
unit += '#include "ui_lock_feedback.c"\n'
unit += function('app_lock_feedback', ROOT/'main/sleep.c')
unit += r'''
static void reset(void){memset(frame,0x57,sizeof(frame));memcpy(original,frame,sizeof(frame));memcpy(back,original,sizeof(frame));pushes=white_pushes=cancels=touches=0;style=2;online=missing=small=s_lock_feedback_visible=false;transfer=result=white_result=0;}
int main(int argc,char **argv){
 EpdiyHighlevelState hl={0};
 for(rotation=0;rotation<4;++rotation){
  reset();app_lock_feedback(&hl);assert(pushes==1&&white_pushes==1&&cancels==1&&touches>0&&s_lock_feedback_visible&&!memcmp(frame,back,sizeof(frame)));
  int cx=(epd_rotated_display_width()-97)/2+48,cy=(epd_rotated_display_height()-97)/2+48;
  for(int y=0;y<epd_rotated_display_height();++y)for(int x=0;x<epd_rotated_display_width();++x){
   int dx=x-cx,dy=y-cy;
   if(dx*dx+dy*dy>48*48)assert(pixel(frame,x,y)==15);
   else assert(pixel(frame,x,y)==0||pixel(frame,x,y)==15);
  }
  assert(pixel(frame,cx,cy-21)==0&&pixel(frame,cx,cy+9)==0&&pixel(frame,cx+30,cy)==15&&pixel(frame,cx+47,cy)==0);

  if(rotation==1&&argc>1){FILE *f=fopen(argv[1],"wb");assert(f);for(int y=0;y<1216;++y)for(int x=0;x<684;++x)assert(fputc((int)pixel(frame,x,y)*17,f)!=EOF);assert(!fclose(f));}
 }
 for(unsigned s=0;s<3;++s){reset();style=s;app_lock_feedback(&hl);assert(pushes==(s==2)&&white_pushes==(s==2)&&cancels==(s==2));}
 reset();online=true;app_lock_feedback(&hl);assert(!pushes&&!white_pushes&&!touches&&!memcmp(frame,original,sizeof(frame)));
 reset();transfer=READ_PICO_TRANSFER_UPLOADING;app_lock_feedback(&hl);assert(!pushes&&!white_pushes&&!touches&&!memcmp(frame,original,sizeof(frame)));
 reset();missing=true;app_lock_feedback(&hl);assert(!pushes&&!white_pushes&&!touches);
 reset();app_lock_feedback(NULL);assert(!pushes&&!white_pushes&&!touches);
 reset();white_result=1;app_lock_feedback(&hl);assert(white_pushes==1&&!pushes&&!touches&&!s_lock_feedback_visible&&!memcmp(back,original,sizeof(frame)));
 reset();result=1;app_lock_feedback(&hl);assert(white_pushes==1&&pushes==1&&cancels==1&&!s_lock_feedback_visible);for(size_t at=0;at<sizeof(frame);++at)assert(back[at]==255);
 reset();small=true;app_lock_feedback(&hl);assert(!pushes&&!white_pushes&&!touches&&!cancels);
 reset();online=true;s_lock_feedback_visible=true;app_lock_feedback(&hl);assert(!s_lock_feedback_visible&&!white_pushes);
 puts("PASS: one calibrated white page precedes a local binary round lock in four rotations; real back history; white/icon faults never commit readiness; other-style/OTA/upload/missing-frame/undersize guards");
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='kiikoread-lock-feedback-', dir='/private/tmp') as folder:
    work = Path(folder)
    (work/'test.c').write_text(unit)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',
                    '-I'+str(ROOT/'tools/ui_gesture_stubs'),'-I'+str(ROOT/'main/ui'),
                    str(work/'test.c'),'-o',str(work/'test')],check=True)
    subprocess.run([str(work/'test')] + sys.argv[1:],check=True)
