"""Actual one-pixel stroke morph and minus/plus hit geometry.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
r=Path(__file__).resolve().parents[1]
unit=r"""
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "ttf_stroke_filter.h"
typedef struct{int x,y,width,height;}EpdRect;
static bool ui_rect_hit(EpdRect a,int x,int y){return x>=a.x&&y>=a.y&&x<a.x+a.width&&y<a.y+a.height;}
"""+function('reader_weight_rect',r/'main/apps/app_book.c')+'\n'+function('reader_weight_step',r/'main/apps/app_book.c')+'\n'+function('reader_weight_value',r/'main/apps/app_book.c')+r"""
int main(void){
 uint8_t src[121]={0},out[225],small[225];src[5*11+5]=255;
 for(int px=36;px<=72;px+=4)for(int level=0;level<=2;++level){
  int pad=level,w=11+2*pad;memset(out,0xa5,sizeof(out));ttf_stroke_filter(out,src,11,11,px,level);
  int ink=0;for(int y=0;y<w;++y)for(int x=0;x<w;++x){bool expected=x>=5&&x<=5+2*pad&&y>=5&&y<=5+2*pad;assert(out[y*w+x]==(expected?255:0));ink+=out[y*w+x]>0;}
  assert(ink==(2*level+1)*(2*level+1));for(int i=w*w;i<225;++i)assert(out[i]==0xa5);
  ttf_stroke_filter(small,src,11,11,144,level);assert(!memcmp(out,small,w*w));
 }
 memset(src,0,sizeof(src));for(int y=2;y<=8;++y)for(int x=2;x<=8;++x)src[y*11+x]=255;
 for(int level=-2;level<0;++level){int pad=-level,w=11+2*pad;ttf_stroke_filter(out,src,11,11,48,level);int ink=0;for(int i=0;i<w*w;++i)ink+=out[i]>0;assert(ink==(7-2*pad)*(7-2*pad));}
 // 覆盖率逐小档变化，整数档位保持旧结果。/ Coverage changes by substeps; integral levels retain prior results.
 memset(src,0,sizeof(src));src[5*11+5]=255;
 int last=0;
 for(int step=1;step<=16;++step){ttf_stroke_filter_fine(out,src,11,11,48,step);int edge=out[5*13+5];assert(edge>last&&edge-last<=16);last=edge;}
 assert(last==255);ttf_stroke_filter(out,src,11,11,48,1);ttf_stroke_filter_fine(small,src,11,11,48,16);assert(!memcmp(out,small,169));
 memset(src,255,sizeof(src));ttf_stroke_filter_fine(out,src,11,11,48,-1);assert(out[1*13+1]==239&&out[6*13+6]==255);
 char value[20];reader_weight_value(value,sizeof(value),1);assert(!strcmp(value,"+0.0625"));reader_weight_value(value,sizeof(value),-3);assert(!strcmp(value,"-0.1875"));reader_weight_value(value,sizeof(value),32);assert(!strcmp(value,"+2"));
 assert(reader_weight_step(99,952)==-1&&reader_weight_step(585,952)==1);
 assert(!reader_weight_step(342,952)&&!reader_weight_step(99,900)&&!reader_weight_step(585,1000));
 puts("PASS: 1/16-pixel coverage increments, exact legacy integral erosion/dilation, bounded glyph buffers, numeric labels and capsule hits");
}
"""
with tempfile.TemporaryDirectory() as folder:
 p=Path(folder)/'test.c';p.write_text(unit);exe=p.with_suffix('')
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(r/'main/font'),str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
