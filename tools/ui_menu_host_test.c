/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实菜单行边界和复原测试。/ English: Real menu row bounds and restoration tests.
 */
#include "ui_menu.h"
#include "../main/ui/ui_kit.h"
#include <assert.h>
#include <stdio.h>
static app_desc_t items[10];
static bool selected;
static int pressed, texts, clears;
int app_count(void) {return 10;}
const app_desc_t* app_at(int i) {return i>=0&&i<10?&items[i]:NULL;}
int app_index_of(const app_desc_t*a) {return (int)(a-items);}
int epd_rotated_display_width(void) {return 684;}
void epd_fill_triangle(int a,int b,int c,int d,int e,int f,uint8_t g,uint8_t*h) {(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;}
int ui_content_width(void) {return 684-2*UI_MARGIN;}
EpdRect ui_bar_rect(int col,int cols) {return (EpdRect){UI_MARGIN+col*240,UI_BAR_TOP,480/cols,UI_BAR_H};}
bool ui_rect_hit(EpdRect r,uint16_t x,uint16_t y) {return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
void ui_clear_rect_fast(uint8_t*f,EpdRect r) {(void)f;(void)r;clears++;}
void ui_clear_page(uint8_t*f) {(void)f;}
int ui_draw_header(uint8_t*f,const char*t,const char*d) {(void)f;(void)t;(void)d;return 0;}
void ui_draw_button(uint8_t*f,EpdRect r,const char*t,bool e) {(void)f;(void)r;(void)t;(void)e;}
void ui_draw_selected_round_rect(uint8_t*f,EpdRect r,int rad) {(void)f;(void)r;(void)rad;}
void ui_draw_pressed_round_rect(uint8_t*f,EpdRect r,int rad) {(void)f;(void)r;(void)rad;pressed++;}
void ui_draw_choice_round_rect(uint8_t*f,EpdRect r,int rad,bool on) {(void)f;(void)r;(void)rad;selected=on;}
void ui_text(uint8_t*f,int x,int y,int p,const char*t,enum EpdFontFlags a,bool i) {(void)f;(void)x;(void)y;(void)p;(void)t;(void)a;(void)i;texts++;}
void ui_text_vc(uint8_t*f,int x,int y,int p,const char*t,enum EpdFontFlags a,bool i) {ui_text(f,x,y,p,t,a,i);}
int main(void) {
    EpdRect r;
    assert(ui_menu_row_rect(1,1,&r));
    assert(ui_menu_hit_test(r.x,r.y,1)==9);
    assert(!ui_menu_row_rect(1,2,&r));
    assert(!ui_menu_row_rect(-1,0,&r));
    assert(!ui_menu_row_rect(2,0,&r));
    assert(!ui_menu_row_rect(0,-1,&r));
    assert(!ui_menu_row_rect(0,0,NULL));
    ui_draw_menu_row_pressed(NULL,&items[9],1,1,true);
    assert(pressed==1&&texts==3&&clears==1);
    ui_draw_menu_row_pressed(NULL,&items[9],1,1,false);
    assert(selected&&texts==6&&clears==2);
    ui_draw_menu_row_pressed(NULL,&items[8],1,1,false);
    assert(!selected);
    ui_draw_menu_row_pressed(NULL,&items[8],1,2,true);
    assert(pressed==1);
    puts("ui_menu: local row bounds and current selection restoration passed");
}
