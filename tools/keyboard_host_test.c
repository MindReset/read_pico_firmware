/* SPDX-License-Identifier: Apache-2.0
 * 中文：实际键盘与离线组词回归，覆盖词组、跨候选页、模式切换、密码和中途光标。
 * English: Real keyboard/IME regressions for phrases, paging, modes, passwords and mid-text carets.
 */
#include "../main/ui/ui_keyboard.c"
#include <assert.h>
#include <time.h>
#include "../main/ui/ui_ime_phrases.h"
int html_test_fail_after = -1;
static void draw_rect(EpdRect r) { assert(r.x >= 0 && r.y >= 0 && r.width > 0 && r.height > 0 && r.x + r.width <= 684 && r.y + r.height <= 1102); }
void ui_fill_round_rect(uint8_t *fb,EpdRect r,int radius,uint8_t gray){(void)fb;(void)radius;(void)gray;draw_rect(r);}
void ui_draw_round_rect(uint8_t *fb,EpdRect r,int radius,uint8_t gray){(void)fb;(void)radius;(void)gray;draw_rect(r);}
void epd_draw_line(int a,int b,int c,int d,uint8_t gray,uint8_t *fb){(void)gray;(void)fb;assert(a>=0&&c>=0&&a<684&&c<684&&b>=0&&d>=0&&b<1102&&d<1102);}
void ui_text_fixed_vc(uint8_t *fb,int x,int y,int px,const char *text,enum EpdFontFlags align,bool inverse){(void)fb;(void)align;(void)inverse;assert(x>=0&&x<=684&&y>=0&&y<1102&&px>0);assert(text);if(!s_panel&&y>=740&&y<964)assert(!strpbrk(text,"0123456789"));}
int ui_text_fixed_width_px(int px,const char *text){int n=0;for(const unsigned char *p=(const unsigned char *)text;*p;++p)if((*p&192)!=128)n+=*p>=128?px:px/2;return n;}
void ui_text_input_composition(uint8_t *fb,const char *text,EpdRect r,int px){(void)fb;(void)text;(void)px;draw_rect(r);}
bool ui_rect_hit(EpdRect r,uint16_t x,uint16_t y){return x>=r.x&&y>=r.y&&x<r.x+r.width&&y<r.y+r.height;}
void epd_fill_rect(EpdRect r,uint8_t gray,uint8_t *fb){(void)gray;(void)fb;draw_rect(r);}
void ui_clear_rect_fast(uint8_t *fb,EpdRect r){(void)fb;draw_rect(r);}
EpdRect ui_rect_union(EpdRect a,EpdRect b){int x=a.x<b.x?a.x:b.x,y=a.y<b.y?a.y:b.y;int r=a.x+a.width>b.x+b.width?a.x+a.width:b.x+b.width,d=a.y+a.height>b.y+b.height?a.y+a.height:b.y+b.height;return(EpdRect){x,y,r-x,d-y};}
void ui_text_input_draw(uint8_t *fb,const ui_text_edit_t *edit,EpdRect r,int px,bool masked,const char *suffix){(void)fb;(void)edit;(void)px;(void)masked;(void)suffix;draw_rect(r);}
static void type(const char *roman) { s_nine=false; for(;*roman;++roman)letter_key(*roman); }
static size_t find(const char *text) {for(size_t i=0;i<s_candidates->count;++i)if(!strcmp(s_candidates->items[i].text,text))return i;assert(!"missing candidate");return 0;}
static void t9(const char *digits) {for(;*digits;++digits){int key=*digits-'1';assert(key>=1&&key<=8);ui_keyboard_tap(120+(key%3)*150,742+(key/3)*78,560,0);}}
static void no_visible_digits(void) {char shown[UI_IME_RAW_MAX*7+1];composition_text(shown);assert(!strpbrk(shown,"0123456789"));ui_keyboard_draw(NULL,560);for(size_t i=0;i<s_candidates->count;i++)assert(!strpbrk(s_candidates->items[i].text,"0123456789"));}
int main(void) {
    char text[96]="";ui_text_edit_t edit;ui_text_edit_init(&edit,text,sizeof(text));
    ui_keyboard_begin(&edit,false); type("nihao"); assert(ui_keyboard_pending());commit(find("你好"));assert(!strcmp(text,"你好"));
    ui_keyboard_end();text[0]=0;ui_text_edit_init(&edit,text,sizeof(text));ui_keyboard_begin(&edit,false);
    type("woxiangdushu");commit(find("我想读书"));assert(!strcmp(text,"我想读书"));
    ui_keyboard_begin(&edit,false);type("nihaozzz");assert(s_candidates->count);size_t index=find("你好");assert(s_candidates->items[index].consume==5);commit(index);assert(!strcmp(s_raw,"zzz"));assert(ui_keyboard_pending());
    char before[96];strcpy(before,text);ui_keyboard_tap(560,1000,560,0);assert(!strcmp(text,before));
    ui_keyboard_begin(&edit,false);type("wo");assert(ui_keyboard_page(1));while(ui_keyboard_page(-1)){}assert(!s_page);
    for(int nine=0;nine<2;++nine)for(int panel=0;panel<4;++panel){s_nine=nine;s_panel=panel;ui_keyboard_draw(NULL,560);}
    text[0]=0;ui_text_edit_init(&edit,text,sizeof(text));ui_keyboard_begin(&edit,false);char digits[49];assert(ui_ime_digits("woxiangdushu",digits,sizeof(digits)));
    strcpy(s_raw,digits);refresh();commit(find("我想读书"));assert(!strcmp(text,"我想读书"));
    ui_keyboard_begin(&edit,false);s_nine=false;strcpy(s_raw,"wo");refresh();commit(find("我"));find("想");
    ui_keyboard_begin(&edit,false);strcpy(s_raw,"64426");refresh();assert(!strcmp(s_candidates->items[find("你好")].roman,"nihao"));ui_keyboard_tap(220,580,560,0);assert(!s_nine&&!strcmp(s_raw,"nihao"));
    // 九键内部数字不显示、不提交；切英文只能进入全键盘，密码不能切九宫格。
    // Internal T9 codes never display or commit; English uses QWERTY and passwords cannot select T9.
    text[0]=0;ui_text_edit_init(&edit,text,sizeof(text));ui_keyboard_begin(&edit,false);
    assert(ui_keyboard_tap(480,1000,560,0)==UI_KEYBOARD_NONE&&s_nine&&s_chinese);
    ui_keyboard_tap(120,742,560,0);assert(!text[0]&&!s_raw[0]);
    for(const char *p="64426";*p;p++){char key[2]={*p,0};t9(key);no_visible_digits();assert(!text[0]);}
    assert(!strcmp(s_candidates->items[0].text,"你好"));ui_keyboard_tap(120,742,560,0);assert(!strcmp(text,"你好")&&!s_raw[0]);
    t9("999999999");no_visible_digits();char unresolved[49];strcpy(unresolved,s_raw);
    ui_keyboard_tap(220,580,560,0);assert(s_nine&&!strcmp(s_raw,unresolved));
    ui_keyboard_tap(580,820,560,0);assert(!s_raw[0]&&!strcmp(text,"你好"));
    ui_keyboard_tap(220,580,560,0);ui_keyboard_tap(480,1000,560,0);assert(!s_nine&&!s_chinese);
    ui_keyboard_tap(26,742,560,0);assert(!strcmp(text,"你好q"));no_visible_digits();
    ui_keyboard_tap(30,580,560,0);assert(s_nine&&s_chinese&&!s_upper);t9("64426");no_visible_digits();
    ui_keyboard_tap(580,820,560,0);ui_keyboard_tap(30,1000,560,0);assert(s_panel==1);
    ui_keyboard_tap(26,742,560,0);assert(!strcmp(text,"你好q1"));
    ui_keyboard_tap(30,580,560,0);assert(!s_panel&&s_nine&&s_chinese);no_visible_digits();
    strcpy(s_raw,"999999999999999999999999999999999999999999999999");refresh();no_visible_digits();
    s_candidates->count=0;no_visible_digits();ui_keyboard_tap(590,900,560,0);assert(!text[0]&&!s_raw[0]);
    ui_keyboard_begin(&edit,true);ui_keyboard_tap(30,580,560,0);assert(!s_nine&&!s_chinese);
    // 长按先删未提交拼音，再按 UTF-8 字符删正文；延迟不补发，松手和中断即时停止。
    // Holds delete composition first, then UTF-8 characters; no backlog, and release/interruption stops immediately.
    strcpy(text,"你好世界");ui_text_edit_init(&edit,text,sizeof(text));ui_keyboard_begin(&edit,false);t9("64426");
    ui_keyboard_hold_start(590,770,560,1000);
    assert(ui_keyboard_hold_tick(true,590,770,1499)==UI_KEYBOARD_NONE&&!strcmp(s_raw,"64426"));
    assert(ui_keyboard_hold_tick(true,590,770,1500)==UI_KEYBOARD_CHANGED&&!strcmp(s_raw,"6442"));
    assert(ui_keyboard_hold_tick(true,590,770,1619)==UI_KEYBOARD_NONE);
    assert(ui_keyboard_hold_tick(true,590,770,1620)==UI_KEYBOARD_CHANGED&&!strcmp(s_raw,"644"));
    assert(ui_keyboard_hold_tick(true,590,770,9000)==UI_KEYBOARD_CHANGED&&!strcmp(s_raw,"64"));
    assert(ui_keyboard_hold_tick(true,590,770,9000)==UI_KEYBOARD_NONE);
    assert(ui_keyboard_hold_tick(false,590,770,9120)==UI_KEYBOARD_NONE);
    assert(ui_keyboard_hold_tick(true,590,770,9999)==UI_KEYBOARD_NONE&&!strcmp(s_raw,"64"));
    ui_keyboard_hold_start(590,770,560,10000);
    for(int i=0;i<6;i++)assert(ui_keyboard_hold_tick(true,590,770,10500+i*120)==UI_KEYBOARD_CHANGED);
    assert(!s_raw[0]&&!text[0]&&!edit.cursor);
    assert(ui_keyboard_hold_tick(true,590,770,11300)==UI_KEYBOARD_NONE&&!s_delete_hold);
    strcpy(text,"你好世界");ui_text_edit_init(&edit,text,sizeof(text));ui_text_edit_place(&edit,6);ui_keyboard_begin(&edit,true);
    ui_keyboard_hold_start(610,930,560,0);ui_keyboard_hold_tick(true,610,930,500);assert(!strcmp(text,"你世界"));
    ui_keyboard_hold_tick(true,610,930,620);assert(!strcmp(text,"世界")&&!edit.cursor);
    ui_keyboard_hold_tick(true,610,930,740);assert(!strcmp(text,"世界")&&!s_delete_hold);
    for(int panel=0;panel<4;panel++){
        strcpy(text,"abcdef");ui_text_edit_init(&edit,text,sizeof(text));ui_keyboard_begin(&edit,true);s_panel=panel;
        ui_keyboard_hold_start(610,930,560,1000);ui_keyboard_hold_tick(true,610,930,1500);assert(!strcmp(text,"abcde"));
        assert(ui_keyboard_hold_tick(true,500,930,1620)==UI_KEYBOARD_NONE&&!s_delete_hold);
        ui_keyboard_hold_tick(true,610,930,2000);assert(!strcmp(text,"abcde"));
        ui_keyboard_hold_start(610,930,560,2000);ui_keyboard_hold_cancel();ui_keyboard_hold_tick(true,610,930,2600);assert(!strcmp(text,"abcde"));
        ui_keyboard_hold_start(610,930,560,3000);ui_keyboard_hold_tick(true,610,930,2999);assert(!s_delete_hold&&!strcmp(text,"abcde"));
        ui_keyboard_hold_start(26,742,560,3000);ui_keyboard_hold_tick(true,26,742,3600);assert(!s_delete_hold&&!strcmp(text,"abcde"));
        ui_keyboard_hold_start(610,930,560,4000);ui_keyboard_end();ui_keyboard_begin(&edit,true);ui_keyboard_hold_tick(true,610,930,4500);assert(!strcmp(text,"abcde"));
    }
    // 插入失败必须保留全部拼音和原文。/ Failed insertion must preserve composition and original text.
    char small[4]="A";ui_text_edit_init(&edit,small,sizeof(small));ui_keyboard_begin(&edit,false);type("nihao");assert(!commit(find("你好")));assert(!strcmp(s_raw,"nihao")&&!strcmp(small,"A"));
    strcpy(text,"你好世界");ui_text_edit_init(&edit,text,sizeof(text));ui_text_edit_place(&edit,3);ui_keyboard_begin(&edit,true);letter_key('a');assert(!strcmp(text,"你a好世界"));backspace();assert(!strcmp(text,"你好世界"));
    ui_keyboard_tap(560,1030,560,10);assert(!s_chinese);s_upper=true;letter_key('b');assert(!strcmp(text,"你B好世界"));
    s_panel=3;ui_keyboard_tap(36,750,560,20);ui_keyboard_tap(100,750,560,30);assert(strstr(text,"B`~"));
    // 密码须能输入全部可打印 ASCII，不因重输/退格覆盖符号而丢键。
    // Every printable ASCII password character remains reachable despite utility keys.
    bool reachable[128]={0};
    for(int panel=0;panel<4;panel++)for(int row=0;row<3;row++)for(int col=0;col<10;col++){
        text[0]=0;ui_text_edit_init(&edit,text,sizeof(text));ui_keyboard_begin(&edit,true);s_panel=panel;
        s_upper=false;ui_keyboard_tap(24+col*64+2,560+180+row*78+2,560,0);
        for(char *p=text;*p;p++)reachable[(unsigned char)*p]=true;
        text[0]=0;ui_text_edit_init(&edit,text,sizeof(text));ui_keyboard_begin(&edit,true);s_panel=panel;s_upper=true;
        ui_keyboard_tap(24+col*64+2,560+180+row*78+2,560,0);
        for(char *p=text;*p;p++)reachable[(unsigned char)*p]=true;
    }
    reachable[' ']=true;for(int c=32;c<127;c++)assert(reachable[c]);
    ui_keyboard_end();assert(!s_edit&&!s_candidates&&!s_raw[0]);
    html_test_fail_after=0;ui_keyboard_begin(&edit,false);assert(!s_chinese&&!s_nine&&!s_candidates);ui_keyboard_tap(30,580,560,0);assert(!s_nine);letter_key('c');html_test_fail_after=-1;ui_keyboard_end();
    ui_ime_result_t *out=malloc(sizeof(*out));assert(out);assert(!ui_ime_candidates("a!",false,"","",out));assert(!ui_ime_candidates("01",true,"","",out));
    char large[50];memset(large,'a',49);large[49]=0;assert(!ui_ime_candidates(large,false,"","",out));free(out);ui_ime_release();
    // 核对词组读音，跨批次候选不重复，超过八个同音字可选且保留剩余拼音。
    // Validate phrase readings, unique multi-batch homophones, and selection beyond eight without losing the suffix.
    for(size_t i=0;i<sizeof(phrases)/sizeof(phrases[0]);i++) {
        if(!read_pico_search_match(phrases[i].text,phrases[i].roman)) {
            fprintf(stderr,"wrong phrase reading: %s %s\n",phrases[i].roman,phrases[i].text);abort();
        }
    }
    out=malloc(sizeof(*out));assert(out);
    uint32_t all[2048];size_t total=read_pico_search_candidates("shi",all,2048,0);assert(total>24);
    bool seen[65536]={0};size_t offset=0,unique=0;
    do {
        assert(ui_ime_candidates_page("shi",false,"","",offset,out));
        assert(out->count&&out->count<=UI_IME_CANDIDATES);
        for(size_t i=0;i<out->count;i++)if(out->items[i].consume==3) {
            const unsigned char *t=(const unsigned char*)out->items[i].text;
            assert(strlen((const char*)t)==3);uint32_t cp=((t[0]&15)<<12)|((t[1]&63)<<6)|(t[2]&63);
            assert(!seen[cp]);seen[cp]=true;unique++;
        }
        offset+=out->count;
    }while(out->has_more);
    assert(unique==total);for(size_t i=0;i<total;i++)assert(seen[all[i]]);free(out);ui_ime_release();
    text[0]=0;ui_text_edit_init(&edit,text,sizeof(text));ui_keyboard_begin(&edit,false);type("shizzz");
    for(int i=0;i<8;i++)assert(ui_keyboard_page(1));
    assert(s_window>=24);
    size_t selection=s_page;char chosen[UI_IME_TEXT_MAX];strcpy(chosen,s_candidates->items[selection].text);
    assert(commit(selection));assert(!strcmp(s_raw,"zzz")&&!strcmp(text,chosen));
    ui_keyboard_end();
    puts("keyboard/IME: PASS (T9, QWERTY, phrases, continuations, pending input, bounds, OOM, passwords and carets)");
}
