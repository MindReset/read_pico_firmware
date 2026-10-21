/* SPDX-License-Identifier: Apache-2.0
 * 中文：实际输入法的增量绘制须与完整绘制相同，且推屏范围包住所有变化像素。
 * English: Actual incremental keyboard paint must equal a fresh render and contain every changed pixel.
 */
#include "../main/ui/ui_keyboard.c"
#include <assert.h>
#include <stdint.h>
#define WIDTH 684
#define HEIGHT 1102
#define BYTES (WIDTH * HEIGHT)
int html_test_fail_after = -1;
static unsigned width_calls, text_calls;
static void pixel(uint8_t *fb,int x,int y,uint8_t gray) { assert(x>=0&&x<WIDTH&&y>=0&&y<HEIGHT);fb[y*WIDTH+x]=gray; }
static bool inside(EpdRect r,int radius,int x,int y) {
    if(x<r.x||x>=r.x+r.width||y<r.y||y>=r.y+r.height)return false;
    int cx=x<r.x+radius?r.x+radius-1:x>=r.x+r.width-radius?r.x+r.width-radius:x;
    int cy=y<r.y+radius?r.y+radius-1:y>=r.y+r.height-radius?r.y+r.height-radius:y;
    return (x-cx)*(x-cx)+(y-cy)*(y-cy)<=radius*radius;
}
void epd_fill_rect(EpdRect r,uint8_t gray,uint8_t *fb){for(int y=r.y;y<r.y+r.height;y++)for(int x=r.x;x<r.x+r.width;x++)pixel(fb,x,y,gray);}
void ui_fill_round_rect(uint8_t *fb,EpdRect r,int radius,uint8_t gray){for(int y=r.y;y<r.y+r.height;y++)for(int x=r.x;x<r.x+r.width;x++)if(inside(r,radius,x,y))pixel(fb,x,y,gray);}
void ui_draw_round_rect(uint8_t *fb,EpdRect r,int radius,uint8_t gray){EpdRect inner={r.x+1,r.y+1,r.width-2,r.height-2};for(int y=r.y;y<r.y+r.height;y++)for(int x=r.x;x<r.x+r.width;x++)if(inside(r,radius,x,y)&&!inside(inner,radius>0?radius-1:0,x,y))pixel(fb,x,y,gray);}
void epd_draw_line(int x,int y,int xx,int yy,uint8_t gray,uint8_t *fb){int dx=abs(xx-x),sx=x<xx?1:-1,dy=-abs(yy-y),sy=y<yy?1:-1,e=dx+dy;for(;;){pixel(fb,x,y,gray);if(x==xx&&y==yy)break;int ee=2*e;if(ee>=dy){e+=dy;x+=sx;}if(ee<=dx){e+=dx;y+=sy;}}}
bool ui_rect_hit(EpdRect r,uint16_t x,uint16_t y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
EpdRect ui_rect_union(EpdRect a,EpdRect b){int x=a.x<b.x?a.x:b.x,y=a.y<b.y?a.y:b.y,r=a.x+a.width>b.x+b.width?a.x+a.width:b.x+b.width,d=a.y+a.height>b.y+b.height?a.y+a.height:b.y+b.height;return(EpdRect){x,y,r-x,d-y};}
void ui_clear_rect_fast(uint8_t *fb,EpdRect r){epd_fill_rect(r,UI_GRAY_WHITE,fb);}
int ui_text_effective_px(int px){return px;}
int ui_text_fixed_width_px(int px,const char *text){++width_calls;int width=0;for(const unsigned char *p=(const unsigned char*)text;*p;p++)if((*p&192)!=128)width+=*p>=128?px:px/2;return width;}
int ui_text_fixed_context_width_px(int px,const char *text,const char *sample){(void)sample;return ui_text_fixed_width_px(px,text);}
void ui_text_fixed_vc(uint8_t *fb,int x,int y,int px,const char *text,enum EpdFontFlags align,bool inverse){
 ++text_calls;int width=ui_text_fixed_width_px(px,text);if(align==EPD_DRAW_ALIGN_CENTER)x-=width/2;else if(align==EPD_DRAW_ALIGN_RIGHT)x-=width;
 for(const unsigned char *p=(const unsigned char*)text;*p;p++)if((*p&192)!=128){int advance=*p>=128?px:px/2;for(int j=0;j<px/2;j++)for(int i=1;i<advance-1;i++)if((i+j+*p)%4)pixel(fb,x+i,y-px/4+j,inverse?255:0);x+=advance;}
}
void ui_text_fixed_context_vc(uint8_t *fb,int x,int y,int px,const char *text,const char *sample,enum EpdFontFlags align,bool inverse){(void)sample;ui_text_fixed_vc(fb,x,y,px,text,align,inverse);}
static uint8_t *fb,*reference,*previous;
static char value[121];static ui_text_edit_t edit;static const EpdRect field={36,243,612,82};
static unsigned updates,layouts,settles;
static ui_keyboard_update_t last_update;
static void fresh(uint8_t *pixels){memset(pixels,255,BYTES);ui_text_input_draw(pixels,&edit,field,28,false,NULL);ui_keyboard_draw(pixels,560);}
static void repaint(bool force){
 memcpy(previous,fb,BYTES);ui_keyboard_update_t update=ui_keyboard_update(fb,560,field,28,false,NULL,force);fresh(reference);
 for(int y=0;y<HEIGHT;y++)for(int x=0;x<WIDTH;x++){
  if(fb[y*WIDTH+x]!=reference[y*WIDTH+x]){fprintf(stderr,"stale input pixel at %d,%d: %u vs %u\n",x,y,fb[y*WIDTH+x],reference[y*WIDTH+x]);abort();}
  if(fb[y*WIDTH+x]!=previous[y*WIDTH+x])assert(ui_rect_hit(update.area,x,y));
 }
 if(update.area.width)updates++;
 if(update.layout)layouts++;
 last_update=update;
 if(update.settle){
  assert(!update.field&&!update.layout&&update.area.x==24&&update.area.y==612&&update.area.width==636&&update.area.height==114);
  ++settles;
 }
}
static void key(int x,int y){ui_keyboard_result_t result=ui_keyboard_tap(x,y,560,1000+(int64_t)updates*100);if(result==UI_KEYBOARD_CHANGED)repaint(false);}
static void begin(bool ascii){value[0]=0;ui_text_edit_init(&edit,value,sizeof(value));ui_keyboard_begin(&edit,ascii);fresh(fb);}
int main(void){
 fb=malloc(BYTES);reference=malloc(BYTES);previous=malloc(BYTES);assert(fb&&reference&&previous);
 begin(true);
 // 按下不输入，反馈范围只含该键；松开复原，滑出取消不误输入。
 // Press commits nothing, paints only its key, and restores on release/cancel.
 assert(ui_keyboard_press(26,742,560,0));unsigned old_layouts=layouts;repaint(false);assert(layouts==old_layouts&&!value[0]);
 assert(ui_keyboard_release());key(26,742);assert(!strcmp(value,"q"));
 assert(ui_keyboard_press(26,742,560,100));repaint(false);assert(ui_keyboard_release());repaint(false);assert(!strcmp(value,"q"));
 begin(true);
 for(int i=0;i<70;i++)key(26+(i%10)*64,742);
 assert(strlen(value)==70);
 for(int i=0;i<65;i++)key(600,900);
 unsigned before=text_calls;key(28,742);assert(text_calls-before<70); // 含对照完整绘制。/ Includes the reference full paint.
 key(50,900);key(26,742);key(26,1000);key(90,750);key(140,1000);key(140,1000);key(100,750);key(30,1000);
 assert(ui_text_input_tap(&edit,field,28,false,NULL,550,285));repaint(true);
 begin(false);
 const char *digits="64426";for(const char *p=digits;*p;p++){int i=*p-'1';key(120+(i%3)*150,742+(i/3)*78);}
 if(ui_keyboard_page(1))repaint(false);
 if(ui_keyboard_page(-1))repaint(false);
 key(88,685);key(220,580);key(26,742);key(600,900);key(30,580);
 key(580,820);key(590,900);key(480,1000);key(480,1000);
 // 九宫格到英文全键盘、数字页返回和拒绝英文九宫格，都不能留下旧按键。
 // T9/QWERTY language switches and numeric return must leave no stale keys or enable English T9.
 key(220,580);key(480,1000);assert(!s_chinese&&!s_nine);key(26,742);
 key(30,580);assert(s_chinese&&s_nine);key(480,1000);assert(s_chinese&&s_nine);
 key(30,1000);key(26,742);key(30,580);assert(!s_panel&&s_nine&&s_chinese);
 key(420,820);key(580,820);key(590,900);
 begin(false);const char *code="64426";for(const char *p=code;*p;p++){int i=*p-'1';key(120+(i%3)*150,742+(i/3)*78);}
 ui_keyboard_hold_start(590,770,560,0);
 for(int i=0;i<5;i++){assert(ui_keyboard_hold_tick(true,590,770,500+i*120)==UI_KEYBOARD_CHANGED);unsigned old_layouts=layouts;repaint(false);assert(layouts==old_layouts);}
 assert(ui_keyboard_hold_tick(false,590,770,1100)==UI_KEYBOARD_NONE);
 begin(true);for(int i=0;i<6;i++)key(26+i*64,742);
 ui_keyboard_hold_start(610,930,560,0);
 for(int i=0;i<6;i++){assert(ui_keyboard_hold_tick(true,610,930,500+i*120)==UI_KEYBOARD_CHANGED);unsigned old_layouts=layouts;repaint(false);assert(layouts==old_layouts);}
 // 按住连删期间保持反馈，不切布局；最后松开只还原删除键。
 // Held-repeat keeps feedback; releasing restores only backspace without a layout update.
 begin(true);strcpy(value,"abcde");ui_text_edit_place(&edit,5);fresh(fb);
 assert(ui_keyboard_press(610,930,560,0));repaint(false);
 for(int i=0;i<3;i++){assert(ui_keyboard_hold_tick(true,610,930,500+i*120)==UI_KEYBOARD_CHANGED);repaint(false);}
 assert(ui_keyboard_release());repaint(false);assert(!strcmp(value,"ab"));
 begin(false);strcpy(s_raw,"744");refresh();fresh(fb);
 const char *choices[4];bool more;syllables(choices,&more);
 if(more){key(50,930);repaint(false);assert(s_syllable_offset==3);}
 begin(false);s_nine=false;strcpy(s_raw,"shi");refresh();fresh(fb);
 for(int i=0;i<9;i++){assert(ui_keyboard_page(1));repaint(false);}
 for(int i=0;i<9;i++){assert(ui_keyboard_page(-1));repaint(false);}
 assert(!s_page&&!s_window);
 // 只移动光标不应重画键盘；候选翻页应只有词栏。/ Caret movement leaves keys alone; candidate paging paints the word strip only.
 assert(ui_text_input_tap(&edit,field,28,false,NULL,550,285));
 ui_keyboard_update_t update=ui_keyboard_update(fb,560,field,28,false,NULL,true);
 assert(update.field&&!update.layout&&update.area.y==field.y&&update.area.height==field.height);
 // 候选变化累计后暂停才整理；不触发光标、普通英文或未松手的连打刷新。
 // Accumulated candidate changes settle only after a pause, never on caret moves, English input or held touches.
 begin(false);const char *burst="644264";
 for(int i=0;i<5;i++){int n=burst[i]-'1';key(120+n%3*150,742+n/3*78);assert(!last_update.settle);}
 assert(ui_keyboard_idle_tick(false,0)==UI_KEYBOARD_NONE);
 assert(ui_keyboard_idle_tick(false,10000)==UI_KEYBOARD_NONE);
 int n=burst[5]-'1';key(120+n%3*150,742+n/3*78);assert(!last_update.settle);
 assert(ui_keyboard_idle_tick(true,10000)==UI_KEYBOARD_NONE);
 assert(ui_keyboard_idle_tick(false,10001)==UI_KEYBOARD_NONE);
 assert(ui_keyboard_idle_tick(false,10700)==UI_KEYBOARD_NONE);
 assert(ui_keyboard_idle_tick(false,10701)==UI_KEYBOARD_CHANGED);repaint(false);
 assert(last_update.settle&&settles==1);
 for(int i=0;i<100;i++)assert(ui_keyboard_idle_tick(false,11000+i*1000)==UI_KEYBOARD_NONE);
 // 候选翻页也计数；触摸、时间回退和新布局取消旧停顿，不补刷多次。
 // Paging counts too; touches, clock rollback and layout switches cancel stale pauses without catch-up.
 begin(false);s_nine=false;strcpy(s_raw,"shi");refresh();fresh(fb);
 for(int i=0;i<6;i++){assert(ui_keyboard_page(1));repaint(false);assert(!last_update.settle);}
 assert(ui_keyboard_idle_tick(false,100)==UI_KEYBOARD_NONE);
 assert(ui_keyboard_idle_tick(true,799)==UI_KEYBOARD_NONE);
 assert(ui_keyboard_idle_tick(false,800)==UI_KEYBOARD_NONE);
 assert(ui_keyboard_idle_tick(false,1499)==UI_KEYBOARD_NONE);
 assert(ui_keyboard_idle_tick(false,500)==UI_KEYBOARD_NONE);
 assert(ui_keyboard_idle_tick(false,1199)==UI_KEYBOARD_NONE);
 assert(ui_keyboard_idle_tick(false,1200)==UI_KEYBOARD_CHANGED);repaint(false);assert(last_update.settle&&settles==2);
 for(int i=0;i<6;i++){assert(ui_keyboard_page(-1));repaint(false);}
 assert(ui_keyboard_press(26,742,560,0));repaint(false);
 assert(ui_keyboard_idle_tick(false,4000)==UI_KEYBOARD_NONE);
 assert(ui_keyboard_release());repaint(false);
 assert(ui_keyboard_idle_tick(false,4100)==UI_KEYBOARD_NONE);
 key(50,900);assert(last_update.layout);
 assert(ui_keyboard_idle_tick(false,10000)==UI_KEYBOARD_NONE);
 begin(true);for(int i=0;i<20;i++)key(26,742);
 assert(ui_keyboard_idle_tick(false,0)==UI_KEYBOARD_NONE&&ui_keyboard_idle_tick(false,10000)==UI_KEYBOARD_NONE);
 begin(false);s_nine=false;strcpy(s_raw,"shi");refresh();fresh(fb);
 for(int i=0;i<6;i++){assert(ui_keyboard_page(1));repaint(false);}
 assert(ui_keyboard_idle_tick(false,0)==UI_KEYBOARD_NONE&&ui_keyboard_idle_tick(false,700)==UI_KEYBOARD_CHANGED);
 // 输入框与整理合并时不得把整个联合区域当作全像素词栏。
 // A coalesced field repaint must never upgrade the entire union to a full-pixel word-strip settle.
 repaint(true);assert(!last_update.settle&&last_update.field);
 assert(ui_keyboard_idle_tick(false,800)==UI_KEYBOARD_NONE&&ui_keyboard_idle_tick(false,1500)==UI_KEYBOARD_CHANGED);
 repaint(false);assert(last_update.settle&&settles==3);
 ui_keyboard_end();ui_ime_release();free(fb);free(reference);free(previous);
 assert(ui_keyboard_idle_tick(false,100000)==UI_KEYBOARD_NONE);
 printf("keyboard incremental paint: PASS (%u updates, %u layout switches, %u bounded idle settles; pixel-equivalent, no stale keys/candidates)\n",updates,layouts,settles);
}
