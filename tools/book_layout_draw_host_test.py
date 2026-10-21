#!/usr/bin/env python3
"""中文：按 PR9 原始实现，缺失插图记录不阻断后续正文。
English: Missing image records must not abort remaining prose, matching PR9.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'main/book/book_layout.c').read_text()
start = source.index('void book_layout_draw_page(')
end = len(source)
unit = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* r93 行缓冲压力钩子：stub 记录最大单次分配，测试场景断言内存上限。
   r93 line-buffer pressure hook: the stub records the largest single allocation
   so scenarios can assert memory bounds. */
size_t test_heap_largest;

#include <limits.h>
#include "book_layout.h"

/* r93 分片预绘入口随抽取区带入（draw_page 之后定义）：桩化其两个内部依赖——
   空闲绘制指针恒空、代次恒 1，draw_slice 在宿主上退化为一次完整 draw_page。
   本用例断言的是 draw_page 行为，draw_slice 只是同块薄包装。声明须在
   book_layout.h 之后（cursor 类型来自该头）。
   r93's slice-prefetch entry rides along after draw_page in the extraction region:
   stub its two internal dependencies -- idle cursor always null, generation fixed --
   so draw_slice degenerates to a full draw_page on the host. Declared after
   book_layout.h because the cursor type comes from it. */
static book_layout_draw_cursor_t *s_idle_draw;
static uint32_t s_generation = 1;
static const char *s_text="IMG\ntail";
static size_t s_count=1,s_len=8,s_pages[]={0};
static int s_px=10,s_lead_height,s_reading_line,s_reading_line_offset,s_tracking_px;
static EpdRect s_rect={0,0,100,100};
static char s_line[16];
static uint32_t image_starts[]={0,1};
static uint32_t *s_page_img_start=image_starts;
typedef struct {int image,y,width,height;} layout_image_t;
static layout_image_t s_images[]={{0,10,80,20}};
static const blk_t block={.offset=0,.len=3,.image=0};
static int painted,baseline;
static size_t skip_image_spacing(size_t off){return off;}
static const blk_t *block_at(size_t off){return off==0?&block:NULL;}
int book_layout_page_image(size_t page){(void)page;return -1;}
int book_layout_page_image_count(size_t page){(void)page;return s_page_img_start[1]-s_page_img_start[0];}
/* r92 给 take_line 加了两参（advance_limit / hang_margin）：横排调用传正文宽与左边距，
   本用例的替身按同样位置接住（横排场景这两个值不影响断行断言）。/ r92 added two params
   (advance_limit, hang_margin) to take_line; the stand-in accepts them in the same slots. */
static bool take_line(size_t off,int advance_limit,int hang_margin,size_t *next,bool *end,int *px,bool *heading,int *width,int *indent,uint8_t *align,int *before,int *after){
 (void)advance_limit;(void)hang_margin;assert(off==4);strcpy(s_line,"tail");*next=8;*end=true;*px=10;*heading=false;*width=40;*indent=0;*align=0;*before=*after=0;return true;
}
/* 竖排开关与竖排绘制：r92 的 draw_page 会先看 s_vertical；单栏/横排场景恒为关，
   一旦被走到就 assert，守卫不许静默放行。/ r92's draw_page checks s_vertical first;
   single-column and horizontal scenes never set it, and hitting it asserts. */
/* run 制字体选择：绘制路径按 run 选槽并在画完后把游标还给系统 face；宿主无字形可查，
   桩只记住当前槽（场景恒为系统槽，不改变绘制调用流）。/ Run-based face selection:
   the stub only remembers the slot (always the system slot here). */
/* run / 字体槽的最小自声明：本用例不查字形，只按位置接住 r92 绘制路径的调用
   （不引入 ttf_font.h，免与单元自带的绘制桩重复声明）。/ Minimal run/slot declarations:
   no glyph lookup here, just absorb r92's draw-path calls. */
typedef struct { int slot; size_t start, end; } ttf_run_t;
#define TTF_FONT_SLOT_SYSTEM 0
static ttf_run_t s_line_runs[8];
static size_t s_line_run_count;
static void ttf_draw_set_runs(const ttf_run_t* runs,size_t count){(void)runs;s_line_run_count=count;}
static int s_ttf_slot=TTF_FONT_SLOT_SYSTEM;
static int ttf_font_select(int slot){s_ttf_slot=slot;return 0;}
/* 字体槽切换点：按 offset 决定用哪个槽，宿主无字形，空实现顶掉。 */
static void slot_select_for(size_t off){(void)off;}
static bool s_vertical;
static void draw_vertical_page(uint8_t *fb,size_t page,EpdRect rect){(void)fb;(void)page;(void)rect;assert(0);}
static int line_height_for(int px){return px;}
static int gap_for(int height,bool heading){(void)height;(void)heading;return 0;}
/* 共享排版算术的替身：draw_page 现在调用这组内联函数（与命中测试同一份），这里按同样规则复刻，
   否则抽出来的 draw_page 编译不过。/ Stubs for the shared layout arithmetic: draw_page now calls
   these inline helpers (the same ones the hit test uses), so they are mirrored here or the
   extracted draw_page will not compile. */
static int64_t advance_after_image(const layout_image_t* rec,int px){return (int64_t)rec->y+rec->height+px/2;}
static int line_leading(int64_t used,int margin_before){return used?margin_before:0;}
static bool line_fits(int64_t used,int leading,int line_height,int page_height){return used+leading+line_height>page_height?false:true;}
static int line_origin_x(int rect_x,int indent,int available,int line_width,uint8_t align){
 if(align==1)return rect_x+indent+(available-line_width)/2;
 if(align==2)return rect_x+indent+(available-line_width);
 return rect_x+indent;
}
static bool line_is_final(bool paragraph_end,size_t next,const blk_t* line_block){
 return paragraph_end||next>=s_len||(line_block&&next>=line_block->offset+line_block->len);
}
static bool line_is_justified(bool heading,uint8_t align,bool final_line,int line_width,int available){
 return !heading&&!align&&(!final_line||line_width>available);
}
static int justify_target_px(int line_width,int available,int rect_x,int line_px){
 int hang=rect_x>4?line_px/3:0;if(hang>rect_x-4)hang=rect_x-4;
 return line_width>available?available+hang:available;
}
static int64_t advance_after_line(int64_t used,int line_height,bool paragraph_end,bool heading,int margin_after){
 used+=line_height;if(paragraph_end)used+=gap_for(line_height,heading)+margin_after;return used;
}
static int ttf_ascender_px(int px){return px;}
int test_guide_segments,test_guide_first_y,test_guide_height;
uint8_t test_guide_gray;
static void ttf_draw_text_px(uint8_t *fb,int x,int y,int px,const char *text,int align,int fg,int bg){
 (void)fb;(void)x;(void)px;(void)align;(void)fg;(void)bg;assert(!strcmp(text,"tail"));++painted;baseline=y;
}
static void ttf_draw_text_px_spaced(uint8_t *fb,int x,int y,int px,const char *text,int tracking,int fg,int bg){(void)tracking;ttf_draw_text_px(fb,x,y,px,text,0,fg,bg);}
static void ttf_draw_text_px_fitted(uint8_t *fb,int x,int y,int px,const char *text,int tracking,int target,int fg,int bg){(void)target;ttf_draw_text_px_spaced(fb,x,y,px,text,tracking,fg,bg);}
'''+source[start:end]+r'''
/* 桩记录全局（r95 rebase 补）：本测试经由 epdiy.h 的 epd_fill_rect 桩记录绘制，但上游
   r94/r95（PR34）改动竖排辅助线后，book_layout.c 的竖排列路径也会写入这些符号，链接期
   必须提供全套定义（此前只定义 3 个 → undefined reference）。
   Stub recording globals (added on the r95 rebase): upstream r94/r95 (PR34) changed the
   vertical guide, so book_layout.c's vertical-column path also writes through the
   epd_fill_rect stub; all six must be defined at link time. */
int test_guide_segments;
int test_guide_first_x;
int test_guide_first_y;
int test_guide_second_y;
int test_guide_last_y;
int test_guide_height;
uint8_t test_guide_gray;

int main(void){
 uint8_t fb=0;
 book_layout_draw_page(&fb,0,s_rect,10);assert(painted==1&&baseline==45);
 painted=0;s_page_img_start[1]=0;
 book_layout_draw_page(&fb,0,s_rect,10);assert(painted==1&&baseline==10);
 painted=0;s_page_img_start[1]=1;s_images[0].image=7;
 book_layout_draw_page(&fb,0,s_rect,10);assert(painted==1&&baseline==10);
 puts("PASS: PR9 missing/mismatched image slots skip the unmatched record and continue prose");
}
'''
with tempfile.TemporaryDirectory(prefix='book-image-record-') as directory:
    work=Path(directory)
    (work/'test.c').write_text(unit)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    '-I'+str(root/'tools/book_layout_stubs'),'-I'+str(root/'tools/book_source_host_stubs'),'-I'+str(root/'main/book'),
                    str(work/'test.c'),'-o',str(work/'test')],check=True)
    subprocess.run([str(work/'test')],check=True)
