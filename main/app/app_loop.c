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
 * Page state and refresh cadence stay in main/apps/.
 */

#include "app_loop.h"

#include <string.h>

#include "app_registry.h"
#include "continuous_du.h"
#include "display.h"
#include "e0470_epaper_waveform.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "read_pico_board.h"
#include "read_pico_pmu.h"
#include "read_pico_sd.h"
#include "settings.h"
#include "sleep.h"
#include "ttf_font.h"
#include "ui_kit.h"
#include "ui_menu.h"

#define TAG "app_loop"

// 触摸芯片报点率远高于屏幕，主循环空转一轮只等这么久。
// Touch reports far faster than the panel; idle this long per loop.
#define LOOP_TICK_MS 5
// SD 卡插上或者刚挂好时，隔一会儿再试一次存在设置里的字体。
// Retry the saved SD font shortly after a card appears or mounts.
#define FONT_RETRY_INTERVAL_MS 3000

static int64_t s_lock_ignore_until_ms;

void app_lock_ignore_for(int64_t ms) {
    s_lock_ignore_until_ms = esp_timer_get_time() / 1000 + ms;
}

void app_present(app_ctx_t* ctx, const app_desc_t* app, app_redraw_t redraw) {
    enum EpdDrawError result = EPD_DRAW_SUCCESS;
    if (app->present != NULL && app->present(ctx, redraw)) return;
    switch (redraw) {
        case APP_REDRAW_NONE:
        case APP_REDRAW_DONE:
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
            result = update_display_mode(ctx->hl, APP_PAGE_REFRESH_MODE);
            break;
    }
    guard_draw_result(ctx->hl, result);
}

// 切页：先让上一页收尾，再给新页一次 on_enter，最后整页画出来。
// Leave the old page, enter the new one, then present a full page.
static void switch_to(
    app_ctx_t* ctx, const app_desc_t** current, const app_desc_t* next
) {
    if (next == NULL || next == *current) return;
    if ((*current)->on_exit != NULL) (*current)->on_exit(ctx);
    *current = next;
    if (next->on_enter != NULL) next->on_enter(ctx);
    app_present(ctx, next, next->enter_full ? APP_REDRAW_FULL : APP_REDRAW_PAGE);
    ESP_LOGI(TAG, "page -> %s", next->title);
}

// 菜单页不是 app，单独画。/ The menu is not an app; present it here.
static void present_menu(app_ctx_t* ctx, const app_desc_t* current, int leaf) {
    if (display_take_white_exit()) {
        guard_draw_result(ctx->hl, update_display_white(ctx->hl));
    }
    ui_draw_menu_page(ctx->fb, current, leaf);
    guard_draw_result(ctx->hl, update_display_mode(ctx->hl, APP_PAGE_REFRESH_MODE));
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

static bool try_load_saved_sd_font(void) {
    const char* path = app_settings_font_path();
    if (ttf_font_path_is_builtin(path)) return false;
    if (ttf_font_ready() && !ttf_font_is_builtin()
        && strcmp(ttf_font_path(), path) == 0) {
        return false;
    }

    read_pico_sd_info_t info;
    esp_err_t err = read_pico_sd_get_info(&info);
    if (err == ESP_ERR_NOT_FINISHED) return false;
    if (!info.mounted) {
        if (err == ESP_ERR_INVALID_STATE) read_pico_sd_start_probe();
        return false;
    }
    return ttf_font_open(path) == ESP_OK && !ttf_font_is_builtin();
}

void app_loop_run(const app_loop_config_t* config) {
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
    bool menu_open = false;
    int menu_leaf = 0;
    int64_t last_lock_poll_ms = 0;
    int64_t last_font_retry_ms = 0;
    app_lock_ignore_for(APP_LOCK_IGNORE_BOOT_MS);

    if (current->on_enter != NULL) current->on_enter(&ctx);
    // 首帧必须整屏 GC16：fb 里还是 app_main 画的开机图，DU 盖不掉。
    // First frame must be full GC16: fb still holds the splash; DU cannot cover it.
    app_present(&ctx, current, APP_REDRAW_FULL);
    ESP_LOGI(TAG, "UI ready on %s", current->title);

    while (true) {
        cst836u_touch_t touch = { 0 };
        esp_err_t err = cst836u_read(config->tp, &touch);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "CST836U read failed: %s", esp_err_to_name(err));
            memset(&touch, 0, sizeof(touch));
        }

        const bool pressed = touch.touched && !was_touched;
        const bool released = !touch.touched && was_touched;
        debounce_touch(&latest, &touch, released);
        ctx.now_ms = esp_timer_get_time() / 1000;
        ctx.pressed = pressed;
        ctx.released = released;
        ctx.consumed = false;
        ctx.request_app = NULL;

        if (pressed) {
            const int key = ui_key_hit_test(touch.x, touch.y);
            const bool handle_hit = ui_menu_handle_hit_test(touch.x, touch.y);
            ctx.consumed = true;

            if (key == UI_KEY_2) {
                // KEY2 是万能出路：整屏 GC16 重画当前页，用来清残影。
                // KEY2 always full-GC16 redraws the current page to clear ghosting.
                // KEY2 always full-GC16 redraws the current page to clear ghosting.
                if (menu_open) {
                    ui_draw_menu_page(ctx.fb, current, menu_leaf);
                    guard_draw_result(ctx.hl, update_display_full(ctx.hl));
                } else if (current->present != NULL
                           && current->present(&ctx, APP_REDRAW_FULL)) {
                } else if (current->render != NULL) {
                    current->render(&ctx, ctx.fb);
                    guard_draw_result(ctx.hl, update_display_full(ctx.hl));
                }
            } else if (key == UI_KEY_3 || handle_hit) {
                // KEY3 和右下角菜单把手同一条路：开菜单 / 收菜单。
                // KEY3 and the bottom-right handle share one path: open/close the menu.
                // KEY3 and the bottom-right handle share one path: open/close the menu.
                menu_open = !menu_open;
                if (menu_open) {
                    menu_leaf = ui_menu_leaf_for_app(current);
                    present_menu(&ctx, current, menu_leaf);
                } else {
                    app_present(&ctx, current, APP_REDRAW_PAGE);
                }
            } else if (key >= 0 && menu_open) {
                int next = menu_leaf + (key == UI_KEY_1 ? -1 : 1);
                if (next >= 0 && next < ui_menu_leaf_count()) {
                    menu_leaf = next;
                    present_menu(&ctx, current, menu_leaf);
                }
            } else if (key >= 0) {
                app_redraw_t redraw = current->on_key != NULL
                    ? current->on_key(&ctx, key)
                    : APP_REDRAW_NONE;
                if (redraw == APP_REDRAW_NONE) {
                    ESP_LOGI(TAG, "KEY%d pressed, no action bound", key + 1);
                }
                app_present(&ctx, current, redraw);
            } else if (menu_open) {
                int hit = ui_menu_hit_test(touch.x, touch.y, menu_leaf);
                if (hit == UI_MENU_HIT_PREV || hit == UI_MENU_HIT_NEXT) {
                    menu_leaf += hit == UI_MENU_HIT_NEXT ? 1 : -1;
                    present_menu(&ctx, current, menu_leaf);
                } else if (hit >= 0) {
                    menu_open = false;
                    const app_desc_t* next = app_at(hit);
                    if (next == NULL || next == current) {
                        app_present(&ctx, current, APP_REDRAW_PAGE);
                    } else {
                        switch_to(&ctx, &current, next);
                    }
                }
            } else {
                app_redraw_t redraw = current->on_touch != NULL
                    ? current->on_touch(&ctx, &latest)
                    : APP_REDRAW_NONE;
                app_present(&ctx, current, redraw);
                if (redraw == APP_REDRAW_NONE) ctx.consumed = false;
            }
        }
        was_touched = touch.touched;

        if (ctx.request_app != NULL) {
            menu_open = false;
            switch_to(&ctx, &current, ctx.request_app);
            ctx.request_app = NULL;
            ctx.consumed = true;
        }

        ctx.now_ms = esp_timer_get_time() / 1000;
        rails_idle_check(ctx.now_ms);

        // 电源键短按 = 锁屏。自检页要连着占用 PMU，这时不抢它的事件队列。
        // Short power-key press locks. A page that holds the PMU keeps its event queue.
        // Short power-key press locks. A page that holds the PMU keeps its event queue.
        if (read_pico_pmu_ready() && !current->holds_pmu
            && ctx.now_ms - last_lock_poll_ms >= APP_LOCK_POLL_MS) {
            last_lock_poll_ms = ctx.now_ms;
            if (read_pico_pmu_take_key_short()) {
                if (ctx.now_ms < s_lock_ignore_until_ms) {
                    ESP_LOGI(TAG, "ignore boot KEY_SHORT");
                } else {
                    enter_lock_and_sleep(ctx.hl, &s_lock_ignore_until_ms, ctx.acc);
                    // 醒来还在同一页，重画一次免得留着锁屏图。
                    // Still the same page; redraw so the lock image does not stay.
                    // Still the same page; redraw so the lock image does not stay.
                    app_present(&ctx, current, APP_REDRAW_PAGE);
                }
            }
        }

        // 设置里存的字体在 SD 卡上，开机时卡可能还没挂好，这里定期重试。
        // The saved font lives on the card; retry until the mount is ready.
        // The saved font lives on the card; retry until the mount is ready.
        if (ctx.now_ms - last_font_retry_ms >= FONT_RETRY_INTERVAL_MS) {
            last_font_retry_ms = ctx.now_ms;
            if (try_load_saved_sd_font()) {
                if (menu_open) {
                    present_menu(&ctx, current, menu_leaf);
                } else {
                    app_present(&ctx, current, APP_REDRAW_FULL);
                }
                ESP_LOGI(TAG, "TTF font ready");
            }
        }

        if (!menu_open && current->on_tick != NULL) {
            app_present(&ctx, current, current->on_tick(&ctx));
        }
        vTaskDelay(pdMS_TO_TICKS(LOOP_TICK_MS));
    }
}
