/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实边缘手势的阈值、取消和释放归属检查。/ English: Real edge-gesture thresholds, cancellation and release ownership.
 */
#include "ui_quick_menu.h"
#include <assert.h>
#include <stdio.h>
int ui_key_hit_test(uint16_t x,uint16_t y){(void)x;return y>1216?0:-1;}
static ui_quick_menu_t menu;
static int64_t time_ms;
static ui_quick_action_t feed(int x,int y,int count,bool pressed,bool released,bool allowed,bool owned){
 cst836u_touch_t touch={.x=x,.y=y,.count=count,.touched=count!=0};
 app_ctx_t ctx={.touch=&touch,.now_ms=time_ms,.pressed=pressed,.released=released};
 ui_quick_action_t action;
 assert(ui_quick_menu_feed(&menu,&ctx,allowed,&action)==owned);return action;
}
static void open(void){ui_quick_menu_reset(&menu);time_ms=0;
 assert(feed(300,32,1,true,false,true,true)==UI_QUICK_NONE);
 time_ms=1500;assert(feed(300,104,1,false,false,true,true)==UI_QUICK_OPEN&&menu.open);
 assert(feed(300,104,1,false,false,true,true)==UI_QUICK_NONE);
 assert(feed(300,104,0,false,true,true,true)==UI_QUICK_NONE);
}
int main(void){
 ui_quick_menu_reset(&menu);assert(feed(300,33,1,true,false,true,false)==UI_QUICK_NONE);
 open();assert(feed(300,500,1,true,false,true,true)==UI_QUICK_NONE);
 assert(feed(300,500,0,false,true,true,true)==UI_QUICK_CLOSE&&!menu.open);
 assert(feed(300,500,0,false,false,true,false)==UI_QUICK_NONE);
 ui_quick_menu_reset(&menu);time_ms=0;feed(300,20,1,true,false,true,true);time_ms=1501;
 assert(feed(300,92,1,false,false,true,true)==UI_QUICK_NONE&&!menu.open);feed(300,92,0,false,true,true,true);
 ui_quick_menu_reset(&menu);time_ms=0;feed(300,20,1,true,false,true,true);
 assert(feed(350,92,1,false,false,true,true)==UI_QUICK_NONE&&!menu.open);feed(350,92,0,false,true,true,true);
 open();feed(85,156,1,true,false,true,true);assert(feed(85,156,2,false,false,true,true)==UI_QUICK_NONE);
 assert(feed(85,156,0,false,true,true,true)==UI_QUICK_NONE&&menu.open);
 feed(85,156,1,true,false,true,true);ui_quick_menu_cancel_input(&menu);
 assert(feed(85,156,0,false,true,true,true)==UI_QUICK_NONE&&menu.open);
 feed(85,156,1,true,false,true,true);assert(feed(150,156,1,false,false,true,true)==UI_QUICK_NONE);
 assert(feed(150,156,0,false,true,true,true)==UI_QUICK_NONE);
 feed(85,156,1,true,false,true,true);assert(feed(85,156,0,false,true,true,true)==UI_QUICK_WIFI);
 feed(300,220,1,true,false,true,true);assert(feed(300,148,1,false,false,true,true)==UI_QUICK_CLOSE);
 assert(feed(300,148,0,false,true,true,true)==UI_QUICK_NONE);
 open();assert(feed(0,1250,1,true,false,true,true)==UI_QUICK_CLOSE);
 feed(0,1250,0,false,true,true,true);
 open();assert(feed(0,0,0,false,false,false,true)==UI_QUICK_CLOSE&&!menu.open);
 puts("PASS: 32/33px edge, 72px/1500ms limits, diagonal rejection, upward/outside/back dismissal, multitouch/error/drag cancellation and consumed release");
}
