/* SPDX-License-Identifier: Apache-2.0
 * 中文：独立密码键盘；导入系统字形不拉伸，16灰背景和清晰圆圈分开绘制。
 * English: Dedicated PIN pad; unscaled system glyphs, separate 16-gray frost and crisp circles.
 * 冻结：不切字体、不写凭据、不调用显示；缓存和输入在退出时释放/清零。
 * Frozen: No face switches, credentials or presentation; release cache and wipe input on exit.
 */
#include "ui_pinpad.h"
#include "ui_kit.h"
#include "ui_image_dither.h"
#include "ttf_font.h"
#include "ui_font.h"
#include "lock_pin.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define PAD_W 684
#define PAD_H 1216
#define LOW_W 76
#define LOW_H 136
#define RADIUS 72
static const int xs[3]={158,342,526};
static const int ys[4]={544,703,862,1021};
EpdRect ui_pinpad_full(void){return (EpdRect){0,0,PAD_W,PAD_H};}
EpdRect ui_pinpad_entry_area(void){return (EpdRect){196,371,292,91};}

static uint8_t pixel(const uint8_t *frame,int x,int y){
    int px=x,py=y;
    switch(epd_get_rotation()){
        case EPD_ROT_PORTRAIT:px=epd_width()-y-1;py=x;break;
        case EPD_ROT_INVERTED_LANDSCAPE:px=epd_width()-x-1;py=epd_height()-y-1;break;
        case EPD_ROT_INVERTED_PORTRAIT:px=y;py=epd_height()-x-1;break;
        default:break;
    }
    return epd_get_pixel(px,py,epd_width(),epd_height(),frame);
}
void ui_pinpad_end(ui_pinpad_t *pad){
    if(!pad)return;
    free(pad->background);lock_pin_wipe(pad,sizeof(*pad));pad->pressed=-1;
}
void ui_pinpad_reset(ui_pinpad_t *pad,const char *title,const char *notice){
    lock_pin_wipe(pad->digits,sizeof(pad->digits));pad->count=0;pad->pressed=-1;pad->dirty_key=-1;pad->blocked=false;
    if(title)snprintf(pad->title,sizeof(pad->title),"%s",title);
    snprintf(pad->notice,sizeof(pad->notice),"%s",notice?notice:"");
}
void ui_pinpad_begin(ui_pinpad_t *pad,const uint8_t *frame,const char *title){
    ui_pinpad_end(pad);ui_pinpad_reset(pad,title,"");
    // 工作区有固定上限且保留1MiB PSRAM，不在输入循环重新处理原图。
    // Fixed workspace with a 1MiB PSRAM reserve; never reprocess artwork in the typing loop.
    size_t need=PAD_W*PAD_H/2u+LOW_W*LOW_H*2u;
    if(heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)<need+1024u*1024u)return;
    pad->background=heap_caps_malloc(PAD_W*PAD_H/2u,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    uint8_t *low=heap_caps_malloc(LOW_W*LOW_H*2u,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!pad->background||!low){free(pad->background);pad->background=NULL;free(low);return;}
    uint8_t *blur=low+LOW_W*LOW_H;
    for(int y=0;y<LOW_H;++y)for(int x=0;x<LOW_W;++x){
        unsigned sum=0,n=0;
        for(int j=0;j<9;++j)for(int i=0;i<9;++i){int xx=x*9+i,yy=y*9+j;if(xx<PAD_W&&yy<PAD_H){sum+=frame?(pixel(frame,xx,yy)>>4)*17u:224u;++n;}}
        low[y*LOW_W+x]=sum/n;
    }
    for(int y=0;y<LOW_H;++y)for(int x=0;x<LOW_W;++x){
        unsigned sum=0,n=0;
        // 较小核保留封面结构，白色混入减至45%，保持圆键与底图的层次。
        // A smaller kernel retains cover structure; 45% white lift separates the art from the circular keys.
        for(int j=-2;j<=2;++j)for(int i=-2;i<=2;++i){int xx=x+i,yy=y+j;if(xx>=0&&xx<LOW_W&&yy>=0&&yy<LOW_H){sum+=low[yy*LOW_W+xx];++n;}}
        blur[y*LOW_W+x]=(sum/n*11+255*9+10)/20;
    }
    memset(pad->background,255,PAD_W*PAD_H/2u);
    for(int y=0;y<PAD_H;++y)for(int x=0;x<PAD_W;++x){
        // 双线性重建避免9px方块；背景最终只量化一次。
        // Bilinear reconstruction avoids 9px blocks; quantize the backdrop just once.
        int ix=x/9,iy=y/9,nx=ix+1<LOW_W?ix+1:ix,ny=iy+1<LOW_H?iy+1:iy;
        int fx=x%9,fy=y%9;
        unsigned v=(blur[iy*LOW_W+ix]*(9-fx)*(9-fy)+blur[iy*LOW_W+nx]*fx*(9-fy)+
            blur[ny*LOW_W+ix]*(9-fx)*fy+blur[ny*LOW_W+nx]*fx*fy+40)/81;
        epd_draw_pixel(x,y,ui_image_dither_gray(v,x,y),pad->background);
    }
    free(low);
}
static bool intersects(EpdRect a,EpdRect b){return a.x<b.x+b.width&&b.x<a.x+a.width&&a.y<b.y+b.height&&b.y<a.y+a.height;}
static EpdRect key_rect(int hit){
    if(hit<10){int col=hit?((hit-1)%3):1,row=hit?((hit-1)/3):3;return(EpdRect){xs[col]-RADIUS-3,ys[row]-RADIUS-3,2*RADIUS+6,2*RADIUS+6};}
    return(EpdRect){hit==10?74:470,1100,140,68};
}
static EpdRect joined(EpdRect a,EpdRect b){int x=a.x<b.x?a.x:b.x,y=a.y<b.y?a.y:b.y,r=a.x+a.width>b.x+b.width?a.x+a.width:b.x+b.width,t=a.y+a.height>b.y+b.height?a.y+a.height:b.y+b.height;return(EpdRect){x,y,r-x,t-y};}
static void fallback_text(uint8_t *fb,int cx,int cy,int em,const char *value,uint8_t color){
    int px=ui_font_title_px(em,value),a=0,b=0;ui_font_measure_line_px(px,value,&a,&b);
    // 内建接口接受灰阶编号0..15；用当前背景保持按下态，不覆盖一块浅色底。
    // The built-in API takes levels 0..15; retain the pressed backdrop without a light patch.
    ui_font_draw_text_px(fb,cx,cy+(a-b)/2,px,value,EPD_DRAW_ALIGN_CENTER,color>>4,pixel(fb,cx,cy)>>4,false);
}
static void text(uint8_t *fb,int cx,int cy,int em,const char *value,uint8_t color,const ui_pinpad_t *pad,int key){
    (void)pad;(void)key;
    if(!ttf_font_ready()||!ttf_font_has_text(value)){fallback_text(fb,cx,cy,em,value,color);return;}
    int px=ttf_em_height_px(em),a=0,b=0;ttf_measure_line_px(px,value,&a,&b);
    int w=ttf_text_width_px(px,value)+4,h=a+b+4;
    if(w<=0||w>PAD_W||h<=0||h>180){fallback_text(fb,cx,cy,em,value,color);return;}
    uint8_t *mask=heap_caps_calloc(1,(size_t)w*h,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!mask){fallback_text(fb,cx,cy,em,value,color);return;}
    if(ttf_text_mask_px(mask,w,h,2,a+2,px,value))for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        unsigned alpha=mask[y*w+x];int xx=cx-w/2+x,yy=cy-(a+b)/2-2+y;
        if(alpha&&xx>=0&&xx<PAD_W&&yy>=0&&yy<PAD_H){unsigned bg=pixel(fb,xx,yy);epd_draw_pixel(xx,yy,(color*alpha+bg*(255-alpha)+127)/255,fb);}
    }
    free(mask);
}
static bool rounded_inside(float x,float y,float left,float top,float width,float height,float radius){
    if(x<left||x>left+width||y<top||y>top+height)return false;
    float cx=x<left+radius?left+radius:x>left+width-radius?left+width-radius:x;
    float cy=y<top+radius?top+radius:y>top+height-radius?top+height-radius:y;
    float dx=x-cx,dy=y-cy;return dx*dx+dy*dy<=radius*radius;
}
static void paint(uint8_t *fb,const ui_pinpad_t *pad,EpdRect area){
    int right=area.x+area.width,bottom=area.y+area.height;
    for(int y=area.y;y<bottom&&y<PAD_H;++y)for(int x=area.x;x<right&&x<PAD_W;++x)
        if(x>=0&&y>=0){
            uint8_t gray=pad->background?pixel(pad->background,x,y):0xe0;
            epd_draw_pixel(x,y,gray,fb);
        }
    if(intersects(area,(EpdRect){180,135,324,168})){
        // 锁用图标库里的那一枚：全机图标同源，换起来也只改这里。
        // The lock comes from the icon set, the single source for every icon in the firmware.
        ui_draw_icon(fb,342,165,56,UI_ICON_LOCK,0);
        text(fb,342,260,48,pad->title,0,pad,-1);
    }
    if(intersects(area,ui_pinpad_entry_area())){
        unsigned count=pad->count+(pad->pressed>=0&&pad->pressed<10&&pad->count<4);
        for(int i=0;i<4;++i){int x=241+i*67;epd_draw_circle(x,401,14,0,fb);epd_draw_circle(x,401,13,0,fb);epd_fill_circle(x,401,12,(unsigned)i<count?0:255,fb);}
        if(pad->notice[0])text(fb,342,445,20,pad->notice,0,pad,-1);
    }
    for(int hit=0;hit<12;++hit){EpdRect box=key_rect(hit);if(!intersects(area,box))continue;bool pressed=pad->pressed==hit;
        if(hit<10){int cx=box.x+RADIUS+3,cy=box.y+RADIUS+3;
            // 圆键白底只在进入时铺好；输入只改内环，不对灰阶背景施加DU。
            // Establish white circular keys on entry; DU input changes only the inner ring, never gray artwork.
            epd_fill_circle(cx,cy,RADIUS,255,fb);
            for(int r=RADIUS;r>=RADIUS-2;--r)epd_draw_circle(cx,cy,r,0,fb);
            for(int r=67;r>=63;--r)epd_draw_circle(cx,cy,r,pressed?0:255,fb);
            char numeral[2]={(char)('0'+hit),0};text(fb,cx,cy,74,numeral,0,pad,hit);
        }else text(fb,box.x+box.width/2,1132,27,hit==10?"取消":"删除",0,pad,hit);
    }
}
void ui_pinpad_paint(uint8_t *fb,const ui_pinpad_t *pad,EpdRect area){paint(fb,pad,area);}

void ui_pinpad_paint_input(uint8_t *fb,const ui_pinpad_t *pad){
    // 不生成TTF蒙版、不分配内存，只改黑白反馈；矩形内其余像素保持原样。
    // No glyph masks or allocations: change binary feedback only, retaining every surrounding pixel.
    int hit=pad->dirty_key;
    if(hit>=0&&hit<10){
        EpdRect box=key_rect(hit);int cx=box.x+RADIUS+3,cy=box.y+RADIUS+3;
        for(int r=67;r>=63;--r)epd_draw_circle(cx,cy,r,pad->pressed==hit?0:255,fb);
    }else if(hit>=10){
        EpdRect box=key_rect(hit);
        ui_fill_rect_fast((EpdRect){box.x+32,box.y+54,76,2},pad->pressed==hit?0:255,fb);
    }
    unsigned count=pad->count+(pad->pressed>=0&&pad->pressed<10&&pad->count<4);
    for(int i=0;i<4;++i)epd_fill_circle(241+i*67,401,12,(unsigned)i<count?0:255,fb);
}
static int hit_test(int x,int y){
    if(x<0||x>=PAD_W||y<0||y>=PAD_H)return -1;
    for(int i=0;i<12;++i){EpdRect box=key_rect(i);if(i<10){int dx=x-box.x-RADIUS-3,dy=y-box.y-RADIUS-3;if(dx*dx+dy*dy<=RADIUS*RADIUS)return i;}else if(x>=box.x&&x<box.x+box.width&&y>=box.y&&y<box.y+box.height)return i;}
    return -1;
}
ui_pin_result_t ui_pinpad_handle(ui_pinpad_t *pad,const ui_gesture_event_t *ev,EpdRect *dirty){
    *dirty=ui_pinpad_entry_area();pad->dirty_key=-1;int prior=pad->pressed;
    if(ev->type==UI_GESTURE_PRESS){
        int hit=hit_test(ev->x0,ev->y0);if(hit<0||((pad->blocked||pad->count==4)&&hit<10))return UI_PIN_NONE;
        pad->pressed=hit;pad->dirty_key=hit;*dirty=joined(*dirty,key_rect(hit));return UI_PIN_CHANGED;
    }
    if(prior<0)return UI_PIN_NONE;
    if(ev->type==UI_GESTURE_MOVE&&hit_test(ev->x,ev->y)==prior)return UI_PIN_NONE;
    pad->pressed=-1;pad->dirty_key=prior;*dirty=joined(*dirty,key_rect(prior));
    if(ev->type!=UI_GESTURE_TAP||hit_test(ev->x,ev->y)!=prior||hit_test(ev->x0,ev->y0)!=prior)return UI_PIN_CHANGED;
    if(prior==10)return UI_PIN_CANCEL;
    if(prior==11){if(pad->count)pad->digits[--pad->count]=0;return UI_PIN_CHANGED;}
    if(pad->count<4){pad->digits[pad->count++]=(char)('0'+prior);pad->digits[pad->count]=0;}
    return pad->count==4?UI_PIN_COMPLETE:UI_PIN_CHANGED;
}
