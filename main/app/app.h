/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * demo 页的统一接口。每个页面是一个 app_desc_t，放在 main/apps/ 下自己的文件里，
 * 绘制、命中、轮询和页内状态都封在那一个文件中；app_loop.c 只按这套回调调度，
 * 加一个页面不必再改主循环。
 *
 * Shared page contract. Each page is an app_desc_t in its own main/apps/ file.
 * Draw, hit-test, poll and page state stay there. app_loop.c only dispatches
 * these callbacks; adding a page does not touch the loop.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cst836u.h"
#include "epd_highlevel.h"
#include "epdiy.h"
#include "app_config.h"
#include "sc7a20h_lab.h"

#ifdef __cplusplus
extern "C" {
#endif

/// 回调返回值告诉主循环该怎么把 framebuffer 推上屏。
/// / What the loop should do with the framebuffer.
typedef enum {
    /// 什么都没变，不刷屏。/ Nothing changed.
    APP_REDRAW_NONE = 0,
    /// 只有 area_hint() 那块变了，使用 APP_DYNAMIC_REFRESH_MODE。回调必须自己先画好 fb。
    /// / Push area_hint() with APP_DYNAMIC_REFRESH_MODE. Caller paints fb first.
    APP_REDRAW_AREA,
    /// 由主循环调 render() 重画整页，使用 APP_PAGE_REFRESH_MODE。
    /// / Loop calls render() and presents with APP_PAGE_REFRESH_MODE.
    APP_REDRAW_PAGE,
    /// 同上；APP_PAGE_FORCE_FULL 置 1 时升为整屏 GC16，否则仍走 APP_PAGE_REFRESH_MODE。
    /// / Same; full-screen GC16 when APP_PAGE_FORCE_FULL is 1.
    APP_REDRAW_FULL,
    /// 回调自己已经刷完屏了（自检、刷新测试这类要分步显示进度的页）。
    /// / Page already presented (self-test, refresh bench, …).
    APP_REDRAW_DONE,
} app_redraw_t;

typedef struct app_desc_s app_desc_t;

typedef struct {
    EpdiyHighlevelState* hl;
    uint8_t* fb;
    sc7a20h_handle_t acc;
    cst836u_handle_t tp;
    bool sensor_ready;
    bool continuous_ready;
    int64_t now_ms;
    /// 去抖后的触摸快照：空槽已清零，抬手后保留最后一次坐标。
    /// / Debounced touch: empty slots cleared; last coords kept after lift.
    const cst836u_touch_t* touch;
    const cst836u_info_t* touch_info;
    /// 本轮是不是按下沿 / 抬起沿，触摸页的跟随节流要用。
    /// / Press/release edge this tick. Touch tracking uses this to throttle.
    bool pressed;
    bool released;
    /// 分页页（字体列表）当前停在第几页，由 KEY1 和页内按钮共同改。
    /// / Leaf page index (font list). KEY1 and in-page buttons both change it.
    int leaf;
    /// 本轮已经被别的分支处理掉了，页内的跟随逻辑要跳过这一轮。
    /// / Another branch already handled this tick; skip in-page tracking.
    bool consumed;
    /// 页面想跳到别的页时填这里，主循环会在本轮末尾切过去。
    /// / Request a page switch; the loop applies it at the end of the tick.
    const struct app_desc_s* request_app;
} app_ctx_t;

struct app_desc_s {
    /// 菜单里显示的标题与副标题。/ Menu title and subtitle.
    const char* title;
    const char* detail;
    /// true = 这一页会长时间独占 PMU，主循环的锁屏按键轮询要让路（设备功能自检）。
    /// / true = page owns the PMU; lock-key poll yields (device self-test).
    bool holds_pmu;
    /// true = 进页走 APP_REDRAW_FULL（均衡配置下整屏 GC16）。内容和上一页差别大的页面用，避免差分刷留边。
    /// / true = enter with APP_REDRAW_FULL so a differential update does not leave a seam.
    bool enter_full;
    /// 进页/离页。on_enter 里做上电、唤醒传感器、拉一次数据这类副作用。
    /// / Enter/exit. on_enter powers up, wakes sensors, takes a first sample.
    void (*on_enter)(app_ctx_t* ctx);
    void (*on_exit)(app_ctx_t* ctx);
    /// 纯绘制，不做 I2C 写、蜂鸣这类副作用，整屏强刷才能安全复用它。
    /// / Paint only. No I2C writes or buzzer. KEY2 full redraw reuses this.
    void (*render)(app_ctx_t* ctx, uint8_t* fb);
    /// 自定义推屏。返回 true 表示页面已经刷过。
    /// / Custom present. Return true if the page updated the display.
    bool (*present)(app_ctx_t* ctx, app_redraw_t redraw);
    app_redraw_t (*on_touch)(app_ctx_t* ctx, const cst836u_touch_t* touch);
    /// key 是 UI_KEY_1。KEY2 整屏强刷、KEY3 开关演示菜单，都由主循环统一处理。
    /// / key is UI_KEY_1. KEY2 full redraw and KEY3 menu handle are global.
    app_redraw_t (*on_key)(app_ctx_t* ctx, int key);
    /// 每轮都调，页面自己判断到不到刷新间隔。
    /// / Called every tick; the page decides if its interval has elapsed.
    app_redraw_t (*on_tick)(app_ctx_t* ctx);
    /// APP_REDRAW_AREA 要刷的区域。/ Area for APP_REDRAW_AREA.
    EpdRect (*area_hint)(app_ctx_t* ctx);
    /// 同一份实现挂多个 app 时用来区分（加速度计的读数页与诊断页共享绘制代码）。
    /// / Distinguishes views that share one implementation (IMU live vs lab).
    void* user;
};

// 锁屏：浅睡按键或拿起回原页；软睡 EN=0，再短按开机；关机 EN=0，长按开机。
// Lock: light-sleep key or pickup returns to the page; soft sleep drops EN
// and a short press powers on; power-off drops EN and needs a long press.
#define APP_LOCK_POLL_MS 200
#define APP_LOCK_IGNORE_BOOT_MS 2000
#define APP_LOCK_IGNORE_SELFTEST_MS 2000

/// 按 redraw 把 framebuffer 推上屏，顺带做 pclk 欠载兜底。APP_REDRAW_DONE 直接返回。
/// / Present fb for redraw, with a pclk-underrun fallback. APP_REDRAW_DONE returns.
void app_present(app_ctx_t* ctx, const app_desc_t* app, app_redraw_t redraw);
/// 之后 ms 毫秒内忽略电源键短按，避免开机或自检的残留事件立刻把屏锁掉。
/// / Ignore short power-key presses for ms so boot/self-test leftovers do not lock.
void app_lock_ignore_for(int64_t ms);

#ifdef __cplusplus
}
#endif
