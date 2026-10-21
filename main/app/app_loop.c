/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 事件循环。只做和页面无关的事：读触摸并去抖、三个实体键与菜单把手的路由、切页
 * 时调 on_exit/on_enter、按回调返回值决定怎么刷屏、电源键锁屏、SD 卡上的字体延迟
 * 加载。页面自己的状态和刷新节奏都在 main/apps/ 各自的文件里。
 *
 * Event loop for page-agnostic work: touch debounce, the three keys and
 * the menu handle, on_exit/on_enter, present, lock, delayed SD font load.
 * Page state and refresh cadence stay in main/apps/; gesture recognition is shared.
 *
 * 冻结：主循环只产出手势、不解释含义；产品系统页接管三键，按用户要求
 * 左右返回、中键首页；阅读页保留翻页、页面设置和长按回书架。把手仍按下即触发。
 * 接管页可选长按键回调，保留按下行为；同键单指500ms仅触发一次，中断取消。
 * Frozen: Produce gestures without interpreting page actions. Product system pages
 * own Back/Home/Back keys; reading retains page turns, settings and hold-to-shelf.
 * Owners may opt into a single 500ms same-key hold callback after the press action; interruptions cancel it.
 * The menu handle still fires on press.
 * 用户修订：四个主页面底栏作为统一导航路由在按下时报点即响应，模态和阅读手势保持原归属。
 * User revision: root-tab routing commits on initial contact; modal controls and reader gestures retain their owners.
 * 用户修订：系统页面切换使用 GL16，不再每次 GC16 全刷；锁屏、手动强刷和周期清残影仍可全刷。
 * User revision: system page changes use GL16 instead of GC16; lock, manual refresh and periodic ghost cleanup may still use full refresh.
 * 用户修订：设置可选普通/快刷/水波纹，默认普通；普通保留原灰阶路线，快刷采用确认的黑白主页面和亚克力灰阶。用户反馈残影和黑带后，书架退出只同向擦白，主页水波纹改白底/24窄带且端点单向推动，中灰保留校准序列；五行横条局部移动、图标固定，没有异步补刷。
 * User revision: settings offer ordinary/fast/water, default ordinary. Ordinary retains the original gray path; fast uses approved monochrome main screens and gray acrylic. Following reported ghosts and dark bands, shelf exit whitens directionally; main water uses white backgrounds, 24 narrow bands and directional endpoints, retaining calibrated midgrays. Move the five-row marker locally, retain icons, and schedule no asynchronous completion.
 * 用户修订：普通模式也固定主页面底栏，仅局部移动横条；快刷亚克力采用规则一像素棋盘格，与已缓存封面一起输出。普通及水波纹保留灰阶，首次进入及解锁仍完整绘制底栏。
 * User revision: ordinary main screens also retain navigation and move only its marker locally; fast acrylic uses a regular one-pixel checkerboard in the same output as cached covers. Ordinary/water retain grays; initial entry and unlock still draw complete navigation.
 * 冻结：返回消费最近切页来源，恢复原页及菜单位置，不跳固定首页。
 * Frozen: Return consumes the latest page origin, restoring its page and menu position.
 * 用户采用顶部快捷层：仅顶部32px起手下滑72px才接管，覆盖时冻结页内输入/轮询，WiFi直接连接最近保存网络/再次断开，蓝牙切换开关，全刷和锁屏复用原路径。用户要求三个模式均真实灰阶并加深轮廓；关闭重绘当前页并局部整理，不修改底栏。
 * User adopts the top quick layer: capture only a 72px downward swipe starting within the top 32px; suspend page input/ticks while covered. WiFi connects saved credentials or disconnects on the next tap; Bluetooth toggles its setting. Reuse full refresh and lock. Per the user, all modes retain grays with clearer outlines. Closing redraws and locally settles the underlay, retaining navigation.
 * 用户点击反馈：底栏及圆形返回键按下局部放大，释放复位；只暂存一个小控件，切页、全刷及锁屏丢弃缓存，不回放旧画面。
 * User click feedback: locally enlarge navigation/round backs on press and restore on release. Retain one small control only; navigation, full refresh and lock discard it without replaying old frames.
 * 用户修订：确认主动锁屏后，拼贴样式先给局部圆锁反馈，再保存/等待阅读预渲染；自动休眠和直接关机/重启不加此反馈。
 * User revision: acknowledge accepted manual collage locks before saves/reader joins; omit the badge for automatic sleep and direct shutdown/restart.
 */

#include "app_loop.h"
#include "auto_lock.h"
#include "crash_note.h"

#include <string.h>

#include "app_registry.h"
#include "app_content_open.h"
#include "boot_state.h"
#include "ble_page_turner.h"
#include "push_sink.h"
#include "ota_online.h"
#include "continuous_du.h"
#include "display.h"
#include "e0470_epaper_waveform.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "read_pico_board.h"
#include "read_pico_crashlog.h"
#include "read_pico_pmu.h"
#include "read_pico_transfer.h"
#include "read_pico_sd.h"
#include "settings.h"
#include "sleep.h"
#include "time_service.h"
#include "ui_avatar.h"
#include "book/book_cover_jobs.h"
#include "book/book_lock_collage.h"
#include "ttf_font.h"
#include "app_font_context.h"
#include "ui_kit.h"
#include "ui_menu.h"
#include "ui_nav.h"
#include "ui_gesture.h"
#include "ui_power_dialog.h"
#include "ui_quick_menu.h"
#include "ui_click_feedback.h"
#include "usb_storage.h"
#include "local_latency_probe.h"

#define TAG "app_loop"

// 触摸芯片报点率远高于屏幕，主循环空转一轮只等这么久。
// Touch reports far faster than the panel; idle this long per loop.
#define LOOP_TICK_MS 5
// SD 卡插上或者刚挂好时，隔一会儿再试一次存在设置里的字体。
// Retry the saved SD font shortly after a card appears or mounts.
#define FONT_RETRY_INTERVAL_MS 750
#define MEDIA_POLL_INTERVAL_MS 500

static int64_t s_lock_ignore_until_ms;
extern const app_desc_t app_transfer;

static const app_desc_t* s_presented_main;
static ui_quick_menu_t s_quick;
static unsigned s_quick_status;
// 锁屏占用整屏后旧主页面底栏不再是物理基准；解锁第一帧必须完整绘制。
// A lock screen replaces the old navigation baseline; the first unlocked frame must render complete navigation.
static void main_lock_boundary(void) {
    ui_click_feedback_reset();
    ui_quick_menu_reset(&s_quick);
    s_presented_main = NULL;
    display_main_transition_cancel();
}

static unsigned quick_status(void) {
    read_pico_transfer_status_t status;
    read_pico_transfer_get_status(&status);
    bool wifi = status.mode == READ_PICO_TRANSFER_MODE_STA && status.state != READ_PICO_TRANSFER_STOPPED;
    return (wifi ? 1u : 0u) | (app_settings_ble_turner() ? 2u : 0u) | (status.network_ready ? 4u : 0u);
}

// 同步输出完再接收下一条触摸，不安排旧层回放；错误时关闭层并可靠全刷当前页。
// Complete presentation before accepting another contact, with no stale replay; close and full-refresh the current page on error.
static void quick_present(app_ctx_t *ctx, const app_desc_t *current, bool full) {
    ui_click_feedback_begin(ctx->fb);
    display_main_transition_cancel();
    if (current->render) current->render(ctx, ctx->fb);
    s_quick_status = quick_status();
    if (s_quick.open) ui_quick_menu_draw(ctx->fb, s_quick_status & 1u, s_quick_status & 2u);
    enum EpdDrawError error = full ? update_display_full(ctx->hl)
        : update_display_area_full_with(ctx->hl, &E0470_WAVEFORM, MODE_GL16, ui_quick_menu_rect());
    guard_draw_result(ctx->hl, error);
    if (error != EPD_DRAW_SUCCESS) {
        ui_quick_menu_reset(&s_quick);
        app_present(ctx, current, APP_REDRAW_FULL);
    }
}

static void quick_toggle_wifi(void) {
    read_pico_transfer_status_t status;
    read_pico_transfer_get_status(&status);
    if (status.state != READ_PICO_TRANSFER_STOPPED) {
        // 上传期间拒绝断开，复用存储任务已有的原子准入保护。
        // Refuse disconnecting during upload using the storage owner's atomic admission guard.
        (void)read_pico_transfer_try_stop_if_idle();
    } else {
        char ssid[33] = {0}; bool configured = false;
        if (read_pico_transfer_get_saved_wifi(ssid, &configured) == ESP_OK && configured) {
            // gen18：武装期不释放蓝牙占用——与息屏同款豁免（覆盖表第 1 行）。
            // ESP32 上 WiFi 与 BLE 可共存，跳过 ble_pt_sync(false) 让推送广播持续；
            // 栈由主循环 ble_pt_sync((armed||turner)&&!wifi_busy&&!flight) 继续保活。
            // 此前无条件 ble_pt_sync(false) 是漏网的武装期拆栈入口（覆盖表修正项）。
            // gen18: do not release Bluetooth while armed - the same exemption as
            // screen-off (coverage-table row 1). WiFi and BLE coexist on the ESP32,
            // so skip ble_pt_sync(false) and keep the push advertisement up; the
            // loop's ble_pt_sync((armed||turner)&&!wifi_busy&&!flight) keeps the
            // stack. An unconditional ble_pt_sync(false) here was a missed armed
            // teardown entry (coverage-table correction).
            if (!push_sink_armed()) ble_pt_sync(false);
            else ESP_LOGI(TAG, "wifi toggle skipped ble stop: push sink armed");
            const read_pico_transfer_cfg_t cfg = {.mode = READ_PICO_TRANSFER_MODE_STA, .network_only = true};
            (void)read_pico_transfer_start(&cfg);
        }
    }
}
static bool main_page_ready(app_ctx_t* ctx, const app_desc_t* app);

static void main_present_finished(app_ctx_t* ctx, const app_desc_t* app, app_redraw_t redraw) {
    if (redraw == APP_REDRAW_DONE || !main_page_ready(ctx, app)) s_presented_main = NULL;
    else if (redraw != APP_REDRAW_NONE) s_presented_main = app;
    display_main_transition_disarm();
}

void app_lock_ignore_for(int64_t ms) {
    s_lock_ignore_until_ms = esp_timer_get_time() / 1000 + ms;
}

void app_present(app_ctx_t* ctx, const app_desc_t* app, app_redraw_t redraw) {
#ifdef KIIKO_LOCAL_LATENCY
    const int64_t probe_start=esp_timer_get_time();
    extern int64_t local_display_elapsed_us;
    const int64_t probe_display=local_display_elapsed_us;
#define LATENCY_PRESENT_DONE() do { if(redraw==APP_REDRAW_PAGE||redraw==APP_REDRAW_FULL) \
    ESP_LOGI("latency","PRESENT page=%d redraw=%d total_us=%lld display_us=%lld paint_us=%lld", \
    app_index_of(app),redraw,(long long)(esp_timer_get_time()-probe_start), \
    (long long)(local_display_elapsed_us-probe_display), \
    (long long)(esp_timer_get_time()-probe_start-(local_display_elapsed_us-probe_display))); } while(0)
#else
#define LATENCY_PRESENT_DONE() do {} while(0)
#endif
    enum EpdDrawError result = EPD_DRAW_SUCCESS;
    if (redraw == APP_REDRAW_PAGE && ui_click_feedback_active()) {
        // 切页固定底栏之前先复位真实按压像素，避免把放大的图标当作长期基准。
        // Restore physical pressed pixels before freezing navigation, so an enlarged icon never becomes its baseline.
        EpdRect area;
        if (ui_click_feedback_release(ctx->fb, &area)) {
            display_main_transition_cancel();
            result = update_display_area_diff_with(ctx->hl, &E0470_FOLLOW_WAVEFORM, MODE_DU, area);
            guard_draw_result(ctx->hl, result);
            if (result != EPD_DRAW_SUCCESS) redraw = APP_REDRAW_FULL;
        }
    }
    if (redraw == APP_REDRAW_PAGE || redraw == APP_REDRAW_FULL) ui_click_feedback_begin(ctx->fb);
    ui_text_set_system_scale(true);
    // 后台目录/封面完成及滚动仍属于当前主页面，不能借普通整页出口闪到底栏。
    // Background directory/cover completion and scrolling remain on this main page and must not flash navigation via ordinary page output.
    if ((redraw == APP_REDRAW_PAGE || redraw == APP_REDRAW_AREA) &&
        s_presented_main == app && main_page_ready(ctx, app)) display_main_transition_arm(app, false, false);
    if (app->present != NULL && app->present(ctx, redraw)) {
        main_present_finished(ctx, app, redraw);
        LATENCY_PRESENT_DONE();
        return;
    }
    switch (redraw) {
        case APP_REDRAW_NONE:
        case APP_REDRAW_DONE:
            main_present_finished(ctx, app, redraw);
            return;
        case APP_REDRAW_AREA: {
            // 回调已经画好了 fb，这里只负责把它那一块推上屏。
            // The callback already painted fb; push that rectangle only.
            EpdRect area = app->area_hint != NULL
                ? app->area_hint(ctx)
                : ui_content_refresh_area();
            result = update_display_area_with(
                ctx->hl, &E0470_WAVEFORM, APP_DYNAMIC_REFRESH_MODE, area
            );
            break;
        }
        case APP_REDRAW_FULL:
            if (app->render != NULL) app->render(ctx, ctx->fb);
            result = APP_PAGE_FORCE_FULL
                ? update_display_full(ctx->hl)
                : update_display_mode(ctx->hl, APP_PAGE_REFRESH_MODE);
            break;
        case APP_REDRAW_PAGE:
        default:
            if (app->render != NULL) app->render(ctx, ctx->fb);
            result = update_display_fast_page(ctx->hl);
            break;
    }
    guard_draw_result(ctx->hl, result);
    main_present_finished(ctx, app, redraw);
    LATENCY_PRESENT_DONE();
#undef LATENCY_PRESENT_DONE
}

// 反馈只推控件附近，失败后重新绘制当前页面；不占用底栏切页许可。
// Push feedback only near its control, repainting the current page on failure without consuming navigation permission.
static void click_present(app_ctx_t *ctx, const app_desc_t *current, EpdRect area) {
    display_main_transition_cancel();
    enum EpdDrawError error = update_display_area_diff_with(ctx->hl, &E0470_FOLLOW_WAVEFORM, MODE_DU, area);
    guard_draw_result(ctx->hl, error);
    if (error != EPD_DRAW_SUCCESS) app_present(ctx, current, APP_REDRAW_FULL);
}

// 中断边界先清页面装饰再清识别器。/ Clear decoration before resetting recognition.
static void cancel_gesture(app_ctx_t* ctx, const app_desc_t* app, ui_gesture_t* gesture) {
    if (gesture->active && app->on_gesture) {
        ui_gesture_event_t event = { .type = UI_GESTURE_CANCEL,
            .x0 = gesture->x0, .y0 = gesture->y0, .x = gesture->x, .y = gesture->y };
        const app_desc_t* requested = ctx->request_app;
        bool menu_requested = ctx->request_menu;
        bool return_requested = ctx->request_return;
        app_redraw_t redraw = app->on_gesture(ctx, &event);
        ctx->request_app = requested;
        ctx->request_menu = menu_requested;
        ctx->request_return = return_requested;
        ui_gesture_reset(gesture);
        app_present(ctx, app, redraw);
    }
    ui_gesture_reset(gesture);
}

static void present_page(app_ctx_t* ctx, const app_desc_t* app,
                         ui_gesture_t* gesture, app_redraw_t redraw) {
    if (redraw == APP_REDRAW_PAGE || redraw == APP_REDRAW_FULL)
        cancel_gesture(ctx, app, gesture);
    app_present(ctx, app, redraw);
}

typedef struct {
    unsigned updates;
    int64_t last_ms;
    EpdRect area;
} menu_feedback_t;

// 菜单反馈只推叶内单行。/ Menu feedback pushes one local row only.
static void menu_feedback(app_ctx_t* ctx, const app_desc_t* current,
                          int leaf, int hit, bool pressed, menu_feedback_t* feedback) {
    EpdRect area;
    int row = hit - leaf * UI_MENU_ITEMS_PER_PAGE;
    if (!ui_menu_row_rect(leaf, row, &area)) return;
    ui_draw_menu_row_pressed(ctx->fb, current, leaf, row, pressed);
    if (feedback->updates == 0) feedback->area = area;
    else {
        int bottom = feedback->area.y + feedback->area.height;
        int next_bottom = area.y + area.height;
        if (area.y < feedback->area.y) feedback->area.y = area.y;
        feedback->area.height = (bottom > next_bottom ? bottom : next_bottom) - feedback->area.y;
    }
    feedback->updates++;
    feedback->last_ms = ctx->now_ms;
    guard_draw_result(ctx->hl, update_display_area_with(
        ctx->hl, &E0470_WAVEFORM, APP_DYNAMIC_REFRESH_MODE, area));
    if (feedback->updates >= UI_SETTLE_DU_MAX) {
        guard_draw_result(ctx->hl, update_display_area_with(
            ctx->hl, &E0470_WAVEFORM, MODE_GL16, feedback->area));
        feedback->updates = 0;
    }
}

// 切页：先让上一页收尾，再给新页一次 on_enter，最后整页画出来。
// Leave the old page, enter the new one, then present a full page.
static bool main_page_ready(app_ctx_t* ctx, const app_desc_t* app) {
    int index = app_index_of(app);
    return index >= 0 && index < 4 &&
           app->main_page_visible && app->main_page_visible(ctx);
}

static void switch_to(
    app_ctx_t* ctx, const app_desc_t** current, const app_desc_t* next
) {
    if (next == NULL || next == *current) return;
#ifdef KIIKO_LOCAL_LATENCY
    const int64_t probe_start=esp_timer_get_time();
    const int probe_from=app_index_of(*current);
#endif
    bool main_source = main_page_ready(ctx, *current);
    bool shelf_source = main_source && app_index_of(*current) == 1;
    display_main_transition_cancel();
    if ((*current)->on_exit != NULL) (*current)->on_exit(ctx);
#ifdef KIIKO_LOCAL_LATENCY
    const int64_t probe_exit=esp_timer_get_time();
#endif
    *current = next;
    app_font_activate_system();
#ifdef KIIKO_LOCAL_LATENCY
    const int64_t probe_font=esp_timer_get_time();
#endif
    if (next->on_enter != NULL) next->on_enter(ctx);
#ifdef KIIKO_LOCAL_LATENCY
    const int64_t probe_enter=esp_timer_get_time();
#endif
    if (main_source && main_page_ready(ctx, next)) display_main_transition_arm(next, shelf_source, true);
    app_present(ctx, next, next->enter_full ? APP_REDRAW_FULL : APP_REDRAW_PAGE);
    display_main_transition_disarm();
#ifdef KIIKO_LOCAL_LATENCY
    ESP_LOGI("latency","SWITCH from=%d to=%d mode=%d exit_us=%lld font_us=%lld enter_us=%lld present_us=%lld total_us=%lld",probe_from,app_index_of(next),app_settings_main_refresh_mode(),
        (long long)(probe_exit-probe_start),(long long)(probe_font-probe_exit),(long long)(probe_enter-probe_font),
        (long long)(esp_timer_get_time()-probe_enter),(long long)(esp_timer_get_time()-probe_start));
#endif
    ESP_LOGI(TAG, "page -> %s", next->title);
}

// 菜单页不是 app，单独画。/ The menu is not an app; present it here.
static void present_menu(app_ctx_t* ctx, const app_desc_t* current, int leaf, menu_feedback_t* feedback) {
    s_presented_main = NULL;
    feedback->updates = 0;
    if (display_take_white_exit()) {
        guard_draw_result(ctx->hl, update_display_white(ctx->hl));
    }
    ui_draw_menu_page(ctx->fb, current, leaf);
    guard_draw_result(ctx->hl, update_display_fast_page(ctx->hl));
}

// 横屏阅读：把 CST836U 原生坐标转成横屏逻辑坐标。触摸面板物理帧 684×1216，与
// 竖屏（INVERTED_PORTRAIT）逻辑帧逐像素重合，所以竖屏下原生坐标可以直接当逻辑
// 坐标用；横屏（LANDSCAPE）逻辑帧是 1216×684，必须补 90° 映射——缺失时原始 x
// 只覆盖 0..683，右半屏（逻辑 x≥684）永远触不到，且右半屏落点的 y 越界，被所有
// 命中测试（ui_rect_hit / book_layout_hit_test / ui_nav_hit）静默丢弃。
// 推导：同一个物理落点在两种方向下对应同一个帧缓冲像素——竖屏逻辑 (tx,ty) 经
// epdiy _rotate(INVERTED_PORTRAIT) 得 fb (ty, 683-tx)，横屏逻辑 (lx,ly) 经
// _rotate(LANDSCAPE) 恒等得 fb (lx,ly)，联立得 lx=ty、ly=683-tx。
// Landscape reading: remap CST836U native coords into landscape logical coords.
// The touch panel's physical frame (684x1216) coincides with the portrait
// (INVERTED_PORTRAIT) logical frame pixel-for-pixel, so raw coords double as
// logical coords in portrait; the landscape (LANDSCAPE) logical frame is
// 1216x684 and needs the 90-degree mapping, or raw x spans only 0..683, the
// right half (logical x>=684) is unreachable, and right-half hits carry
// out-of-range y that every hit test (ui_rect_hit / book_layout_hit_test /
// ui_nav_hit) silently drops. Derivation: one physical hit maps to one
// framebuffer pixel in both orientations - portrait logical (tx,ty) gives fb
// (ty, 683-tx) via epdiy _rotate(INVERTED_PORTRAIT), landscape logical (lx,ly)
// gives fb (lx,ly) via the identity _rotate(LANDSCAPE) - hence lx=ty, ly=683-tx.
// 例外：实体按键不在显示屏上，而在面板向下延伸的条纹区（实测原生 y≈1500，分界
// UI_KEY_AREA_TOP=1300，见 ui_menu.h 注释）。ui_key_hit_test() 按原生条纹坐标
// 识别按键（y>=1300 且 x<480 → x/160），两种方向语义相同——条纹点绝不能旋转：
// 旋转后 (80,1500) 会变成屏内 (1215,603)，三个键和菜单全部失效。条纹点原样放行，
// 横屏按键行为与竖屏 stock 逐字节一致（debounce 只拷贝不筛坐标，点击反馈/手势对
// 屏外点本就是竖屏存量行为，命中测试均有上界，屏外点不会误命中任何矩形）。
// Exception: the three physical keys are not on the display but in the panel
// strip below it (native y~1500; split at UI_KEY_AREA_TOP=1300, see ui_menu.h).
// ui_key_hit_test() reads them by native strip coords (y>=1300, x<480 -> x/160)
// in BOTH orientations, so strip points must pass through unrotated - rotating
// (80,1500) yields on-screen (1215,603) and kills all three keys plus the menu.
// Passing them through makes landscape keys byte-identical to portrait stock
// behavior (debounce only copies coords, click feedback/gestures already handle
// off-screen points in portrait, and every hit test is upper-bounded).
static void touch_native_to_logical(cst836u_touch_t* t) {
    if (epd_get_rotation() != EPD_ROT_LANDSCAPE) return;
    const int w = epd_width();   // 1216，横屏逻辑宽 / landscape logical width
    const int h = epd_height();  // 684，横屏逻辑高 / landscape logical height
    uint16_t nx = t->x, ny = t->y;
    if (ny < UI_KEY_AREA_TOP) {
        int lx = (int)ny, ly = h - 1 - (int)nx;
        t->x = (uint16_t)(lx < 0 ? 0 : lx > w - 1 ? w - 1 : lx);
        t->y = (uint16_t)(ly < 0 ? 0 : ly > h - 1 ? h - 1 : ly);
    }
    for (int i = 0; i < CST836U_MAX_POINTS; i++) {
        nx = t->points[i].x; ny = t->points[i].y;
        if (ny < UI_KEY_AREA_TOP) {
            int lx = (int)ny, ly = h - 1 - (int)nx;
            t->points[i].x = (uint16_t)(lx < 0 ? 0 : lx > w - 1 ? w - 1 : lx);
            t->points[i].y = (uint16_t)(ly < 0 ? 0 : ly > h - 1 ? h - 1 : ly);
        }
    }
}

// 空槽常带着抬起事件或残留坐标，不能进页面看到的快照；抬手后保留最后一次位置。
// Empty slots often carry lift events or stale coords; keep last position on lift.
static void debounce_touch(
    cst836u_touch_t* latest, const cst836u_touch_t* raw, bool released
) {
    if (raw->touched) {
        *latest = *raw;
        for (int i = 0; i < CST836U_MAX_POINTS; i++) {
            if (!latest->points[i].active) {
                memset(&latest->points[i], 0, sizeof(latest->points[i]));
            }
        }
    } else if (released) {
        latest->touched = false;
        latest->count = 0;
        for (int i = 0; i < CST836U_MAX_POINTS; i++) {
            latest->points[i].active = false;
        }
    }
    memcpy(latest->raw, raw->raw, sizeof(raw->raw));
}

static bool try_load_saved_sd_font(bool allow_probe) {
    read_pico_sd_info_t info;
    esp_err_t err = read_pico_sd_get_info(&info);
    if (err == ESP_ERR_NOT_FINISHED) return false;
    if (!info.mounted) {
        if (allow_probe && err == ESP_ERR_INVALID_STATE) read_pico_sd_start_probe();
        return false;
    }
    return app_font_retry_active();
}

// 先停文件使用者，再换字体；此处只观察挂载，不卸载也不重新挂载。
// Stop file consumers before font fallback; observe mounting without unmounting or remounting.
static bool poll_media(app_ctx_t* ctx, const app_desc_t* current,
                       bool* mounted, bool* invalidated) {
    if (usb_storage_active()) { *mounted = false; *invalidated = false; return false; }
    read_pico_sd_info_t info = {0};
    esp_err_t err = read_pico_sd_get_info(&info);
    if (err == ESP_ERR_INVALID_STATE && info.present && !*invalidated) {
        (void)read_pico_sd_start_probe();
        return false;
    }
    if (err == ESP_ERR_NOT_FINISHED) return false;
    bool ready = err == ESP_OK && info.mounted;
    bool lost = *mounted && !ready;
    bool gained = !*mounted && ready;
    *mounted = ready;
    if (ready) *invalidated = false;
    if (gained || lost) { ui_avatar_invalidate(); (void)book_cover_jobs_quiesce(0); book_lock_collage_invalidate(); }
    if (gained && current->on_media_ready) current->on_media_ready(ctx);
    if (!lost) return false;
    *invalidated = true;
    if (current->on_media_lost) current->on_media_lost(ctx);
    if (ttf_font_ready() && !ttf_font_is_builtin()) ttf_font_open_builtin();
    ui_text_set_system_font(false);
    return true;
}

// 电源动作先保存当前页面，再定稿一张不带时钟的静态画面；避免关机后留下会误读的冻结时间。
// Save the current page first, then settle a clock-free static frame so shutdown never leaves a misleading frozen time.
// 锁屏前先由页面保存进度，再持久化一次恢复上下文；不在翻页或点击时写入。
// Flush page progress before persisting one lock checkpoint; never write it on page turns or taps.
static void prepare_lock(app_ctx_t *ctx, const app_desc_t *current, bool feedback, bool manual) {
    // 物理横屏是阅读页私有状态（全工程运行时唯一的旋转切换点在 book render()）；
    // 进入任何系统画面（锁屏/壁纸/PIN/票根/关机/重启）前必须复位，否则竖屏资产
    // 会画在横屏物理屏上——真机实证：长按电源睡眠后壁纸横置、关机画面错位。
    // epd_set_rotation 只改状态不刷新，唯一一次刷新由各调用路径固有的 present /
    // 全刷完成，不会双重刷新。boot 解锁门不走本路径，由 init 竖屏 + 锁屏 art 的
    // rotation 校验自愈（ESP rail 被 PMU 掉电后完整重启）。
    // Landscape is the reader's private state (the only runtime rotation switch
    // lives in book render()); reset it before any system frame (lock/wallpaper/
    // PIN/ticket/shutdown/restart), or portrait assets land on a landscape panel
    // — observed on hardware: sideways sleep wallpaper after a long power press,
    // misaligned shutdown frame. epd_set_rotation only mutates state, so each
    // caller's inherent present/full refresh stays the single refresh. The boot
    // gate bypasses this path and self-heals via init portrait + the lock art's
    // rotation check (the PMU drops the ESP rail for a full restart).
    if (epd_get_rotation() == EPD_ROT_LANDSCAPE) epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);
    if (feedback) app_lock_feedback(ctx->hl);
    // gen21：手动电源键（息屏/休眠）拥有最高优先级——武装期绝不拦截，改为静默
    // 取消武装（安全关射频）后正常息屏；唤醒后状态卡回 IDLE/「未开启」，闭环成立。
    // 自动锁屏/深睡（manual=false）保持 gen18 豁免保活（M1.5 关屏保活），不在此取消，
    // 仍交 enter_lock_and_sleep 拒锁。cancel_why 在未 IDLE 时才打
    // 「armed canceled by manual sleep」，重复调用幂等静默。
    // gen21: a manual power-key lock outranks the push arm - never refuse, just
    // silently disarm (tear the radio down) then sleep; on wake the status card
    // returns to IDLE/"未开启", closing the loop. Auto lock (manual=false) keeps
    // the gen18 exemption (M1.5 screen-off keepalive) and is still refused by
    // enter_lock_and_sleep; cancel_why only logs "armed canceled by manual sleep"
    // while not IDLE, so re-calling is idempotent and silent.
    if (manual && push_sink_armed()) {
        push_sink_cancel_why("manual sleep");
    }
    if (current->on_before_lock) current->on_before_lock(ctx);
    int tab = app_index_of(current);
    pico_resume_t resume = {.tab = tab >= 0 && tab < 4 ? (uint8_t)tab : 0};
    bool fullscreen = false;
    if (tab == 1 && app_book_resume_context(resume.path, sizeof(resume.path), &fullscreen)) {
        resume.reader = 1;
        resume.fullscreen = fullscreen;
    }
    (void)pico_boot_save_resume(&resume);
}
static void run_power_action(app_ctx_t *ctx, const app_desc_t *current, bool restart) {
    prepare_lock(ctx, current, false, false);  // 关机/重启走 app_enter_host_sleep，深睡路径自带 cancel（gen18 ③），此处 manual=false
    ui_power_final_draw(ctx->fb, restart);
    guard_draw_result(ctx->hl, update_display_full(ctx->hl));
    app_lock_wait_key_idle(800);
    epd_poweroff();
    pico_boot_clear_resume();
    if (restart) app_restart_host();
    esp_err_t error = app_enter_host_sleep(APP_SLEEP_OFF);
    if (error != ESP_OK) ESP_LOGW(TAG, "power off failed: %s", esp_err_to_name(error));
    // 关机交接失败时回到可操作原页；不关闭书籍后再重新自动开书。/ A failed shutdown returns to the operable current page without closing/reopening the book.
    app_present(ctx, current, APP_REDRAW_PAGE);
}

bool app_poll_main_navigation(app_ctx_t *ctx) {
    if (!ctx || !ctx->tp) return false;
    if (ctx->request_app) return true;
    cst836u_touch_t touch = {0};
    if (cst836u_read(ctx->tp, &touch) != ESP_OK || !touch.touched || touch.count != 1) return false;
    int index = ui_nav_hit(touch.x, touch.y);
    if (index < 0 || index == 1) return false;
    ui_nav_request(ctx, index);
    ctx->user_activity = true;
    return ctx->request_app != NULL;
}

void app_loop_run(const app_loop_config_t* config) {
    s_presented_main = NULL;
    const app_desc_t* current = config->first_app != NULL
        ? config->first_app
        : app_home_page();

    cst836u_info_t touch_info = { 0 };
    cst836u_get_info(config->tp, &touch_info);
    cst836u_touch_t latest = { 0 };

    app_ctx_t ctx = {
        .hl = config->hl,
        .fb = config->fb,
        .acc = config->acc,
        .tp = config->tp,
        .sensor_ready = config->sensor_ready,
        .continuous_ready = continuous_du_init(),
        .touch = &latest,
        .touch_info = &touch_info,
    };

    bool was_touched = false;
    ui_quick_menu_reset(&s_quick);
    bool menu_open = false;
    ui_gesture_t gesture = {0};
    ui_power_dialog_t power_dialog = {0};
    bool power_dialog_open = false;
    int menu_pressed = UI_MENU_HIT_NONE;
    menu_feedback_t feedback = {0};
    int menu_leaf = 0;
    const app_desc_t* return_app = NULL;
    int return_leaf = 0, return_menu_leaf = 0;
    bool return_to_menu = false;
    int held_key = -1;
    int64_t held_since_ms = 0;
    int64_t last_lock_poll_ms = 0;
    int64_t last_font_retry_ms = 0;
    // #121: 字体重试闩锁——卡稳定挂载后退出周期重试，卡离位时重开（见主循环）。
    // #121: font retry latch - retires once the card stays mounted, rearms on removal (see the loop).
    bool font_retry_settled = false;
    int64_t last_media_poll_ms = 0;
    int64_t last_network_poll_ms = 0;
    int64_t last_input_ms = esp_timer_get_time() / 1000;
    uint32_t last_ble_input = ble_pt_input_serial();
    unsigned idle_minutes = app_settings_auto_lock_minutes();
    unsigned cover_policy = (unsigned)app_settings_main_refresh_mode() | ((unsigned)app_settings_system_contrast() << 8);
    read_pico_sd_info_t initial_media = {0};
    (void)read_pico_sd_get_info(&initial_media);
    bool media_mounted = initial_media.mounted || (ttf_font_ready() && !ttf_font_is_builtin());
    bool media_invalidated = false;
    app_lock_ignore_for(APP_LOCK_IGNORE_BOOT_MS);

    // The SD probe starts during board setup and is usually ready by the first product frame.
    // Retry here so the very first bookshelf paint already has a full CJK system face.
    try_load_saved_sd_font(false);
    rp_boot_mark("sdfont");
    if (current->on_enter != NULL) current->on_enter(&ctx);
    rp_boot_mark("enter");
    poll_media(&ctx, current, &media_mounted, &media_invalidated);
    // 首帧必须整屏 GC16：fb 里还是 app_main 画的开机图，DU 盖不掉。
    // First frame must be full GC16: fb still holds the splash; DU cannot cover it.
    app_present(&ctx, current, APP_REDRAW_FULL);
    rp_boot_mark("frame");
    pico_boot_ready();
    ESP_LOGI(TAG, "UI ready on %s", current->title);
    // 上次崩溃的现场（异常类型与触发地址）：有的话打一遍，串口和屏幕各有一份。
    // The previous panic scene, when there was one: logged here, and shown on the home screen.
    crash_note_log();
#ifdef KIIKO_LOCAL_LATENCY
    local_latency_init();
#endif

    while (true) {
        unsigned requested_cover_policy = (unsigned)app_settings_main_refresh_mode() | ((unsigned)app_settings_system_contrast() << 8);
        if (requested_cover_policy != cover_policy) {
            app_home_cover_mode_changed(); app_book_cover_mode_changed();
            cover_policy = requested_cover_policy;
        }
        cst836u_touch_t touch = { 0 };
        esp_err_t err = cst836u_read(config->tp, &touch);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "CST836U read failed: %s", esp_err_to_name(err));
            memset(&touch, 0, sizeof(touch));
        }
        // 横屏阅读补一次坐标映射；竖屏恒等跳过。必须在 debounce 之前，latest/ctx.touch
        // 全链路（手势、快捷菜单、ui_nav_hit、各 app 的 on_touch）才拿到逻辑坐标。
        // Remap once for landscape reading; portrait is a no-op. Must run before
        // debounce so latest/ctx.touch carry logical coords for the whole chain
        // (gestures, quick menu, ui_nav_hit, every app's on_touch).
        touch_native_to_logical(&touch);

        const bool pressed = touch.touched && !was_touched;
        const bool released = !touch.touched && was_touched;
        debounce_touch(&latest, &touch, released);
        ctx.now_ms = esp_timer_get_time() / 1000;
        if (err == ESP_OK && (touch.touched || released)) last_input_ms = ctx.now_ms;
        if (ctx.now_ms - last_network_poll_ms >= 500) {
            last_network_poll_ms = ctx.now_ms;
            read_pico_transfer_service_poll();
            app_time_service_poll(!touch.touched && ctx.now_ms - last_input_ms >= 10000 &&
                !current->holds_pmu && !usb_storage_active() && !pico_online_busy() && !ble_pt_connected());
        }
        ctx.pressed = pressed;
        ctx.released = released;
        ctx.consumed = false;
        // A load may have captured navigation after the prior switch. Consume it at the normal boundary.

        ctx.request_menu = false;
        ctx.request_return = false;
        bool selected_from_menu = false;
        const bool power_input_owned = power_dialog_open;
        bool quick_lock_requested = false;
        bool quick_input_owned = false;

        if (ctx.now_ms - last_media_poll_ms >= MEDIA_POLL_INTERVAL_MS) {
            last_media_poll_ms = ctx.now_ms;
            bool media_event = poll_media(&ctx, current, &media_mounted, &media_invalidated);
            // #121: 卡不在位（拔卡/ USB 存储/ 尚未挂载）即重开字体重试，重插后由下方
            // 重试块补载保存字体。必须放在 continue 之前：拔卡那一轮会整体跳过重试块。
            // #121: any card absence rearms the font retry so the block below reloads
            // the saved face after a reinsert. This must precede the continue - a loss
            // iteration skips the retry block entirely.
            if (!media_mounted) font_retry_settled = false;
            if (media_event) {
                power_dialog_open = false;
                ui_power_dialog_reset(&power_dialog, touch.touched);
                ui_quick_menu_reset(&s_quick);
                held_key = -1;
                cancel_gesture(&ctx, current, &gesture);
                menu_pressed = UI_MENU_HIT_NONE;
                ctx.consumed = true;
                ctx.pressed = ctx.released = false;
                was_touched = touch.touched;
                if (menu_open) present_menu(&ctx, current, menu_leaf, &feedback);
                else app_present(&ctx, current, APP_REDRAW_FULL);
                // 本轮旧输入不再触发动作，下一轮恢复正常事件处理。
                // Discard this tick's stale input, then resume normal dispatch next tick.
                vTaskDelay(pdMS_TO_TICKS(LOOP_TICK_MS));
                continue;
            }
        }

        if (!power_input_owned && !menu_open) {
            if (err != ESP_OK) ui_quick_menu_cancel_input(&s_quick);
            else {
                ui_quick_action_t action;
                quick_input_owned = ui_quick_menu_feed(&s_quick, &ctx,
                    current->render && !current->holds_pmu && !usb_storage_active() && !pico_online_busy(), &action);
                if (quick_input_owned) {
                    held_key = -1;
                    cancel_gesture(&ctx, current, &gesture);
                    ctx.consumed = true;
                }
                if (action == UI_QUICK_OPEN || action == UI_QUICK_CLOSE) quick_present(&ctx, current, false);
                else if (action == UI_QUICK_WIFI) {
                    quick_toggle_wifi();
                    quick_present(&ctx, current, false);
                } else if (action == UI_QUICK_BLUETOOTH) {
                    app_settings_set_ble_turner(!app_settings_ble_turner());
                    if (!app_settings_ble_turner()) ble_pt_reset_failure();
                    quick_present(&ctx, current, false);
                } else if (action == UI_QUICK_FULL) quick_present(&ctx, current, true);
                else if (action == UI_QUICK_LOCK) quick_lock_requested = true;
            }
        }

        if (power_input_owned && err == ESP_OK) {
            ctx.consumed = false;
            EpdRect dirty;
            ui_power_action_t action = ui_power_dialog_feed(&power_dialog, &ctx, &dirty);
            if (action == UI_POWER_ACTION_NONE && dirty.height > 0) {
                ui_power_dialog_draw_controls(ctx.fb, &power_dialog);
                enum EpdDrawError result = update_display_area_diff_with(
                    ctx.hl, &E0470_FOLLOW_WAVEFORM, MODE_DU, dirty);
                guard_draw_result(ctx.hl, result);
                if (result != EPD_DRAW_SUCCESS) {
                    power_dialog_open = false;
                    ui_power_dialog_reset(&power_dialog, touch.touched);
                    app_present(&ctx, current, APP_REDRAW_FULL);
                }
            } else {
                if (action == UI_POWER_ACTION_CANCEL) {
                    power_dialog_open = false;
                    ui_power_dialog_reset(&power_dialog, false);
                    if (menu_open) present_menu(&ctx, current, menu_leaf, &feedback);
                    else app_present(&ctx, current, APP_REDRAW_PAGE);
                } else if (action == UI_POWER_ACTION_SHUTDOWN) {
                    if (!app_settings_staged_shutdown()) {
                        power_dialog_open = false;
                        ui_power_dialog_reset(&power_dialog, false);
                        run_power_action(&ctx, current, false);  // 走 app_enter_host_sleep，深睡路径自带 cancel（"关机/深睡，接收结束"），此处 manual=false
                    } else {
                        power_dialog_open = false;
                        ui_power_dialog_reset(&power_dialog, false);
                        prepare_lock(&ctx, current, true, true);
                        extern const app_desc_t app_book;
                        main_lock_boundary();
                        enter_lock_and_sleep(ctx.hl, &s_lock_ignore_until_ms, ctx.acc, ctx.tp,
                                             current == &app_book && app_book_reader_body_visible());
                        display_request_full_refresh();
                        ctx.now_ms = esp_timer_get_time() / 1000;
                        last_input_ms = ctx.now_ms;
                        poll_media(&ctx, current, &media_mounted, &media_invalidated);
                        last_media_poll_ms = ctx.now_ms;
                        ctx.pressed = false;
                        ctx.released = false;
                        app_present(&ctx, current, APP_REDRAW_PAGE);
                    }
                } else if (action == UI_POWER_ACTION_RESTART) {
                    power_dialog_open = false;
                    ui_power_dialog_reset(&power_dialog, false);
                    run_power_action(&ctx, current, true);
                }
            }
            ctx.consumed = true;
        }

        if (err != ESP_OK) {
            held_key = -1;
            cancel_gesture(&ctx, current, &gesture);
            EpdRect dirty;
            ui_power_dialog_cancel_input(&power_dialog, &dirty);
            if (power_dialog_open && dirty.height > 0) {
                ui_power_dialog_draw_controls(ctx.fb, &power_dialog);
                guard_draw_result(ctx.hl, update_display_area_diff_with(
                    ctx.hl, &E0470_FOLLOW_WAVEFORM, MODE_DU, dirty));
            }
            ctx.consumed = true;
            if (menu_pressed >= 0) menu_feedback(&ctx, current, menu_leaf, menu_pressed, false, &feedback);
            menu_pressed = UI_MENU_HIT_NONE;
        }
        EpdRect click_area;
        if (ui_click_feedback_active() && (err != ESP_OK || released || touch.count != 1 ||
            quick_input_owned || power_input_owned || menu_open ||
            ui_click_feedback_cancel_at(touch.x, touch.y))) {
            if (ui_click_feedback_release(ctx.fb, &click_area)) click_present(&ctx, current, click_area);
        }
        // 主页面底栏在首次单指报点就确认；抬手、轻滑和停留不再重复切页。
        // Commit root navigation on the first single contact; lift, small motion and holds never repeat it.
        bool nav_press_owned = false;
        if (err == ESP_OK && pressed && touch.count == 1 && !power_input_owned &&
            !quick_input_owned && !menu_open && !usb_storage_active() && !ctx.request_app &&
            main_page_ready(&ctx, current)) {
            const int tab = ui_nav_hit(touch.x, touch.y);
            if (tab >= 0) {
                held_key = -1;
                cancel_gesture(&ctx, current, &gesture);
                ctx.consumed = true;
                ctx.user_activity = true;
                nav_press_owned = true;
                if (tab != app_index_of(current)) ui_nav_request(&ctx, tab);
            }
        }
        if (err == ESP_OK && pressed && touch.count == 1 && !power_input_owned &&
            !quick_input_owned && !menu_open && !usb_storage_active() &&
            ui_click_feedback_press(ctx.fb, touch.x, touch.y, &click_area))
            click_present(&ctx, current, click_area);
        if (!power_input_owned && !quick_input_owned && !nav_press_owned && pressed && err == ESP_OK) {
            held_key = -1;
            const int key = ui_key_hit_test(touch.x, touch.y);
            const bool handle_hit = ui_menu_handle_hit_test(touch.x, touch.y) &&
                (!current->menu_handle_enabled || current->menu_handle_enabled(&ctx));
            ctx.consumed = true;
            if (key >= 0 || handle_hit) {
                cancel_gesture(&ctx, current, &gesture);
                if (menu_pressed >= 0) menu_feedback(&ctx, current, menu_leaf, menu_pressed, false, &feedback);
                menu_pressed = UI_MENU_HIT_NONE;
            }
            if (key >= 0 && current->owns_keys && !menu_open) {
                app_redraw_t redraw = current->on_key && !(key == UI_KEY_2 && current->defer_middle_short)
                    ? current->on_key(&ctx, key) : APP_REDRAW_NONE;
                present_page(&ctx, current, &gesture, redraw);
                if (current->on_key_long && touch.count == 1) {
                    held_key = key;
                    held_since_ms = ctx.now_ms;
                }
            } else if (key == UI_KEY_2) {
                if (menu_open) {
                    feedback.updates = 0;
                    ui_draw_menu_page(ctx.fb, current, menu_leaf);
                    guard_draw_result(ctx.hl, update_display_full(ctx.hl));
                } else if (current->present != NULL && current->present(&ctx, APP_REDRAW_FULL)) {
                } else if (current->render != NULL) {
                    current->render(&ctx, ctx.fb);
                    guard_draw_result(ctx.hl, update_display_full(ctx.hl));
                }
            } else if ((key == UI_KEY_3 || handle_hit) && !usb_storage_active()) {
                menu_open = !menu_open;
                if (menu_open) {
                    menu_leaf = ui_menu_leaf_for_app(current);
                    present_menu(&ctx, current, menu_leaf, &feedback);
                } else app_present(&ctx, current, APP_REDRAW_PAGE);
            } else if (key >= 0 && menu_open) {
                int next = menu_leaf + (key == UI_KEY_1 ? -1 : 1);
                if (next >= 0 && next < ui_menu_leaf_count()) {
                    menu_leaf = next;
                    present_menu(&ctx, current, menu_leaf, &feedback);
                }
            } else if (key >= 0) {
                app_redraw_t redraw = current->on_key ? current->on_key(&ctx, key) : APP_REDRAW_NONE;
                if (redraw == APP_REDRAW_NONE)
                    ESP_LOGI(TAG, "KEY%d pressed, no action bound", key + 1);
                present_page(&ctx, current, &gesture, redraw);
            } else if (menu_open) {
                int hit = ui_menu_hit_test(touch.x, touch.y, menu_leaf);
                if (hit == UI_MENU_HIT_PREV || hit == UI_MENU_HIT_NEXT) {
                    menu_leaf += hit == UI_MENU_HIT_NEXT ? 1 : -1;
                    present_menu(&ctx, current, menu_leaf, &feedback);
                } else if (hit >= 0 && touch.count == 1) {
                    menu_pressed = hit;
                    menu_feedback(&ctx, current, menu_leaf, hit, true, &feedback);
                }
            } else if (current->on_gesture) {
                ctx.consumed = false;
            } else {
                // 旧页按下先收到 consumed=true，仅 NONE 为同轮 tick 放行。
                // Legacy press sees consumed=true; only NONE clears it for this tick.
                app_redraw_t redraw = current->on_touch
                    ? current->on_touch(&ctx, &latest) : APP_REDRAW_NONE;
                app_present(&ctx, current, redraw);
                if (redraw == APP_REDRAW_NONE) ctx.consumed = false;
            }
        }
        if (err == ESP_OK) was_touched = touch.touched;
        if (!power_input_owned && !quick_input_owned && menu_open && menu_pressed >= 0) {
            int hit = ui_menu_hit_test(latest.x, latest.y, menu_leaf);
            if (touch.count > 1 || hit != menu_pressed || released) {
                int chosen = menu_pressed;
                menu_feedback(&ctx, current, menu_leaf, chosen, false, &feedback);
                menu_pressed = UI_MENU_HIT_NONE;
                if (released && err == ESP_OK && hit == chosen) {
                    menu_open = false;
                    ctx.request_app = app_at(chosen);
                    selected_from_menu = true;
                    if (ctx.request_app == current) app_present(&ctx, current, APP_REDRAW_PAGE);
                }
            }
        }
        if (!power_input_owned && !quick_input_owned && !menu_open && current->on_gesture && err == ESP_OK) {
            ui_gesture_event_t event;
            if (ui_gesture_feed(&gesture, &ctx, &event)) {
                app_redraw_t redraw = current->on_gesture(&ctx, &event);
                present_page(&ctx, current, &gesture, redraw);
                if (redraw != APP_REDRAW_NONE) ctx.consumed = true;
            }
        }

        ctx.now_ms = esp_timer_get_time() / 1000;
        rails_idle_check(ctx.now_ms);

        // 电源键短按默认锁屏；阅读正文可选择将它用于下一页。
        // A short power press normally locks; the reader body may use it for the next page.
        if (quick_lock_requested || (read_pico_pmu_ready() && !current->holds_pmu && !usb_storage_active()
            && ctx.now_ms - last_lock_poll_ms >= APP_LOCK_POLL_MS)) {
            last_lock_poll_ms = ctx.now_ms;
            read_pico_pmu_key_action_t key_action = quick_lock_requested ? READ_PICO_PMU_KEY_SHORT : read_pico_pmu_take_key_action();
            if (key_action != READ_PICO_PMU_KEY_NONE) {
                last_input_ms = ctx.now_ms;
                bool quick_was_open = s_quick.open;
                if (quick_was_open && !quick_lock_requested) {
                    ui_quick_menu_reset(&s_quick);
                    quick_present(&ctx, current, false);
                }
                if (!quick_lock_requested && ctx.now_ms < s_lock_ignore_until_ms) {
                    ESP_LOGI(TAG, "ignore boot power-key action %d", (int)key_action);
                } else if (key_action == READ_PICO_PMU_KEY_LONG) {
                    held_key = -1;
                    cancel_gesture(&ctx, current, &gesture);
                    if (menu_pressed >= 0) menu_feedback(&ctx, current, menu_leaf, menu_pressed, false, &feedback);
                    menu_pressed = UI_MENU_HIT_NONE;
                    extern const app_desc_t app_book;
                    bool reader_power_lock = !menu_open && current == &app_book &&
                        app_book_reader_body_visible() && app_settings_reader_power_turn();
                    if (reader_power_lock) {
                        prepare_lock(&ctx, current, true, true);
                        main_lock_boundary();
                        enter_lock_and_sleep(ctx.hl, &s_lock_ignore_until_ms, ctx.acc, ctx.tp, true);
                        display_request_full_refresh();
                        ctx.now_ms = esp_timer_get_time() / 1000;
                        last_input_ms = ctx.now_ms;
                        poll_media(&ctx, current, &media_mounted, &media_invalidated);
                        last_media_poll_ms = ctx.now_ms;
                        ctx.consumed = true;
                        ctx.pressed = false;
                        ctx.released = false;
                        app_present(&ctx, current, APP_REDRAW_PAGE);
                    } else {
                        // 关机对话框是第 7 个系统画面，并入「系统画面一律竖屏」不变式：
                        // 横屏下必须先复位物理旋转。肖像几何的对话框在横屏物理屏上三重
                        // 失效（真机 round-2 实证）：按钮 Y=960 越出横屏 UI_LOCK_HEIGHT
                        // =684 不绘制；命中区肖像坐标触摸错位不可达；可见元素经肖像→
                        // 横屏旋转映射「没有转过来」。
                        // 仅内联 prepare_lock 的旋转复位一行，是 2026-10-15 用户裁决
                        // 选 A 的降级形态（降级理由入档）：prepare_lock 其余副作用为
                        // 终态转移设计，在可取消的对话框语境有害——app_device_settings
                        // 的 on_before_lock 会中止升级/停联机/停 BLE（阻塞至 2s）/关
                        // PIN 入口，取消后均不恢复；app_lock_feedback 需另传 false 才
                        // 能避开；resume 保存在确认路径 run_power_action→prepare_lock
                        // 会再做，取消路径不重启无需保存。横屏是阅读页私有状态（运行
                        // 时唯一旋转切换点在 book render()），本行对其它 app 空操作。
                        // 取消返回闭环已实证：取消走 app_present(APP_REDRAW_PAGE)→当前
                        // 视图 render() 自设旋转回横屏并整页重绘，无帧缓冲区域恢复路径，
                        // 像素/旋转/触摸一致。
                        // The shutdown dialog is the 7th system screen and joins the
                        // "system frames are portrait" invariant: under landscape the
                        // panel rotation must be reset first. On a landscape panel the
                        // portrait-geometry dialog fails three ways (observed on
                        // hardware, round 2): the button at Y=960 exceeds the landscape
                        // UI_LOCK_HEIGHT=684 so it is never drawn; the hit test uses
                        // portrait coordinates so touches miss; visible elements map
                        // through portrait→landscape and "do not turn".
                        // Only prepare_lock's rotation reset line is inlined - the
                        // downgraded form of option A per the 2026-10-15 ruling
                        // (rationale archived): its other side effects are terminal-
                        // state transfers by design and harmful in a cancelable dialog
                        // context - the settings app's on_before_lock aborts upgrades,
                        // cancels online joins, stops BLE (blocking up to 2s) and
                        // closes the PIN entry, none of which restore after a cancel;
                        // app_lock_feedback would need an extra false to avoid; the
                        // resume save is redundant because the confirm path
                        // run_power_action→prepare_lock saves again and a cancel never
                        // restarts. Landscape is the reader's private state (the only
                        // runtime rotation switch lives in book render()), so this
                        // line is a no-op for other apps.
                        // The cancel return path is verified closed: cancel goes
                        // through app_present(APP_REDRAW_PAGE) → the current view's
                        // render() sets rotation back to landscape and fully repaints;
                        // no framebuffer region-restore path exists, so pixels,
                        // rotation and touch agree.
                        if (epd_get_rotation() == EPD_ROT_LANDSCAPE) epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);
                        power_dialog_open = true;
                        ui_power_dialog_reset(&power_dialog, touch.touched);
                        ctx.request_app = NULL;
                        ctx.request_return = ctx.request_menu = false;
                        main_lock_boundary();
                        ui_text_set_system_scale(true);
                        ui_power_dialog_draw(ctx.fb, &power_dialog);
                        enum EpdDrawError result = update_display_area_with(
                            ctx.hl, &E0470_WAVEFORM, MODE_GL16, ui_power_dialog_rect());
                        guard_draw_result(ctx.hl, result);
                        if (result != EPD_DRAW_SUCCESS) {
                            power_dialog_open = false;
                            ui_power_dialog_reset(&power_dialog, touch.touched);
                            app_present(&ctx, current, APP_REDRAW_FULL);
                        }
                    }
                } else if (!power_dialog_open) {
                    held_key = -1;
                    cancel_gesture(&ctx, current, &gesture);
                    if (menu_pressed >= 0) menu_feedback(&ctx, current, menu_leaf, menu_pressed, false, &feedback);
                    menu_pressed = UI_MENU_HIT_NONE;
                    app_redraw_t power_redraw = !quick_lock_requested && !quick_was_open && !menu_open && current->on_power_short
                        ? current->on_power_short(&ctx) : APP_REDRAW_NONE;
                    if (power_redraw != APP_REDRAW_NONE) {
                        present_page(&ctx, current, &gesture, power_redraw);
                        ctx.consumed = true;
                    } else {
                        prepare_lock(&ctx, current, true, true);
                        extern const app_desc_t app_book;
                        main_lock_boundary();
                        enter_lock_and_sleep(ctx.hl, &s_lock_ignore_until_ms, ctx.acc, ctx.tp,
                                             !menu_open && current == &app_book && app_book_reader_body_visible());
                        display_request_full_refresh();
                        ctx.now_ms = esp_timer_get_time() / 1000;
                        last_input_ms = ctx.now_ms;
                        poll_media(&ctx, current, &media_mounted, &media_invalidated);
                        last_media_poll_ms = ctx.now_ms;
                        ctx.consumed = true;
                        ctx.pressed = false;
                        ctx.released = false;
                        // 醒来还在同一页，重画一次免得留着锁屏图。
                        // Still the same page; redraw so the lock image does not stay.
                        if (menu_open) present_menu(&ctx, current, menu_leaf, &feedback);
                        else app_present(&ctx, current, APP_REDRAW_PAGE);
                    }
                }
            }
        }

        // 设置里存的字体在 SD 卡上，开机时卡可能还没挂好，这里定期重试；
        // #121: 闩锁语义——卡稳定挂载且跑过一遍补载后退出周期重试（旧代码无退出
        // 条件，把会话内每次字体激活都变成对用户显式选择的强制回退，gen8 真机：
        // 选字体一秒后弹回默认）；卡离位时由上面的 media poll 重开，重插补载。
        // The saved font lives on the card; retry until the mount is ready.
        // #121: latch semantics - retire once the card stays mounted and one
        // catch-up pass has run (with no exit, every in-session activation became
        // a forced rollback of the user's explicit choice - gen8 hardware: the
        // font snapped back one second after a pick); the media poll above rearms
        // the retry while the card is absent, so a reinsert still reloads.
        if (!font_retry_settled && !power_dialog_open && !s_quick.open && !usb_storage_active() && current != &app_transfer &&
            ctx.now_ms - last_font_retry_ms >= FONT_RETRY_INTERVAL_MS) {
            last_font_retry_ms = ctx.now_ms;
            if (try_load_saved_sd_font(!media_invalidated)) {
                held_key = -1;
                cancel_gesture(&ctx, current, &gesture);
                ctx.consumed = true;
                menu_pressed = UI_MENU_HIT_NONE;
                if (menu_open) {
                    present_menu(&ctx, current, menu_leaf, &feedback);
                } else {
                    app_present(&ctx, current, APP_REDRAW_FULL);
                }
                ESP_LOGI(TAG, "TTF font ready");
            }
            // 卡挂载且未作废：这一遍就是补载，退出周期重试；卡再离位时上面重开。
            // Mounted and not invalidated: that pass was the catch-up - retire; the line above rearms on removal.
            if (media_mounted && !media_invalidated) font_retry_settled = true;
        }

        if (!power_dialog_open && released && held_key == UI_KEY_2 && ui_key_hit_test(latest.x, latest.y) == UI_KEY_2 &&
            current->defer_middle_short && !menu_open &&
            !ctx.request_app && !ctx.request_menu && !ctx.request_return) {
            int key = held_key;
            held_key = -1;
            ctx.consumed = true;
            app_redraw_t redraw = ctx.now_ms - held_since_ms >= UI_LONG_PRESS_MS
                ? current->on_key_long(&ctx, key) : current->on_key(&ctx, key);
            present_page(&ctx, current, &gesture, redraw);
        }
        if (!power_dialog_open && held_key >= 0) {
            if (menu_open || ctx.request_app || ctx.request_menu || ctx.request_return || !touch.touched ||
                touch.count != 1 || ui_key_hit_test(touch.x, touch.y) != held_key ||
                ctx.now_ms < held_since_ms) {
                held_key = -1;
            } else if (!pressed && ctx.now_ms - held_since_ms >= UI_LONG_PRESS_MS) {
                int key = held_key;
                held_key = -1;
                ctx.consumed = true;
                present_page(&ctx, current, &gesture, current->on_key_long(&ctx, key));
            }
        }
        // 蓝牙翻页器：BLE 栈只在这里跟随设置启停。必须走 ble_pt_sync()——自己写
        // 「设置与状态不一致就 start()」会让启动失败的那次判断每轮都成立，变成每秒
        // 几十次重复初始化并把内存耗光。sync() 内部有内存闸门、失败退避和崩溃自锁。
        // Bluetooth page-turner: the BLE stack follows the setting only here, and only through
        // ble_pt_sync(). Rolling our own "state differs from setting, so start()" makes a failed
        // start true on every tick, re-initialising dozens of times a second until memory runs
        // out; sync() carries the memory gate, the failure back-off and the crash-loop lock.
        // WiFi 在 C6、BLE 在 S3；传输仍与 BLE 竞争 S3 内部内存，网络占用先拆 BLE，结束后恢复。
        // WiFi runs on C6 and BLE on S3; both consume S3 internal memory, so network leases pause BLE and release it afterwards.
        read_pico_transfer_status_t transfer;
        read_pico_transfer_get_status(&transfer);
        const bool wifi_busy = transfer.state != READ_PICO_TRANSFER_STOPPED ||
            pico_online_busy();
        // 飞行模式下蓝牙同样停摆；解除后按原设置自动恢复。
        // Flight mode halts Bluetooth too; clearing it restores whatever the setting asks for.
        // 推流武装期同理要栈：手机在等一条推送，武装入口本身可能就是翻页器
        // 设置关着时点的——BLE 栈必须为推流保持运行（on_sync 会注册 GATT 服务）。
        // An open push arm needs the stack for the same reason: the phone is
        // waiting for a push, and the arm entry may have been tapped with the
        // page-turner setting off - the BLE stack must stay up for the push
        // (on_sync registers the GATT services).
        ble_pt_sync((app_settings_ble_turner() || push_sink_armed()) && !wifi_busy && !app_settings_flight_mode());
        ble_pt_poll();
        // 推流轮询：驱动窗口/宽限到期、拔电看守、待武装超时。放在 poll 之后，
        // 翻页器让位闸（ble_pt_poll 内）先执行，射频先归推流。
        // Push poll: window/grace expiry, the unplug watch and the pending-arm
        // timeout. It runs after ble_pt_poll so the page-turner's yield gate
        // (inside that poll) hands the radio over first.
        push_sink_poll();
        if (s_quick.open || quick_input_owned) {
            // 覆盖层期间丢弃页内蓝牙输入，关闭后不重放翻页或学习事件。
            // Discard page BLE input while covered, without replaying turns or learning edges after closing.
            ble_pt_event_t key_event; ble_pt_raw_t raw_event;
            while (ble_pt_pop_key(&key_event)) {}
            while (ble_pt_pop_raw(&raw_event)) {}
        }
        if (s_quick.open && quick_status() != s_quick_status) quick_present(&ctx, current, false);
        if (!power_dialog_open && !s_quick.open && !quick_input_owned && !ui_click_feedback_active() && !menu_open && !ctx.request_app && !ctx.request_menu && !ctx.request_return && current->on_tick != NULL) {
            app_redraw_t redraw = current->on_tick(&ctx);
            if (redraw == APP_REDRAW_PAGE || redraw == APP_REDRAW_FULL) held_key = -1;
            present_page(&ctx, current, &gesture, redraw);
        }
        if (ctx.user_activity) { last_input_ms = ctx.now_ms; ctx.user_activity = false; }
        uint32_t ble_input = ble_pt_input_serial();
        if (ble_input != last_ble_input) { last_ble_input = ble_input; last_input_ms = ctx.now_ms; }
        unsigned requested_idle = app_settings_auto_lock_minutes();
        // 武装期不锁屏：深睡没有 BLE 唤醒源，睡下去这条推送就收不到了，而用户
        // 此刻正是在等它。/ No locking while a push arm is open: deep sleep has
        // no BLE wake source, so a sleep would lose the push the user is
        // waiting for.
        bool idle_blocked = usb_storage_active() || wifi_busy || current == &app_transfer ||
            current->holds_pmu || power_dialog_open || touch.touched ||
            ctx.request_app || ctx.request_menu || ctx.request_return || push_sink_armed();
        if (idle_blocked || requested_idle != idle_minutes) last_input_ms = ctx.now_ms;
        idle_minutes = requested_idle;
        if (!idle_blocked && ctx.now_ms >= s_lock_ignore_until_ms &&
            auto_lock_due(ctx.now_ms, last_input_ms, idle_minutes)) {
            held_key = -1;
            cancel_gesture(&ctx, current, &gesture);
            if (menu_pressed >= 0) menu_feedback(&ctx, current, menu_leaf, menu_pressed, false, &feedback);
            menu_pressed = UI_MENU_HIT_NONE;
            prepare_lock(&ctx, current, false, false);
            extern const app_desc_t app_book;
            main_lock_boundary();
            enter_lock_and_sleep(ctx.hl, &s_lock_ignore_until_ms, ctx.acc, ctx.tp,
                                 !menu_open && current == &app_book && app_book_reader_body_visible());
            display_request_full_refresh();
            ctx.now_ms = esp_timer_get_time() / 1000;
            last_input_ms = ctx.now_ms;
            poll_media(&ctx, current, &media_mounted, &media_invalidated);
            last_media_poll_ms = ctx.now_ms;
            ctx.pressed = ctx.released = false;
            if (menu_open) present_menu(&ctx, current, menu_leaf, &feedback);
            else app_present(&ctx, current, APP_REDRAW_PAGE);
        }
        if (menu_open && feedback.updates > 0
            && ctx.now_ms - feedback.last_ms >= UI_SETTLE_IDLE_MS) {
            guard_draw_result(ctx.hl, update_display_area_with(
                ctx.hl, &E0470_WAVEFORM, MODE_GL16, feedback.area));
            feedback.updates = 0;
        }
        if (!menu_open) feedback.updates = 0;
#ifdef KIIKO_LOCAL_LATENCY
        static int64_t probe_heartbeat;
        if(ctx.now_ms-probe_heartbeat>5000){probe_heartbeat=ctx.now_ms;ESP_LOGI("latency","STATE touch=%d menu=%d dialog=%d usb=%d",touch.touched,menu_open,power_dialog_open,usb_storage_active());}
        if(!menu_open&&!power_dialog_open&&!usb_storage_active()&&!touch.touched) {
            app_redraw_t probe=local_latency_poll(&ctx,current);
            if(probe!=APP_REDRAW_NONE)app_present(&ctx,current,probe);
        }
#endif
        // 返回来源优先；普通请求仍按切页优先处理。
        // Return requests win; ordinary requests still prioritize page switches.
        // USB MSC 持有 TF 卡时禁止任何页面切换；必须先安全停止并重新挂载。
        // Never navigate while USB MSC owns the card; stop and remount it first.
        if (usb_storage_active()) {
            ctx.request_return = false;
            ctx.request_app = NULL;
            ctx.request_menu = false;
        }
        if (!power_dialog_open && (ctx.request_return || ctx.request_app || ctx.request_menu)) {
            if (ui_click_feedback_release(ctx.fb, &click_area)) click_present(&ctx, current, click_area);
            ui_quick_menu_reset(&s_quick);
            held_key = -1;
            const app_desc_t* next = ctx.request_app;
            const bool go_back = ctx.request_return;
            cancel_gesture(&ctx, current, &gesture);
            menu_pressed = UI_MENU_HIT_NONE;
            ctx.request_app = NULL;
            ctx.request_menu = false;
            ctx.request_return = false;
            ctx.consumed = true;
            if (go_back) {
                bool main_source = main_page_ready(&ctx, current);
                bool shelf_source = main_source && app_index_of(current) == 1;
                display_main_transition_cancel();
                menu_open = return_app ? return_to_menu : true;
                menu_leaf = return_app ? return_menu_leaf : ui_menu_leaf_for_app(current);
                if (return_app) {
                    if (current->on_exit) current->on_exit(&ctx);
                    current = return_app;
                    return_app = NULL;
                    app_font_activate_system();
                    if (current->on_enter) current->on_enter(&ctx);
                    // 初始化后恢复通用页码，避免 on_enter 的默认页码覆盖来源。
                    // Restore the shared leaf after initialization overrides its default.
                    ctx.leaf = return_leaf;
                }
                if (menu_open) present_menu(&ctx, current, menu_leaf, &feedback);
                else {
                    if (main_source && main_page_ready(&ctx, current)) display_main_transition_arm(current, shelf_source, true);
                    app_present(&ctx, current, APP_REDRAW_PAGE);
                    display_main_transition_disarm();
                }
            } else if (next) {
                if (next != current) {
                    return_app = current;
                    return_leaf = ctx.leaf;
                    return_to_menu = selected_from_menu;
                    return_menu_leaf = menu_leaf;
                }
                menu_open = false;
                switch_to(&ctx, &current, next);

            } else {
                menu_open = true;
                menu_leaf = ui_menu_leaf_for_app(current);
                present_menu(&ctx, current, menu_leaf, &feedback);
            }
        }
        // 连续列表相位期间缩短等待；每次仍读触摸，并给其他任务让出一个调度周期。
        // Reduce waits during continuous list phases; poll touch each time and yield one scheduling tick.
        vTaskDelay(pdMS_TO_TICKS(LOOP_TICK_MS));
    }
}
