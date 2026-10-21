#!/usr/bin/env python3
"""中文：预渲染目标寻址的独立宿主测试（C1-C6）——第 3 步双栏休眠接线的寻址断言集。
English: Standalone host test for prefetch target addressing (C1-C6) — the addressing
assertion set of step 3's dormant two-column wiring.

背景 / Why: 第 3 步把 kick_prep / reader_prefetch_idle 的预渲染目标从上游的「s_page+1」
改走视觉页地图（M1-M4），并给双栏休眠接线加了 app 层切片钩子 reader_prep_slice（P1
侦查答案：draw_slice 一次只画一页一行，双栏必须在 app 层拼两次切片）。上游 139 行
pthread harness（tools/reader_idle_prefetch_host_test.py）按同红政策基座**逐字节不动**：
它的链接闭包里没有 page_map 与钩子符号，只看得到退化形态。双栏形态的寻址与缝合行为
在本测试里看守——寻址三函数（地图区块）与 prep 决策/钩子函数全部取产品真实实现，
其余（信号量/任务/计时/堆/页表/draw_slice）用确定性桩替换，无并发。

Background: step 3 routes the prefetch target of kick_prep / reader_prefetch_idle from
upstream's "s_page+1" through the visual page map (M1-M4) and adds the app-layer slice
hook reader_prep_slice for the dormant two-column wiring (P1's answer: draw_slice paints
one row of one page per call, so two columns stitch from two slices at the app layer).
The upstream 139-line pthread harness stays byte-identical under the same-red policy: its
link closure has no page_map or hook symbols and only ever sees the degenerate form. The
two-column addressing and stitch behaviour is guarded here — the three addressing
functions (the map block) and the prep decision/hook functions are the real product code;
everything else (semaphores, tasks, timers, heap, page table, draw_slice) is a
deterministic stub. No concurrency.

断言集 / Assertions (设计文档 §3 的 C1-C5 + 钩子缝合增量 C6):
  C1 两栏配对下 kick 的目标 = 下一视觉页左栏（≠ s_page+1，后者是当前视觉页的右栏）；
     数组路径（含 JOINT）与退化公式路径各测一遍，发布后 s_next_page==target。
  C2 目标视觉页覆盖的任一逻辑页（left 或 right）有图 → 拒 kick；两栏皆无图 → 放行。
  C3 退化恒等：cols==1 时 target ≡ s_page+1、右栏恒不存在、切片矩形就是联合区本身
     （与上游 harness 的 76/118 行断言同判据，分工：上游看守运行时，这里看守目标解析）。
  C4 idle 的 s_prep_page 门闸随映射同步：未达成不激活；达成后受 8 次步进预算约束；
     发布条件达成后不重复画；门闸比的是映射左栏而不是上游的 s_page+1。
  C5 视觉末页钳制：visual+1>=visual_count → 拒 kick（双栏视觉末页、落单左栏末页、
     单栏逻辑末页三种形态）。
  C6 钩子缝合（P1 新代码，上游闭包里没有它）：双栏帧 = 左栏 N 行进左栏矩形 + 右栏 N 行
     进右栏矩形；落单左栏（含 JOINT 目标）只画左栏且走左栏矩形；invalidate 后同目标
     重踢时右栏游标重开，不续画被清帧前的陈旧行。
  恒等七步链（设计文档 §2.3）的可执行部分：ID1-ID3 = 第 1-3 步（地图三函数单栏恒等）；
     ID4 = 第 4 步之 M1（kick 目标退化）；ID5 = 第 4 步之 M2（idle 门闸/守卫退化）；
     ID6 = 第 4 步之 M3（图片守卫退化）；M4 由 C5 的单栏末页断言在列。第 5 步（上游
     harness 逐字节全绿）、第 6 步（黄金三值）、第 7 步（互证开形态命名断言）由各自
     测试看守，见各测试。/ The executable part of the seven-step closed-form identity
     chain (design doc section 2.3): ID1-ID3 are steps 1-3 (the map functions'
     single-column identities); ID4 is step 4's M1 (kick target degeneration); ID5 is
     M2 (idle gate and guard degeneration); ID6 is M3 (image guard degeneration); M4 is
     held by C5's single-column last-page assertion. Steps 5 (upstream harness
     byte-identical and green), 6 (golden three values) and 7 (cross-check on-form
     assertions) stay with their own tests.

Assertion set (design doc section 3's C1-C5 plus the hook-stitch increment C6):
  C1 Under a two-column pair, kick's target is the next visual page's left column
     (!= s_page+1, which is the current visual page's right column); the array path
     (with a JOINT page) and the fallback formula path are both covered, and the
     published frame satisfies s_next_page==target.
  C2 An image on either logical page the target visual page covers (left or right)
     refuses the kick; both clean allows it.
  C3 Degenerate identity: at cols==1 the target equals s_page+1, the right side is
     absent, and the slice rect is the joint area itself (the same criterion as the
     upstream harness's lines 76/118; the split of labour: upstream guards runtime,
     this guards target resolution).
  C4 idle's s_prep_page gate maps in step: quiet before the target is reached, bounded
     by its 8-step budget once reached, silent once the publish condition holds; the
     gate compares against the mapped left column, not upstream's s_page+1.
  C5 Last visual page clamping: visual+1>=visual_count refuses the kick (two-column
     last visual page, lone-left last visual page, single-column last logical page).
  C6 Hook stitch (P1's new code, absent from upstream's closure): a two-column frame is
     N rows into the left column rect then N rows into the right column rect; a lone
     left column (including a JOINT target) paints only the left side, through the left
     column rect; a same-target re-kick after invalidate reopens the right cursor
     instead of resuming stale rows from before the cleared frame.
  The executable part of the seven-step closed-form identity chain (design doc section
     2.3): ID1-ID3 are steps 1-3 (the map functions' single-column identities); ID4 is
     step 4's M1 (kick target degeneration); ID5 is M2 (idle gate and guard
     degeneration); ID6 is M3 (image guard degeneration); M4 is held by C5's
     single-column last-page assertion. Steps 5 (upstream harness byte-identical and
     green), 6 (golden three values) and 7 (cross-check on-form assertions) stay with
     their own tests.

用法 / Usage:
    python3 tools/reader_prefetch_map_host_test.py
"""
from pathlib import Path
import subprocess
import sys
import tempfile

from home_cover_cache_host_test import function
from reader_page_map_host_test import map_block

ROOT = Path(__file__).resolve().parents[1]
BOOK = ROOT / 'main/apps/app_book.c'

# 产品真实实现：地图区块整块 + prep 决策/钩子函数 + 栏几何。寻址三函数随地图区块一
# 并抽取（与互证测试同源），钩子强定义在本 TU 在场——kick/idle/step 里的块作用域弱
# 声明因此全部取到产品路径（与产品构建同场景），这正是本测试要看守的形态。
# Real product code: the whole map block plus the prep decision/hook functions and the
# column geometry. The three addressing functions come with the map block (same source
# as the cross-check test); the hook's strong definition is present in this TU, so every
# block-scope weak declaration inside kick/idle/step takes the product path (the same
# scenario as the product build) — exactly the form this test guards.
GEOMETRY = ['reader_column_gutter', 'reader_column_width', 'reader_column_offset',
            'reader_column_rect']
# 依赖地图区块的抽取件（寻址三函数与 s_page_map_columns 均在地图区块内）：栏数读取、
# 钩子、目标解析、prep 状态机。
# Extracted pieces that need the map block (the three addressing functions and
# s_page_map_columns all live inside it): the column-count read, the hook, the target
# resolver and the prep state machine.
AFTER_MAP = ['reader_column_count', 'reader_prep_target', 'reader_prep_slice',
             'invalidate_prep', 'reader_prefetch_buffer', 'reader_prefetch_step',
             'kick_prep', 'reader_prefetch_idle']

PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

typedef struct{int x,y,width,height;}EpdRect;
/* 与 book_layout.h 的游标结构逐字段一致（桩要忠实镜像代次/完成语义）。
   Field-for-field the same as book_layout.h's cursor (the stub mirrors generation and
   completion semantics faithfully). */
typedef struct{uint32_t generation,image_at;size_t page,offset;int64_t used;
               bool started,complete,failed;}book_layout_draw_cursor_t;
typedef struct{bool touched;}touch_t;
typedef struct{bool consumed,request_app,request_menu,request_return;touch_t *touch;}app_ctx_t;
enum{READING,SHELF,READER_PANEL_NONE,READER_PANEL_FONT};
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define BOOK_PREP_RESERVE (1024u*1024u)
#define BOOK_PREP_IDLE_US 4000
#define portMAX_DELAY 999999
#define pdPASS 1

/* 日志桩 / log stub */
static const char* TAG="book";
#define ESP_LOGW(tag,fmt,...) ((void)0)

/* 信号量/任务桩：无并发——寻址断言只看 kick/idle 的纯决策，并发面由上游 harness 看守。
   Semaphore/task stubs: no concurrency — the addressing assertions watch kick/idle's
   pure decisions; the concurrency surface stays with the upstream harness. */
typedef struct{int value;}sem_t;
typedef sem_t* SemaphoreHandle_t;
typedef struct{int dummy;}task_t;
typedef task_t* TaskHandle_t;
static sem_t s_lock_stub,s_done_stub;
static task_t s_task_stub;
static SemaphoreHandle_t xSemaphoreCreateMutex(void){return &s_lock_stub;}
static SemaphoreHandle_t xSemaphoreCreateBinary(void){return &s_done_stub;}
static int xSemaphoreTake(sem_t*s,int timeout){(void)timeout;if(!s->value)return 0;--s->value;return 1;}
static void xSemaphoreGive(sem_t*s){(void)s;}
static int xTaskCreatePinnedToCore(void(*run)(void*),const char*name,int stack,void*arg,int priority,task_t**out,int core){
    (void)run;(void)name;(void)stack;(void)arg;(void)priority;(void)core;*out=&s_task_stub;return pdPASS;}
static void xTaskNotifyGive(task_t*t){(void)t;}
/* kick_prep 只取 prep_task 的地址建任务，任务体不进本测试的链接闭包。
   kick_prep only takes prep_task's address to create the task; the task body itself
   stays out of this test's link closure. */
static void prep_task(void*arg){(void)arg;}

/* 计时/堆/帧/设置桩 / timer, heap, frame and settings stubs */
static int64_t fake_us=1000000;
static int64_t esp_timer_get_time(void){return fake_us;}
static size_t heap_caps_get_free_size(int caps){(void)caps;return 4u*1024u*1024u;}
static void* heap_caps_aligned_alloc(size_t align,size_t n,int caps){(void)align;(void)caps;return malloc(n);}
static size_t fb_bytes(void){return 64;}
static int s_px=48,s_margin=40;
/* 肖像几何：684 宽、边距 40 → 版心 604 宽；gutter=MAX(48,40)=48，栏宽=(604-48)/2=278。
   Portrait geometry: 684 wide, margin 40 -> a 604-wide body; gutter=MAX(48,40)=48 and
   the column width is (604-48)/2=278. */
static EpdRect body_rect(void){return(EpdRect){40,188,604,700};}
static const char* app_settings_book_background_path(void){return "";}
/* 第 4 步起 reader_column_count 改读设置——harness 用可控桩驱动栏数，scenario() 把
   columns 参数写进桩，C/ID 系列的栏语义与旧版逐位一致（cols>1 ⟺ 双栏开+横屏开）。
   From step 4 reader_column_count reads the settings — the harness drives the count
   through controllable stubs; scenario() writes its columns argument into them, so the
   C/ID column semantics stay bit-identical (cols>1 iff two-column and landscape on). */
static int host_landscape=0,host_two_column=0;
bool app_settings_reader_landscape(void){return host_landscape!=0;}
bool app_settings_reader_two_column(void){return host_two_column!=0;}

/* 页表 fixture：页型由 spec 装入——'.' 纯文字；'i' 页内混排插图（image>=0 且 slots>0，
   普通页）；'I' 整页插图（JOINT：image>=0 且 slots==0）。与互证测试同款语义。
   Page-table fixture: page kinds come from the spec — '.' plain text; 'i' inline images
   (image>=0 with slots>0, an ordinary page); 'I' whole-page image (JOINT: image>=0 with
   slots==0). Same semantics as the cross-check test. */
#define TEST_PAGE_MAX 64
static size_t test_page_count;
static int test_page_image[TEST_PAGE_MAX];
static int test_page_image_slots[TEST_PAGE_MAX];
static uint32_t test_generation=1;
static void fixture_load(const char* spec){
    test_page_count=strlen(spec);
    assert(test_page_count>0&&test_page_count<TEST_PAGE_MAX);
    for(size_t k=0;k<test_page_count;++k){
        test_page_image[k]=spec[k]=='.'?-1:spec[k]=='i'?5:7;
        test_page_image_slots[k]=spec[k]=='i'?1:0;
    }
}
size_t book_layout_page_count(void){return test_page_count;}
int book_layout_page_image(size_t page){return page<test_page_count?test_page_image[page]:-1;}
int book_layout_page_image_count(size_t page){return page<test_page_count?test_page_image_slots[page]:0;}
uint32_t book_layout_generation(void){return test_generation;}
size_t book_layout_page_for_offset(size_t off){
    if(!test_page_count)return 0;
    size_t page=off/100;
    return page<test_page_count?page:test_page_count-1;
}

/* prep 状态（上游 harness 同款字段）/ prep state (the upstream harness's fields) */
static int s_view=READING;
static const char* s_text="text";
static bool s_toolbar,s_clear_confirm;
static int s_reader_panel=READER_PANEL_NONE;
static size_t s_page;
static uint8_t* s_next_fb;
static int s_next_page=-1,s_prep_page=-1;
static uint32_t s_next_generation;
static book_layout_draw_cursor_t s_prep_cursor;
static atomic_bool s_prep_stop;
static SemaphoreHandle_t s_draw_lock,s_prep_done;
static TaskHandle_t s_prep_task;

/* draw_slice 桩：一次只画一行，TEST_ROWS 行收尾；每次真正作画记录页与矩形，供几何
   断言。代次/页不匹配或已失败返 -1，已完成幂等返 1——与产品同构。
   draw_slice stub: one row per call, TEST_ROWS rows finish a page; every painting call
   records its page and rect for the geometry assertions. Generation/page mismatch or
   failure returns -1, a completed cursor idempotently returns 1 — isomorphic to the
   product. */
#define TEST_ROWS 5
static size_t slice_calls;
static size_t slice_page[128];
static EpdRect slice_rect[128];
static int book_layout_draw_slice(uint8_t* fb,size_t page,EpdRect rect,int px,book_layout_draw_cursor_t* cursor){
    (void)fb;
    assert(px==s_px);
    assert(page<test_page_count);
    if(cursor->generation&&cursor->generation!=test_generation)return -1;
    if(!cursor->generation){cursor->generation=test_generation;cursor->page=page;}
    if(cursor->generation!=test_generation||cursor->page!=page||cursor->failed)return -1;
    if(cursor->complete)return 1;
    if(slice_calls<128){slice_page[slice_calls]=page;slice_rect[slice_calls]=rect;}
    ++slice_calls;
    ++cursor->offset;cursor->started=true;
    if(cursor->offset>=TEST_ROWS){cursor->complete=true;return 1;}
    return 0;
}
'''

MAIN = r'''
/* ---- 情景助手 / scenario helpers ---- */
static const EpdRect AREA={40,188,604,700};
static const EpdRect COL0={40,188,278,700};    /* 左栏：x=版心左缘，宽 278 */
static const EpdRect COL1={366,188,278,700};   /* 右栏：x=40+278+48=366 */
static void fail(const char* what){printf("FAIL: %s\n",what);exit(1);}
#define CHECK(cond,what) do{if(!(cond))fail(what);}while(0)
static bool rect_eq(EpdRect a,EpdRect b){return a.x==b.x&&a.y==b.y&&a.width==b.width&&a.height==b.height;}
static void scenario(const char* spec,int columns,size_t page){
    fixture_load(spec);
    host_landscape=1;host_two_column=columns>1;
    reader_rebuild_page_map(columns);
    s_page=page;
    s_next_page=s_prep_page=-1;s_next_generation=0;
    memset(&s_prep_cursor,0,sizeof(s_prep_cursor));
    test_generation=1;
    slice_calls=0;
}
static void complete_prep(void){
    for(int i=0;i<256;++i){if(reader_prefetch_step()!=0)return;}
    fail("prep did not complete within 256 steps");
}
/* 记录行区间 [from,to) 必须全部是 (page, rect)：双栏缝合的几何判据。
   The recorded rows [from,to) must all be (page, rect): the geometry criterion of the
   two-column stitch. */
static void check_rows(size_t from,size_t to,size_t page,EpdRect rect,const char* what){
    for(size_t i=from;i<to;++i){
        if(slice_page[i]!=page||!rect_eq(slice_rect[i],rect)){
            printf("FAIL: %s (row %zu: page=%zu rect=%d,%d,%d,%d want page=%zu rect=%d,%d,%d,%d)\n",
                   what,i,slice_page[i],slice_rect[i].x,slice_rect[i].y,slice_rect[i].width,slice_rect[i].height,
                   page,rect.x,rect.y,rect.width,rect.height);
            exit(1);
        }
    }
}

/* OC（开形态 = 单栏拼接，§9.2）：覆盖性 + 配对性 + 双路径等价，作为常驻守卫——
   双栏开关 step 4 起可经真实设置激活，故无条件随 harness 运行，不再休眠。
   OC (open form = single-column concatenation, §9.2): coverage + pairing +
   dual-path equivalence as a permanent guard — the two-column switch is reachable
   through the real settings from step 4, so it runs unconditionally, no longer dormant. */
static void open_form_guard(void){
    /* 纯文字章：退化公式路径——覆盖性/配对性/双路径等价全验。 */
    scenario("......",2,0);
    {
        size_t n=book_layout_page_count();
        size_t vc=reader_visual_count();
        CHECK(vc==(n+1)/2,"OC: a plain chapter yields (n+1)/2 visual pages");
        CHECK(reader_visual_start(0)==0,"OC: the first visual page starts at the chapter's first logical page");
        size_t prev_end=0;
        for(size_t i=0;i<vc;++i){
            size_t l=0,r=0;
            reader_logical_pages(i,&l,&r);
            /* 配对性：视觉页 i 覆盖 2i / 2i+1（退化路径，无 off-by-one）。 */
            CHECK(l==2*i,"OC: visual page i pairs its left column to logical 2i");
            if(2*i+1<n) CHECK(r==2*i+1,"OC: the paired right column maps to 2i+1");
            else        CHECK(r==SIZE_MAX,"OC: a lone left column when 2i+1 runs past the end");
            /* 覆盖性：首递增、相邻首尾相接（无丢失/无重复）。 */
            CHECK(reader_visual_start(i)==l,"OC: the visual start equals its left column page");
            size_t end=(r!=SIZE_MAX)?r:l;
            if(i>0) CHECK(reader_visual_start(i)==prev_end+1,"OC: adjacent visual pages connect head-to-tail (no gap, no overlap)");
            /* 双路径等价：logical→visual 与 visual→logical 互逆。 */
            CHECK(reader_visual_index(l)==i,"OC: dual-path round-trip on the left column");
            if(r!=SIZE_MAX) CHECK(reader_visual_index(r)==i,"OC: dual-path round-trip on the right column");
            prev_end=end;
        }
        CHECK(prev_end==n-1,"OC: the last visual page's range closes on the chapter's last logical page (no loss)");
    }
    /* 含整页插图章：数组路径——覆盖性 + 双路径等价（JOINT 独占语义取代配对性）。 */
    scenario(".I....",2,0);
    {
        size_t n=book_layout_page_count();
        size_t vc=reader_visual_count();
        CHECK(reader_visual_start(0)==0,"OC: the first visual page still starts at logical 0 with a JOINT page present");
        size_t prev_end=0;
        for(size_t i=0;i<vc;++i){
            size_t l=0,r=0;
            reader_logical_pages(i,&l,&r);
            CHECK(reader_visual_start(i)==l,"OC: the visual start equals its left column page");
            size_t end=(r!=SIZE_MAX)?r:l;
            if(i>0) CHECK(reader_visual_start(i)==prev_end+1,"OC: adjacent visual pages connect head-to-tail across the JOINT boundary (no gap, no overlap)");
            CHECK(reader_visual_index(l)==i,"OC: dual-path round-trip on the left column (array path)");
            if(r!=SIZE_MAX) CHECK(reader_visual_index(r)==i,"OC: dual-path round-trip on the right column (array path)");
            prev_end=end;
        }
        CHECK(prev_end==n-1,"OC: the JOINT chapter's ranges still cover every logical page exactly once (no loss, no duplication)");
    }
}

int main(void){
    touch_t touch={0};
    app_ctx_t ctx={.touch=&touch};

    /* C1：两栏配对下 kick 的目标是下一视觉页的左栏，不是 s_page+1——后者是当前视觉页
       的右栏，拿它当下一帧等于把右栏重绘一遍。退化公式路径（无 JOINT）与数组路径
       （含 JOINT）各测一遍。 */
    scenario("....",2,0);
    CHECK(reader_visual_count()==2,"C1: two visual pages");
    CHECK(kick_prep(),"C1: kick allowed");
    CHECK(s_prep_page==2,"C1: target is the next visual page's left column, not s_page+1");
    complete_prep();
    CHECK(s_next_page==2&&s_next_generation==test_generation,"C1: published frame");
    /* 缝合几何：左栏 TEST_ROWS 行进左栏矩形，右栏 TEST_ROWS 行进右栏矩形（C6a）。 */
    CHECK(slice_calls==2*TEST_ROWS,"C1: a two-column frame paints both columns");
    check_rows(0,TEST_ROWS,2,COL0,"C1: left rows paint the left logical page in the left column rect");
    check_rows(TEST_ROWS,2*TEST_ROWS,3,COL1,"C1: right rows paint the right logical page in the right column rect");
    /* C1b：数组路径——下一个视觉页是 JOINT 页（独占视觉页，无右栏）。 */
    scenario("..I..",2,0);
    CHECK(reader_visual_count()==3,"C1b: three visual pages with a JOINT page");
    CHECK(kick_prep(),"C1b: kick allowed");
    CHECK(s_prep_page==2,"C1b: the next visual page is the JOINT page (not s_page+1)");
    complete_prep();
    CHECK(s_next_page==2,"C1b: published frame");
    /* JOINT 目标无右栏：只画左栏，且走左栏矩形而不是联合区（落单左栏同款）。 */
    CHECK(slice_calls==TEST_ROWS,"C1b: a JOINT target paints one column only");
    check_rows(0,TEST_ROWS,2,COL0,"C1b: the JOINT target takes the left column rect");

    /* C2：目标视觉页覆盖的任一逻辑页有图即拒预绘；两栏皆无图放行。 */
    scenario("..i.",2,0);
    CHECK(!kick_prep(),"C2: an image in the target's left column refuses prefetch");
    CHECK(s_prep_page==-1,"C2: a refused kick leaves the target untouched");
    scenario("...i",2,0);
    CHECK(!kick_prep(),"C2: an image in the target's right column refuses prefetch");
    scenario("....",2,0);
    CHECK(kick_prep(),"C2: a clean target allows prefetch");

    /* C3：退化恒等——cols==1 时 target ≡ s_page+1、右栏恒不存在、切片矩形就是联合区
       本身（与上游 harness 76/118 行同判据；上游看守运行时，这里看守目标解析）。 */
    scenario("....",1,0);
    {
        size_t l=0,r=0;
        CHECK(reader_prep_target(0,&l,&r),"C3: the target resolves");
        CHECK(l==1&&r==SIZE_MAX,"C3: left=s_page+1, right absent");
    }
    CHECK(kick_prep(),"C3: kick allowed");
    CHECK(s_prep_page==1,"C3: target == s_page+1");
    complete_prep();
    CHECK(s_next_page==1,"C3: the published frame is s_page+1");
    CHECK(slice_calls==TEST_ROWS,"C3: one column of rows");
    check_rows(0,TEST_ROWS,1,AREA,"C3: the single-column slice takes the joint area");

    /* C4：idle 的 s_prep_page 门闸随映射同步。 */
    scenario("....",2,0);
    /* (a) 没有 kick：s_prep_page=-1 ≠ 映射左栏 2 → 不画。 */
    reader_prefetch_idle(&ctx);
    CHECK(slice_calls==0,"C4a: idle stays quiet before any kick");
    /* (b) 门闸比映射左栏：s_prep_page 若是上游 +1 值(1)，idle 仍不激活。 */
    s_prep_page=1;
    reader_prefetch_idle(&ctx);
    CHECK(slice_calls==0,"C4b: idle keys on the mapped left column, not s_page+1");
    /* (c) kick 后门闸达成：8 次步进预算内画不完一帧（TEST_ROWS=5，左栏收尾那次步进
       会顺手画右栏第一行，故最多 9 行），发布条件不达成。 */
    CHECK(kick_prep(),"C4c: kick allowed");
    CHECK(s_prep_page==2,"C4c: the mapped target");
    reader_prefetch_idle(&ctx);
    CHECK(slice_calls<=8+1,"C4c: idle honours its 8-step budget");
    CHECK(s_next_page==-1,"C4c: the frame is still incomplete after the budget");
    /* (d) 手动画完后再 idle：发布条件达成，不重复画。 */
    complete_prep();
    CHECK(s_next_page==2,"C4d: the frame is published");
    slice_calls=0;
    reader_prefetch_idle(&ctx);
    CHECK(slice_calls==0,"C4d: idle does not repaint a published frame");

    /* C5：视觉末页钳制——视觉页已到末页时拒预绘，即使逻辑上还有下一页。 */
    scenario("....",2,2);
    CHECK(!kick_prep(),"C5: the last two-column visual page refuses prefetch");
    CHECK(s_prep_page==-1,"C5: a refused kick leaves the target untouched");
    scenario("...",2,2);
    CHECK(!kick_prep(),"C5: a lone last visual page refuses prefetch");
    scenario("....",1,3);
    CHECK(!kick_prep(),"C5: the single-column last page refuses (upstream guard)");

    /* C6：invalidate 后同目标重踢——右栏游标必须重开，不能续画被清帧前的陈旧行。
       （P1 钩子的新代码，上游 harness 的链接闭包里没有它，只能在这里看守。） */
    scenario("....",2,0);
    CHECK(kick_prep(),"C6: the first kick");
    for(int i=0;i<TEST_ROWS;++i)CHECK(reader_prefetch_step()==0,"C6: left column plus the first right row");
    /* 此刻左栏已完、右栏画了 1 行（TEST_ROWS+1 条记录）。 */
    CHECK(slice_calls==TEST_ROWS+1,"C6: a partial frame before invalidate");
    invalidate_prep();
    slice_calls=0;   /* 只数重踢这一轮的记录 / count only the re-kick round */
    CHECK(kick_prep(),"C6: the same-target re-kick after invalidate");
    complete_prep();
    CHECK(s_next_page==2,"C6: the republished frame");
    CHECK(slice_calls==2*TEST_ROWS,"C6: the frame repaints from row one of both columns");
    check_rows(0,TEST_ROWS,2,COL0,"C6: fresh left rows");
    check_rows(TEST_ROWS,2*TEST_ROWS,3,COL1,"C6: fresh right rows");

    /* 恒等证明链（设计文档 §2.3 七步）的可执行形式：关形态（cols==1）下接线不改变
       上游可观测行为。第 1-3 步 = ID1-ID3（地图三函数恒等）；第 4 步「M1-M4 逐位
       退化为上游表达式」= ID4（M1 kick 目标）+ ID5（M2 idle 门闸/守卫）+ ID6
       （M3 图片守卫）+ C5 的单栏末页断言（M4）四件合持；第 5 步 = 上游 139 行
       harness 逐字节不动且全绿（tools/reader_idle_prefetch_host_test.py），第 6 步 =
       黄金三值复跑不变，第 7 步 = 互证测试开形态命名断言（§9.2 事例表）——后三步
       由各自测试看守，此处不重复造轮子。/ The closed-form identity proof chain
       (design doc section 2.3) in executable form: at the closed form (cols==1) the
       wiring changes nothing observable upstream. Steps 1-3 are ID1-ID3 (the map
       functions' identities); step 4 "M1-M4 degenerate to the upstream expressions
       bit for bit" is held jointly by ID4 (M1 kick target), ID5 (M2 idle gate and
       guard), ID6 (M3 image guard) and C5's single-column last-page assertion (M4);
       step 5 is the upstream 139-line harness untouched and green, step 6 is the
       golden three values unchanged, step 7 is the cross-check test's on-form
       assertions — those three stay with their own tests. */
    scenario("..I..",1,0);
    /* ① reader_visual_index 单栏恒等（JOINT 页两侧同样恒等）。 */
    for(size_t k=0;k<book_layout_page_count();++k)
        CHECK(reader_visual_index(k)==k,"ID1: the single-column visual index is the identity");
    /* ② visual_count == page_count。 */
    CHECK(reader_visual_count()==book_layout_page_count(),"ID2: the single-column visual count equals the page count");
    /* ③ 右栏恒不存在。 */
    for(size_t v=0;v<reader_visual_count();++v){
        size_t l=0,r=0;
        reader_logical_pages(v,&l,&r);
        CHECK(r==SIZE_MAX,"ID3: the right column is absent at single column");
    }
    /* ④ kick 目标逐位退化为 s_page+1（JOINT 页前后都测）。 */
    for(size_t p=0;p+1<book_layout_page_count();++p){
        scenario("..I..",1,p);
        CHECK(kick_prep(),"ID4: kick allowed");
        CHECK(s_prep_page==(int)p+1,"ID4: the target degenerates to s_page+1");
    }
    /* ⑤ M2 退化：idle 门闸与图片守卫在 cols==1 下逐位回归上游——门闸判据是 s_page+1
       （JOINT 页前后同测），达成才在 8 次步进预算内推进，发布后不重画，目标页有图
       同样拒画。/ Step 5, M2 degenerate: the idle gate and its image guard return to
       the upstream semantics bit for bit at cols==1 — the gate keys on s_page+1
       (tested around JOINT pages), advances within the 8-step budget once met, never
       repaints a published frame, and refuses when the target page has an image. */
    for(size_t p=0;p+1<book_layout_page_count();++p){
        scenario("..I..",1,p);
        reader_prefetch_idle(&ctx);
        CHECK(slice_calls==0,"ID5: idle stays quiet while the gate is unmet");
        s_prep_page=(int)p+1;
        reader_prefetch_idle(&ctx);
        CHECK(slice_calls>0,"ID5: idle paints once the gate reads s_page+1");
        CHECK(slice_calls<=8+1,"ID5: idle honours its 8-step budget");
    }
    scenario("..I..",1,0);
    CHECK(kick_prep(),"ID5: kick allowed");
    complete_prep();
    CHECK(s_next_page==1,"ID5: the published frame");
    slice_calls=0;
    reader_prefetch_idle(&ctx);
    CHECK(slice_calls==0,"ID5: idle does not repaint a published frame");
    /* 门闸达成但目标页有图：idle 侧图片守卫同样退化（上游第二项的 idle 侧）。/
       Gate met but the target page has an image: the idle-side guard degenerates
       the same way (the idle side of upstream's second term). */
    scenario(".i..",1,0);
    s_prep_page=1;
    reader_prefetch_idle(&ctx);
    CHECK(slice_calls==0,"ID5: idle honours the image guard on the target page");
    /* ⑥ M3 退化：cols==1 时 kick 的图片守卫坍缩为上游两条件——当前页有图拒（第一项）、
       目标页有图拒（第二项）、右栏条件恒假；JOINT 目标页无插图记录故放行。/
       Step 6, M3 degenerate: at cols==1 the kick image guard collapses to upstream's
       two conditions — an image on the current page refuses (first term), an image on
       the target page refuses (second term), the right-column condition stays
       vacuous; a JOINT target page carries no image record so prefetch is allowed. */
    scenario("i...",1,0);
    CHECK(!kick_prep(),"ID6: an image on the current page refuses (upstream term 1)");
    CHECK(s_prep_page==-1,"ID6: a refused kick leaves the target untouched");
    scenario(".i..",1,0);
    CHECK(!kick_prep(),"ID6: an image on the target page refuses (upstream term 2)");
    scenario(".I..",1,0);
    CHECK(kick_prep(),"ID6: a JOINT target page has no image record, prefetch allowed");
    CHECK(s_prep_page==1,"ID6: the JOINT target is still s_page+1");

    /* OC：开形态常驻守卫——双栏开关 step 4 起可经真实设置激活，故无条件运行。
       OC: the open-form permanent guard — the two-column switch is reachable through
       the real settings from step 4, so it runs unconditionally. */
    open_form_guard();

    printf("PASS: prefetch target addressing through the visual page map "
           "(C1 pair/JOINT mapping and stitch geometry, C2 image guard, C3 degenerate identity, "
           "C4 idle gate mapping and budget, C5 last visual page, C6 right-cursor reset on re-kick, "
           "ID1-ID6 closed-form identity chain steps 1-4 with M2/M3 degenerate guards)\n");
    return 0;
}
'''


def build_unit() -> str:
    src = BOOK.read_text(encoding='utf-8')
    parts = [PRELUDE]
    parts += [function(name, BOOK) + '\n' for name in GEOMETRY]
    parts.append(map_block(src))
    parts += [function(name, BOOK) + '\n' for name in AFTER_MAP]
    parts.append(MAIN)
    return ''.join(parts)


def main() -> int:
    unit = build_unit()
    with tempfile.TemporaryDirectory() as tmp:
        c = Path(tmp) / 'prefetch_map.c'
        exe = Path(tmp) / 'prefetch_map'
        c.write_text(unit, encoding='utf-8')
        subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-variable', '-Wno-unused-function',
                        '-fsanitize=address,undefined', str(c), '-o', str(exe)],
                       check=True)
        proc = subprocess.run([str(exe)], capture_output=True, text=True)
        sys.stdout.write(proc.stdout)
        if proc.returncode != 0:
            sys.stderr.write(proc.stderr)
            print(f'reader_prefetch_map: FAIL (exit {proc.returncode})', file=sys.stderr)
            return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
