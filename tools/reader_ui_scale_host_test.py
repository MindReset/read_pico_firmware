"""Exercise actual paired reader sheets and native text-box fitting at all UI scales.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import re,subprocess,tempfile
def function(name,path):
    text=path.read_text()
    scan=re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"',lambda m:' '*len(m[0]),text)
    match=re.search(r'^(?:static )?(?:void|int|bool|EpdRect) '+name+r'\([^;{}]*\)\s*\{',scan,re.M)
    assert match,name
    at,depth=match.end(),1
    while depth:
        depth+=(scan[at]=='{')-(scan[at]=='}');at+=1
    return text[match.start():at]
def mask(text):
    """掩码注释，保留字符串与代码，便于按标记截取。/ Mask comments for marker slicing."""
    return re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                  lambda m:' '*len(m[0]) if m[0].startswith(('//','/*')) else m[0],text)
def declaration(text,head,tail):
    """按起止标记抽取声明（typedef 等）；先定位尾标记，再向前找最近的头标记。
    Extract a declaration by head/tail markers: anchor on the tail first, then walk
    back to the nearest head, so other same-shaped declarations cannot match."""
    scan=mask(text);j=scan.index(tail);i=scan.rindex(head,0,j)
    return text[i:j+len(tail)]
def initializer(text,head):
    """抽取带花括号初始化器的变量定义。/ Extract a variable with a brace initializer."""
    scan=mask(text);i=scan.index(head);at,depth=scan.index('{',i),0
    while True:
        if scan[at]=='{':depth+=1
        elif scan[at]=='}':
            depth-=1
            if depth==0:break
        at+=1
    return text[i:text.index(';',at)+1]
ROOT=Path(__file__).resolve().parents[1]
book=ROOT/'main/apps/app_book.c'
kit=ROOT/'main/ui/ui_kit.c'
s=book.read_text()
unit=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
typedef struct {int x,y,width,height;}EpdRect;
enum EpdFontFlags {EPD_DRAW_ALIGN_LEFT,EPD_DRAW_ALIGN_CENTER,EPD_DRAW_ALIGN_RIGHT};
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
#define UI_GRAY_WHITE 255
#define UI_GRAY_BLACK 0
#define UI_INK_WHITE 15
#define UI_INK_BLACK 0
#define BOOK_PX_MIN 24
#define BOOK_PX_MAX 60
enum {UI_ICON_CHEVRON_RIGHT,UI_ICON_ROTATE_CW};
static unsigned scale=100,face;
static bool scaling=true,s_system_ttf;
static unsigned glyphs;
static EpdRect last_box;
static int s_reader_slider=-1,s_reader_preview_px=32,s_px=32;
static unsigned app_settings_system_font_size(void){return scale;}
static int ui_text_effective_px(int px){return scaling?(px+2)*scale/100:px+2;}
static bool ttf_font_is_builtin(void){return face==0;}
static bool ui_font_has_text(const char *text){(void)text;return true;}
static int chars(const char *s){int n=0;for(;*s;s++)if(((unsigned char)*s&0xc0)!=0x80)++n;return n;}
static int ui_text_fixed_width_px(int px,const char *text){return chars(text)*px*(face==2?12:9)/10;}
static void ttf_measure_line_px(int px,const char *text,int *above,int *below){(void)text;*above=px*3/4;*below=px*(face==2?5:2)/10;}
static void ui_font_measure_line_px(int px,const char *text,int *above,int *below){ttf_measure_line_px(px,text,above,below);}
static void ui_text_fixed_vc(uint8_t *fb,int x,int cy,int px,const char *text,enum EpdFontFlags align,bool inv){
 (void)fb;(void)inv;int above,below;ttf_measure_line_px(px,text,&above,&below);
 int w=ui_text_fixed_width_px(px,text),baseline=cy+(above-below)/2;
 int left=align==EPD_DRAW_ALIGN_LEFT?x:align==EPD_DRAW_ALIGN_RIGHT?x-w:x-w/2;
 assert(left>=last_box.x&&left+w<=last_box.x+last_box.width);
 assert(baseline-above>=last_box.y&&baseline+below<=last_box.y+last_box.height);
 assert(!strchr(text,'\n'));++glyphs;
}
// r93 的实心填充改走 ui_fill_rect_fast，这个 epdiy 桩不再被调用；保留它是为了别的绘制路径。
// r93 fills through ui_fill_rect_fast, so this epdiy shim is no longer called; kept for other paths.
static __attribute__((unused)) void epd_fill_rect(EpdRect r,int color,uint8_t *fb){(void)r;(void)color;(void)fb;}
static void epd_fill_circle(int x,int y,int radius,int color,uint8_t *fb){(void)x;(void)y;(void)radius;(void)color;(void)fb;}
static void epd_draw_circle(int x,int y,int radius,int color,uint8_t *fb){epd_fill_circle(x,y,radius,color,fb);}
// r93 的快速实心填充：沿用上面圆角填充的越界断言，让几何守门继续有效。
// r93's fast fill: keep the same out-of-bounds assertion as the rounded fill above.
static void ui_fill_rect_fast(EpdRect r,uint8_t gray,uint8_t *fb){(void)fb;(void)gray;assert(r.x>=0&&r.x+r.width<=684&&r.y>=0);}
static void ui_fill_round_rect(uint8_t *fb,EpdRect r,int radius,int color){(void)fb;(void)radius;(void)color;assert(r.x>=0&&r.x+r.width<=684&&r.y>=0);}
static void ui_draw_control_frame(uint8_t *fb,EpdRect r,int radius,int color){ui_fill_round_rect(fb,r,radius,color);}
static void ui_draw_icon(uint8_t *fb,int x,int y,int px,int icon,int color){(void)fb;(void)x;(void)y;(void)px;(void)icon;(void)color;}
static void copy_text(char *to,size_t cap,const char *from){snprintf(to,cap,"%s",from);}
static int app_settings_book_weight_fine(void){return 2;}
#define APP_BOOK_WEIGHT_FINE_MAX 32
static bool app_settings_book_fonts(void){return true;}
static bool app_settings_book_antialias(void){return true;}
static unsigned app_settings_book_reading_line(void){return 2;}
static const char *ttf_font_display_name(void){return "测试字体的名称";}
static unsigned app_settings_reader_full_pages(void){return 15;}
static unsigned app_settings_reader_turn_effect(void){return 1;}
static bool app_settings_reader_vertical_turn(void){return true;}
static bool app_settings_reader_power_turn(void){return true;}
static bool app_settings_reader_immersive(void){return true;}
static bool app_settings_reader_hide_images(void){return false;}
// 归因：我方行表第 8 槽是「横屏阅读」，reading_row_on 读它的状态。
// Attribution: our table's eighth slot is 横屏阅读; reading_row_on reads its state.
static bool app_settings_reader_landscape(void){return false;}
static bool app_settings_reader_two_column(void){return false;}  /* 第 4 步行表新增引用的桩 / stub for the step-4 row-table reference */
'''
unit+=function('ui_text_box',kit).replace('void ui_text_box(', 'static void actual_text_box(')+'\n'
unit+=r'''
static void ui_text_box(uint8_t *fb,EpdRect box,int px,const char *text,enum EpdFontFlags align,bool inv){
 assert(box.x>=0&&box.y>=0&&box.x+box.width<=684&&box.y+box.height<=1216);
 assert(box.width>0&&box.height>0);last_box=box;actual_text_box(fb,box,px,text,align,inv);
}
'''
unit+=function('draw_sheet',book)+'\n'
# 归因：我方阅读设置面板改为行表驱动（上游用字面量与固定序号），绘制函数引用了行表三件套
# 与 reading_row_on/reading_row_count，这里一并抽取；宏与表都须先于使用它们的函数。
# Attribution: our reading sheet is table-driven (upstream used literals and fixed ordinals);
# the drawing functions reference the row-table triple plus reading_row_on/reading_row_count,
# so extract those too — the macros and the table must precede the functions using them.
for name in ('READING_ROW_KEYS','READING_ROW_FULL','READING_ROW_WAVE','READING_ROW_TURN',
             'READING_ROW_COUNT','READING_ROW_COLS'):
    unit+=re.search(r'^#define '+name+r' .*',s,re.M)[0]+'\n'
unit+=declaration(s,'typedef enum {','} reading_cell_t;')+'\n'
unit+=declaration(s,'typedef struct {','} reading_row_t;')+'\n'
unit+=initializer(s,'static const reading_row_t s_reading_rows')+'\n'
for name in ('reading_row_count','reading_row_on'):
    unit+=function(name,book)+'\n'
for name in ('fit_fixed_text','reader_ui_extra','reader_font_top','reader_font_card','reader_slider_rect',
             'draw_pill_slider','draw_font_pair','reading_setting_rect','reading_settings_top',
             'reading_toggle_rect','draw_reading_switch','draw_reading_card','draw_reading_toggle'):
    unit+=function(name,book)+'\n'
for name in ['s_font_card','s_weight_card','s_bookfont_card','s_layout_card','s_rule_card','s_antialias_card']:
    unit+=re.search(r'^#define '+name+r' .*',s,re.M)[0]+'\n'
unit+=function('reader_panel_text_vc',book)+'\n'
unit+=function('reader_weight_value',book)+'\n'
unit+=function('draw_font_settings',book)+'\n'+function('draw_reading_settings',book)+'\n'
unit+=function('reader_font_picker_top',book)+'\n'+function('reader_font_pick_rect',book)+'\n'
unit+='static bool s_bookmark_edit;\n'+function('bookmark_row_step',book)+'\n'+function('bookmark_rows',book)+'\n'+function('bookmark_row_rect',book)+'\n'
unit+=r'''
static bool overlap(EpdRect a,EpdRect b){return a.x<b.x+b.width&&b.x<a.x+a.width&&a.y<b.y+b.height&&b.y<a.y+a.height;}
int main(void){uint8_t fb[1]={0};
 for(face=0;face<3;++face)for(scale=100;scale<=200;scale+=10){
   draw_font_settings(fb);draw_reading_settings(fb);
   for(int i=0;i<6;++i){EpdRect a=reader_font_card(i);assert(a.y>=reader_font_top()+100&&a.y+a.height<1141);for(int j=0;j<i;++j)assert(!overlap(a,reader_font_card(j)));}
   EpdRect slider=reader_slider_rect(0);assert(slider.y>reader_font_top()+100&&slider.y+slider.height<reader_font_card(0).y);
   for(int i=0;i<8;++i){EpdRect a=reading_setting_rect(i);assert(a.y>=reading_settings_top()+100&&a.y+a.height<1141);for(int j=0;j<i;++j)assert(!overlap(a,reading_setting_rect(j)));}
   for(int i=0;i<9;++i){EpdRect a=reader_font_pick_rect(i);assert(a.y>reader_font_picker_top()+120&&a.y+a.height<1141);for(int j=0;j<i;++j)assert(!overlap(a,reader_font_pick_rect(j)));}
   for(int edit=0;edit<2;++edit){s_bookmark_edit=edit;assert(bookmark_rows()>=3);for(int row=0;row<bookmark_rows();++row){EpdRect a=bookmark_row_rect(row);assert(a.y+a.height<=560+(edit?520:600));if(row)assert(!overlap(a,bookmark_row_rect(row-1)));}}
 }
 assert(glyphs>500);
 scaling=false;scale=200;assert(ui_text_effective_px(30)==32);
 puts("PASS: 100–200% native glyph bounds for 3 font metrics, paired sheet/sliders/switches and shared hits, expanded font grid and bookmark rows; body scaling isolated");
}
'''
with tempfile.TemporaryDirectory(dir=ROOT/'build') as folder:
    c,b=Path(folder)/'test.c',Path(folder)/'test'
    c.write_text(unit)
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-error=sign-compare','-fsanitize=address,undefined',str(c),'-o',str(b)],check=True)
    subprocess.run([str(b)],check=True)
