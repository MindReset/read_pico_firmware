/* SPDX-License-Identifier: Apache-2.0
 * 中文：忠实移植确认的时钟；表盘逐行解码，指针按原形状光栅化，秒针只重画脏区。
 * English: Port the approved clocks faithfully; decode faces by row, rasterize original hands and redraw only dirty seconds.
 */
#include "ui_home_clock.h"
#include "ui_home_clock_assets.h"
#include "ui_home_clock_new_assets.h"
#include "ui_home_clock_blur.h"
#include "ui_image_dither.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#define TAU 6.28318530718f
typedef struct {uint8_t *fb;EpdRect bounds,clip;bool binary;unsigned phase,trail_start,trail_steps;} clock_canvas_t;
typedef struct {EpdRect bounds,clip;int hour,minute,rotation;bool binary,valid;unsigned age;} clock_base_key_t;
static uint8_t *s_base_cache;
static clock_base_key_t s_base_keys[2];
static unsigned s_base_age;
void ui_home_clock_bind_base_cache(uint8_t *memory,size_t bytes){
    if(bytes<UI_HOME_CLOCK_BASE_CACHE_BYTES)memory=NULL;
    if(memory!=s_base_cache){s_base_cache=memory;memset(s_base_keys,0,sizeof(s_base_keys));}
}
static bool base_candidate(const clock_canvas_t *c,EpdRect *physical){
    if(!s_base_cache||c->trail_steps||c->clip.width!=c->bounds.width||c->clip.height<c->bounds.height*9/10)return false;
    *physical=ui_rotate_rect_to_fb(c->clip);
    return !(physical->x&1)&&!(physical->width&1)&&
        (size_t)physical->width*physical->height/2<=UI_HOME_CLOCK_BASE_CACHE_BYTES/2;
}
static bool base_key_matches(const clock_base_key_t *k,const clock_canvas_t *c,const struct tm *date){
    return k->valid&&k->hour==date->tm_hour&&k->minute==date->tm_min&&k->binary==c->binary&&
        k->rotation==epd_get_rotation()&&!memcmp(&k->bounds,&c->bounds,sizeof(EpdRect))&&!memcmp(&k->clip,&c->clip,sizeof(EpdRect));
}
static void base_copy(clock_canvas_t *c,EpdRect p,unsigned slot,bool restore){
    uint8_t *cache=s_base_cache+slot*(UI_HOME_CLOCK_BASE_CACHE_BYTES/2);
    size_t stride=(size_t)epd_width()/2,span=(size_t)p.width/2;
    for(int y=p.y;y<p.y+p.height;++y){uint8_t *row=c->fb+(size_t)y*stride+p.x/2;
        if(restore)memcpy(row,cache,span);else memcpy(cache,row,span);cache+=span;}
}
static bool base_restore(clock_canvas_t *c,const struct tm *date){
    EpdRect p;if(!base_candidate(c,&p))return false;
    for(unsigned i=0;i<2;++i)if(base_key_matches(&s_base_keys[i],c,date)){
        base_copy(c,p,i,true);s_base_keys[i].age=++s_base_age;return true;}
    return false;
}
static void base_store(clock_canvas_t *c,const struct tm *date){
    EpdRect p;if(!base_candidate(c,&p))return;
    unsigned i=!s_base_keys[0].valid?0:!s_base_keys[1].valid?1:s_base_keys[0].age<s_base_keys[1].age?0:1;
    base_copy(c,p,i,false);
    s_base_keys[i]=(clock_base_key_t){.bounds=c->bounds,.clip=c->clip,.hour=date->tm_hour,.minute=date->tm_min,
        .rotation=epd_get_rotation(),.binary=c->binary,.valid=true,.age=++s_base_age};
}
static float clamp(float x,float lo,float hi){return fminf(hi,fmaxf(lo,x));}
static EpdRect intersect(EpdRect a,EpdRect b){
    int x=fmaxf(a.x,b.x),y=fmaxf(a.y,b.y),xx=fminf(a.x+a.width,b.x+b.width),yy=fminf(a.y+a.height,b.y+b.height);
    return(EpdRect){x,y,xx>x?xx-x:0,yy>y?yy-y:0};
}
static float cx(const clock_canvas_t *c,float x){return c->bounds.x+(x-24)*c->bounds.width/320.f;}
static float cy(const clock_canvas_t *c,float y){return c->bounds.y+(y-640)*c->bounds.height/350.f;}
static uint8_t gray_at(const uint8_t *fb,int x,int y){
    int px=x,py=y;
    switch(epd_get_rotation()){
        case EPD_ROT_PORTRAIT:px=epd_width()-y-1;py=x;break;
        case EPD_ROT_INVERTED_PORTRAIT:px=y;py=epd_height()-x-1;break;
        case EPD_ROT_INVERTED_LANDSCAPE:px=epd_width()-x-1;py=epd_height()-y-1;break;
        default:break;
    }
    return(epd_get_pixel(px,py,epd_width(),epd_height(),fb)>>4)*17;
}
static void pixel(clock_canvas_t *c,int x,int y,float gray,float alpha){
    if(alpha<=0||x<c->clip.x||y<c->clip.y||x>=c->clip.x+c->clip.width||y>=c->clip.y+c->clip.height)return;
    float old=gray_at(c->fb,x,y);uint8_t g=lroundf(clamp(old+(gray-old)*fminf(1,alpha),0,255));
    epd_draw_pixel(x,y,c->binary?ui_image_dither_acrylic_bw(g,x,y):ui_image_dither_gray(g,x,y),c->fb);
}
static bool row_decode(unsigned style,unsigned y,uint8_t row[135]){
    const uint8_t *p=clock_pixels[style]+clock_offsets[style][y],*end=clock_pixels[style]+clock_offsets[style][y+1];
    unsigned n=0;
    while(p<end&&n<135){unsigned flag=*p++,len=(flag&127)+1;if(len>135-n)return false;
        if(flag&128){if(p==end)return false;memset(row+n,*p++,len);}
        else{if((unsigned)(end-p)<len)return false;memcpy(row+n,p,len);p+=len;}n+=len;
    }
    return n==135&&p==end;
}
static void background(clock_canvas_t *c,unsigned style){
    epd_fill_rect(c->clip,255,c->fb);
    EpdRect image={(int)floorf(cx(c,42)),(int)floorf(cy(c,663)),(int)ceilf(270*c->bounds.width/320.f),(int)ceilf(270*c->bounds.height/350.f)};
    EpdRect part=intersect(c->clip,image);uint8_t row[135];int previous=-1;
    for(int y=part.y;y<part.y+part.height;++y){
        int sy=clamp((y+.5f-c->bounds.y)*350/c->bounds.height+640-663,0,269);
        if(sy!=previous){if(!row_decode(style,sy,row))return;previous=sy;}
        for(int x=part.x;x<part.x+part.width;++x){
            int sx=clamp((x+.5f-c->bounds.x)*320/c->bounds.width+24-42,0,269);
            uint8_t g=((row[sx/2]>>((sx&1)*4))&15)*17;
            epd_draw_pixel(x,y,c->binary?ui_image_dither_acrylic_bw(g,x,y):g,c->fb);
        }
    }
}
static void polygon(clock_canvas_t *c,const float *xs,const float *ys,int count,float gray,bool outlined){
    float xx[32],yy[32],xmin=10000,xmax=-10000,ymin=10000,ymax=-10000;
    for(int i=0;i<count;++i){xx[i]=cx(c,xs[i]);yy[i]=cy(c,ys[i]);xmin=fminf(xmin,xx[i]);xmax=fmaxf(xmax,xx[i]);ymin=fminf(ymin,yy[i]);ymax=fmaxf(ymax,yy[i]);}
    float ink=c->bounds.width/320.f*1.2f;
    EpdRect area={(int)floorf(xmin-ink-1),(int)floorf(ymin-ink-1),(int)ceilf(xmax-xmin+ink*2+3),(int)ceilf(ymax-ymin+ink*2+3)};area=intersect(area,c->clip);
    for(int y=area.y;y<area.y+area.height;++y)for(int x=area.x;x<area.x+area.width;++x){
        float alpha=0;
        for(int sy=0;sy<3;++sy)for(int sx=0;sx<3;++sx){float px=x+(sx+.5f)/3,py=y+(sy+.5f)/3;bool inside=false;
            for(int i=0,j=count-1;i<count;j=i++)if((yy[i]>py)!=(yy[j]>py)&&px<(xx[j]-xx[i])*(py-yy[i])/(yy[j]-yy[i])+xx[i])inside=!inside;
            if(inside)alpha+=1.f/9;
        }
        pixel(c,x,y,gray,alpha);
        if(outlined){float distance=10000;
            for(int i=0,j=count-1;i<count;j=i++){float vx=xx[i]-xx[j],vy=yy[i]-yy[j],square=vx*vx+vy*vy;
                float t=square?clamp(((x+.5f-xx[j])*vx+(y+.5f-yy[j])*vy)/square,0,1):0;
                distance=fminf(distance,hypotf(x+.5f-xx[j]-t*vx,y+.5f-yy[j]-t*vy));}
            pixel(c,x,y,25,clamp(ink-distance,0,1));
        }
    }
}
static void shape(clock_canvas_t *c,const float points[][2],int count,float angle,float gray,bool outlined){
    float x[16],y[16],s=sinf(angle),co=cosf(angle);
    for(int i=0;i<count;++i){x[i]=177+s*points[i][1]+co*points[i][0];y[i]=798-co*points[i][1]+s*points[i][0];}
    polygon(c,x,y,count,gray,outlined);
}
static void disc(clock_canvas_t *c,float x,float y,float radius,float center,float edge){
    float mx=cx(c,x),my=cy(c,y),r=radius*c->bounds.width/320.f;
    EpdRect area={(int)floorf(mx-r-1),(int)floorf(my-r-1),(int)ceilf(r*2+3),(int)ceilf(r*2+3)};area=intersect(area,c->clip);
    for(int yy=area.y;yy<area.y+area.height;++yy)for(int xx=area.x;xx<area.x+area.width;++xx){
        float distance=hypotf(xx+.5f-mx,yy+.5f-my),t=fminf(1,distance/r),t2=t*t;
        pixel(c,xx,yy,center+(edge-center)*t2*t2*t,clamp(r+.5f-distance,0,1));
    }
}

static float art_x(unsigned style,float x){return clock_new_transform[style][1]+clock_new_transform[style][0]*x;}
static float art_y(unsigned style,float y){return clock_new_transform[style][2]+clock_new_transform[style][0]*y;}
static void new_background(clock_canvas_t *c,unsigned style){
    uint8_t row[160];int previous=-1;epd_fill_rect(c->clip,255,c->fb);
    for(int y=c->clip.y;y<c->clip.y+c->clip.height;++y){
        unsigned sy=(y-c->bounds.y)*350/c->bounds.height;if(sy>=350)continue;
        if((int)sy!=previous){
            const uint8_t *p=clock_new_pixels[style]+clock_new_offsets[style][sy],*end=clock_new_pixels[style]+clock_new_offsets[style][sy+1];unsigned n=0;
            while(p<end&&n<160){unsigned flag=*p++,len=(flag&127)+1;if(len>160-n)return;
                if(flag&128){if(p==end)return;memset(row+n,*p++,len);}else{if((unsigned)(end-p)<len)return;memcpy(row+n,p,len);p+=len;}n+=len;
            }if(n!=160||p!=end)return;previous=sy;
        }
        for(int x=c->clip.x;x<c->clip.x+c->clip.width;++x){
            unsigned sx=(x-c->bounds.x)*320/c->bounds.width;if(sx>=320)continue;uint8_t g=((row[sx/2]>>((sx&1)*4))&15)*17;
            epd_draw_pixel(x,y,c->binary?ui_image_dither_acrylic_bw(g,x,y):g,c->fb);
        }
    }
}
static void art_disc(clock_canvas_t *c,unsigned style,float x,float y,float radius,float gray,float edge){disc(c,art_x(style,x),art_y(style,y),radius*clock_new_transform[style][0],gray,edge);}
static void art_ellipse(clock_canvas_t *c,unsigned style,float x,float y,float rx,float ry,float gray,float edge){
    float mx=cx(c,art_x(style,x)),my=cy(c,art_y(style,y));rx*=clock_new_transform[style][0]*c->bounds.width/320.f;ry*=clock_new_transform[style][0]*c->bounds.height/350.f;
    EpdRect area={(int)floorf(mx-rx-1),(int)floorf(my-ry-1),(int)ceilf(rx*2+3),(int)ceilf(ry*2+3)};area=intersect(area,c->clip);
    for(int yy=area.y;yy<area.y+area.height;++yy)for(int xx=area.x;xx<area.x+area.width;++xx){float r=hypotf((xx+.5f-mx)/rx,(yy+.5f-my)/ry),t=fminf(1,r),t2=t*t;pixel(c,xx,yy,gray+(edge-gray)*t2*t2*t,clamp((1-r)*fminf(rx,ry)+.5f,0,1));}
}
static void art_line(clock_canvas_t *c,unsigned style,float x0,float y0,float x1,float y1,float width,float gray){
    x0=cx(c,art_x(style,x0));y0=cy(c,art_y(style,y0));x1=cx(c,art_x(style,x1));y1=cy(c,art_y(style,y1));width*=clock_new_transform[style][0]*c->bounds.width/320.f;
    float dx=x1-x0,dy=y1-y0,square=dx*dx+dy*dy;
    EpdRect area={(int)floorf(fminf(x0,x1)-width-1),(int)floorf(fminf(y0,y1)-width-1),(int)ceilf(fabsf(dx)+2*width+3),(int)ceilf(fabsf(dy)+2*width+3)};area=intersect(area,c->clip);
    for(int y=area.y;y<area.y+area.height;++y)for(int x=area.x;x<area.x+area.width;++x){float t=square?clamp(((x+.5f-x0)*dx+(y+.5f-y0)*dy)/square,0,1):0;pixel(c,x,y,gray,clamp(width+.5f-hypotf(x+.5f-x0-t*dx,y+.5f-y0-t*dy),0,1));}
}
static void cat_pupil(clock_canvas_t *c,float x,float y,float rx,float ry,uint8_t ink){
    float mx=cx(c,art_x(1,x)),my=cy(c,art_y(1,y));rx*=clock_new_transform[1][0]*c->bounds.width/320.f;ry*=clock_new_transform[1][0]*c->bounds.height/350.f;
    EpdRect r={(int)floorf(mx-rx),(int)floorf(my-ry),(int)ceilf(rx*2+1),(int)ceilf(ry*2+1)};r=intersect(r,c->clip);
    for(int yy=r.y;yy<r.y+r.height;++yy)for(int xx=r.x;xx<r.x+r.width;++xx){float dx=(xx+.5f-mx)/rx,dy=(yy+.5f-my)/ry;if(dx*dx+dy*dy<=1)epd_draw_pixel(xx,yy,ink,c->fb);}
}
static void cat_hands(clock_canvas_t *c,const struct tm *date){
    // 猫眼只恢复旧瞳孔和画新位置，不重绘外框或文字。/ Restore old pupils and draw their new positions without changing the frame or labels.
    float gaze=sinf((c->phase%40)*TAU/40);
    for(unsigned eye=0;eye<2;++eye){float x=eye?213:141;
        // 静态眼眶保留灰阶，移动瞳孔只在纯白核心上切黑白，避免反复补移动边缘灰阶。
        // Retain gray rims; move binary pupils inside white cores without repeated moving-edge gray correction.
        art_ellipse(c,1,x,716,24,33,255,255);
        cat_pupil(c,x+8*gaze,716,7.8f,27,0);cat_pupil(c,x+8*gaze-2,705,1.6f,6,255);
    }
    float angles[]={((date->tm_hour%12)+date->tm_min/60.f)*TAU/12,date->tm_min*TAU/60};
    for(int hand=0;hand<2;++hand){float a=angles[hand],sn=sinf(a),co=cosf(a),w=hand?1.9f:2.9f,len=hand?38:27;
        float xs[]={177-sn*4-co*w,177-sn*4+co*w,177+sn*len+co*w,177+sn*len-co*w};
        float ys[]={918+co*4-sn*w,918+co*4+sn*w,918-co*len+sn*w,918-co*len-sn*w};
        for(unsigned j=0;j<4;++j){xs[j]=art_x(1,xs[j]);ys[j]=art_y(1,ys[j]);}polygon(c,xs,ys,4,248,false);
    }art_disc(c,1,177,918,5.5f,248,193);
}
static void dali_hands(clock_canvas_t *c,const struct tm *date){
    float x=177-17*sinf(.24f*2.4f),y=830+161*.02f;
    float angles[]={((date->tm_hour%12)+date->tm_min/60.f)*TAU/12,date->tm_min*TAU/60};
    for(unsigned hand=0;hand<2;++hand){float sn=sinf(angles[hand]),co=cosf(angles[hand]),len=hand?76:48,w=hand?2.1f:3.3f;
        art_line(c,0,x-sn*10+1.8f,y+co*10+2,x+sn*len+1.8f,y-co*len+2,w+1.6f,100);
        art_line(c,0,x-sn*10,y+co*10,x+sn*len,y-co*len,w,247);
    }art_disc(c,0,x,y,7.1f,99,173);art_disc(c,0,x-1,y-1,5.3f,248,215);
}

static float frosted_alpha(float distance){
    float at=distance*16;unsigned i=(unsigned)at;
    if(i>=1024)return 0;
    return (clock_blur_falloff[i]+(clock_blur_falloff[i+1]-clock_blur_falloff[i])*(at-i))/65535.f;
}
static float capsule(float x,float y,float s,float co,float start,float end){float along=x*s-y*co;return hypotf(x*co+y*s,along-clamp(along,start,end));}
static void hands(clock_canvas_t *c,const struct tm *date,ui_home_clock_style_t style){
    float hour=((date->tm_hour%12)+date->tm_min/60.f)*TAU/12,minute=date->tm_min*TAU/60;
    if(style==UI_HOME_CLOCK_ASTERISK){
        const float h[][2]={{-7,-23},{7,-23},{7,69},{23,69},{0,105},{-23,69},{-7,69}};
        const float m[][2]={{-6,-21},{6,-21},{4,101},{0,109},{-4,101}};
        shape(c,h,7,hour,250,true);shape(c,m,5,minute,250,true);disc(c,177,798,3.8f,155,205);
    }else if(style==UI_HOME_CLOCK_MOON){
        const float h[][2]={{-4.5f,-13},{4.5f,-13},{4,54},{0,59},{-4,54}};
        const float m[][2]={{-1.8f,-15},{1.8f,-15},{1.7f,83},{0,88},{-1.7f,83}};
        shape(c,h,5,hour,25,false);shape(c,m,5,minute,25,false);disc(c,177,798,4.5f,80,145);
    }else{
        // 只计算被请求的区域，秒针移动不重新合成整张磨砂表盘。/ Compute only the requested area; second moves never recompose the full frosted face.
        // 分针按分钟定稿，秒级局刷只移动圆点，确保裁剪区域外的目标保持不变。
        // Settle the minute hand on minute boundaries; second crops move only the dot, preserving the target outside the crop.
        minute=date->tm_min*TAU/60;
        float hs=sinf(hour),hc=cosf(hour),ms=sinf(minute),mc=cosf(minute);
        float start=(c->trail_start%600)*TAU/600,end=((c->trail_start+c->trail_steps)%600)*TAU/600;
        float ax=91*sinf(start),ay=-91*cosf(start),zx=91*sinf(end),zy=-91*cosf(end);
        EpdRect area={(int)floorf(cx(c,48)),(int)floorf(cy(c,669)),(int)ceilf(259*c->bounds.width/320.f),(int)ceilf(259*c->bounds.height/350.f)};area=intersect(area,c->clip);
        for(int y=area.y;y<area.y+area.height;++y)for(int x=area.x;x<area.x+area.width;++x){
            float xx=(x+.5f-c->bounds.x)*320/c->bounds.width+24-177,yy=(y+.5f-c->bounds.y)*350/c->bounds.height+640-798,r=hypotf(xx,yy);
            if(r>128.5f)continue;
            float gray=232-yy*.055f,sd=capsule(xx,yy,hs,hc,35,65),ld=capsule(xx,yy,ms,mc,12,94);
            float a=.65f*frosted_alpha(sd),b=.70f*frosted_alpha(ld);
            gray+=(111-gray)*a;gray+=(92-gray)*b;
            // 走过的位置保留浅灰轨迹；一圈后固定，不叠加黑度或执行擦环刷新。
            // Retain a light-gray trail, bounded to one lap without darkening or an erase-ring refresh.
            if(c->trail_steps){
                float angle=atan2f(xx,-yy);if(angle<0)angle+=TAU;
                float delta=fmodf(angle-start+TAU,TAU)*600/TAU;
                float distance=c->trail_steps>=600||delta<=c->trail_steps?fabsf(r-91):fminf(hypotf(xx-ax,yy-ay),hypotf(xx-zx,yy-zy));
                gray+=(102-gray)*(.30f*clamp(15.5f-distance,0,1));
            }
            pixel(c,x,y,gray,clamp(128.5f-r,0,1));
        }
        base_store(c,date);
        float second=(c->phase%600)*TAU/600;disc(c,177+91*sinf(second),798-91*cosf(second),15,69,88);
    }
}
// 小屋按已确认前立面等比缩放，数字使用活动系统字形，不缓存另一份字库。
// Scale the approved house elevation proportionally; numerals use the active system face, without another font cache.
static float house_x(float x){return 177+(x-177)*.8f;}
static float house_y(float y){return 662+(y-650)*.8f;}
static void house_poly(clock_canvas_t *c,const float *x,const float *y,int count,float gray,bool outline){
    float xx[8],yy[8];for(int i=0;i<count;++i){xx[i]=house_x(x[i]);yy[i]=house_y(y[i]);}
    polygon(c,xx,yy,count,gray,outline);
}
static void house_disc(clock_canvas_t *c,float x,float y,float radius,float center,float edge){disc(c,house_x(x),house_y(y),radius*.8f,center,edge);}
static void house(clock_canvas_t *c,const struct tm *date){
    epd_fill_rect(c->clip,255,c->fb);
    const float x[]={177,269,269,85,85},y[]={650,735,1007,1007,735};float sx[5],sy[5];
    for(int i=0;i<5;++i){sx[i]=x[i]+3;sy[i]=y[i]+4;}
    house_poly(c,sx,sy,5,225,false);house_poly(c,x,y,5,241,false);
    // 单像素柔和外缘，保持房体浅灰，不压成黑框。/ A soft one-pixel edge retains the pale body instead of forming a black frame.
    float xx[5],yy[5];for(int i=0;i<5;++i){xx[i]=cx(c,house_x(x[i]));yy[i]=cy(c,house_y(y[i]));}
    for(int py=c->clip.y;py<c->clip.y+c->clip.height;++py)for(int px=c->clip.x;px<c->clip.x+c->clip.width;++px){
        float distance=10000;
        for(int i=0,j=4;i<5;j=i++){float vx=xx[i]-xx[j],vy=yy[i]-yy[j],square=vx*vx+vy*vy;
            float t=clamp(((px+.5f-xx[j])*vx+(py+.5f-yy[j])*vy)/square,0,1);
            distance=fminf(distance,hypotf(px+.5f-xx[j]-t*vx,py+.5f-yy[j]-t*vy));}
        pixel(c,px,py,153,clamp(.8f*c->bounds.width/320.f+.5f-distance,0,1));
    }
    const float lipx[]={87,268,268,87},lipy[]={1004,1004,1007,1007};house_poly(c,lipx,lipy,4,208,false);
    house_disc(c,177,742,35,105,153);house_disc(c,177,744,32,30,65);
    const float leverx[]={173,181,188,166},levery[]={741,741,770,770};house_poly(c,leverx,levery,4,204,false);
    house_disc(c,177,745,2.3f,126,174);house_disc(c,177,787,7,119,169);house_disc(c,177,787,5.2f,243,224);
    house_disc(c,177,825,4.8f,84,130);house_disc(c,177,826,2.8f,135,160);
    for(int i=0;i<12;++i){if(i%3==0)continue;float a=i*TAU/12,dx=sinf(a),dy=-cosf(a),nx=cosf(a),ny=sinf(a);
        float tx[]={177+dx*55-nx*1.6f,177+dx*55+nx*1.6f,177+dx*68+nx*1.6f,177+dx*68-nx*1.6f};
        float ty[]={913+dy*55-ny*1.6f,913+dy*55+ny*1.6f,913+dy*68+ny*1.6f,913+dy*68-ny*1.6f};house_poly(c,tx,ty,4,51,false);
    }
    const float digitx[]={177,244,177,110},digity[]={845,913,982,913};const char *digits[]={"12","3","6","9"};
    for(int i=0;i<4;++i)ui_text_fixed_ink_vc(c->fb,lroundf(cx(c,house_x(digitx[i]))),lroundf(cy(c,house_y(digity[i]))),lroundf(26*.8f*c->bounds.width/320.f),digits[i],EPD_DRAW_ALIGN_CENTER,0x30);
    float angles[]={((date->tm_hour%12)+date->tm_min/60.f)*TAU/12,date->tm_min*TAU/60};
    for(int i=0;i<2;++i){float dx=sinf(angles[i]),dy=-cosf(angles[i]),nx=cosf(angles[i]),ny=sinf(angles[i]),wide=i?1.9f:3.8f,len=i?62:44,tail=i?15:11;
        float hx[]={177-dx*tail-nx*wide,177-dx*tail+nx*wide,177+dx*len+nx*wide,177+dx*len-nx*wide};
        float hy[]={913-dy*tail-ny*wide,913-dy*tail+ny*wide,913+dy*len+ny*wide,913+dy*len-ny*wide};house_poly(c,hx,hy,4,45,false);
    }
    house_disc(c,177,913,11,177,198);house_disc(c,177,913,8,38,65);
}
const char *ui_home_clock_name(ui_home_clock_style_t style){
    static const char *const names[]={"IKEA PS 1995","Asterisk Clock","Moon Clock","Kehai Clock","Dali Clock","Cat Clock","小屋时钟"};
    return names[(unsigned)style<UI_HOME_CLOCK_COUNT?style:0];
}
void ui_home_clock_trail_draw_region(uint8_t *fb,EpdRect b,const struct tm *date,bool binary,ui_home_clock_style_t style,EpdRect clip,unsigned phase,unsigned trail_start,unsigned trail_steps){
    if(!fb||!date||b.width<=0||b.height<=0||(unsigned)style>=UI_HOME_CLOCK_COUNT)return;
    if(style==UI_HOME_CLOCK_PS1995){ui_home_clock_draw(fb,b,date,binary);return;}
    clock_canvas_t c={fb,b,intersect(intersect(b,clip),(EpdRect){0,0,epd_rotated_display_width(),epd_rotated_display_height()}),binary,phase,trail_start,trail_steps>600?600:trail_steps};
    if(c.clip.width<=0||c.clip.height<=0)return;
    if(style==UI_HOME_CLOCK_KEHAI&&base_restore(&c,date)){
        float second=(phase%600)*TAU/600;disc(&c,177+91*sinf(second),798-91*cosf(second),15,69,88);return;
    }
    if(style==UI_HOME_CLOCK_HOUSE)house(&c,date);
    else if(style==UI_HOME_CLOCK_DALI||style==UI_HOME_CLOCK_CAT){new_background(&c,style==UI_HOME_CLOCK_CAT);if(style==UI_HOME_CLOCK_CAT)cat_hands(&c,date);else dali_hands(&c,date);}
    else {background(&c,style-1);hands(&c,date,style);}
}
void ui_home_clock_phase_draw_region(uint8_t *fb,EpdRect b,const struct tm *date,bool binary,ui_home_clock_style_t style,EpdRect clip,unsigned phase){ui_home_clock_trail_draw_region(fb,b,date,binary,style,clip,phase,0,0);}
void ui_home_clock_style_draw_region(uint8_t *fb,EpdRect b,const struct tm *date,bool binary,ui_home_clock_style_t style,EpdRect clip){ui_home_clock_phase_draw_region(fb,b,date,binary,style,clip,date?(unsigned)date->tm_sec*10:0);}
void ui_home_clock_style_draw(uint8_t *fb,EpdRect bounds,const struct tm *date,bool binary,ui_home_clock_style_t style){ui_home_clock_style_draw_region(fb,bounds,date,binary,style,bounds);}
EpdRect ui_home_clock_second_area(EpdRect b,const struct tm *before,const struct tm *after){
    return ui_home_clock_phase_area(b,(unsigned)before->tm_sec*10,(unsigned)after->tm_sec*10);
}
EpdRect ui_home_clock_phase_area(EpdRect b,unsigned before,unsigned after){
    clock_canvas_t c={.bounds=b};float a=(before%600)*TAU/600,z=(after%600)*TAU/600;
    int x0=lroundf(cx(&c,177+91*sinf(a))),y0=lroundf(cy(&c,798-91*cosf(a))),x1=lroundf(cx(&c,177+91*sinf(z))),y1=lroundf(cy(&c,798-91*cosf(z)));
    int r=ceilf(17*b.width/320.f);return intersect((EpdRect){fminf(x0,x1)-r,fminf(y0,y1)-r,abs(x1-x0)+2*r+1,abs(y1-y0)+2*r+1},b);
}

bool ui_home_clock_has_motion(ui_home_clock_style_t style){return style==UI_HOME_CLOCK_KEHAI||style==UI_HOME_CLOCK_CAT;}
EpdRect ui_home_clock_motion_area(EpdRect b,unsigned before,unsigned after,ui_home_clock_style_t style){
    if(style!=UI_HOME_CLOCK_CAT)return ui_home_clock_phase_area(b,before,after);
    clock_canvas_t c={.bounds=b};int left=floorf(cx(&c,art_x(1,123)))-2,top=floorf(cy(&c,art_y(1,687)))-2,right=ceilf(cx(&c,art_x(1,231)))+2,bottom=ceilf(cy(&c,art_y(1,745)))+2;
    return intersect((EpdRect){left,top,right-left+1,bottom-top+1},b);
}
