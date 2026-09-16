/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 睡眠与唤醒。试浅睡看按键/拿起；深睡和关机从本页下电。
 * 三项锁屏选择写入 NVS，电源键锁屏时由 sleep.c 执行。
 *
 * Sleep and wake. Trial light sleep for key/pickup; deep and off power down
 * from this page. The three lock choices go to NVS; sleep.c runs them on
 * a power-key lock.
 *
 * 冻结：底栏试睡 / 读取走 ui_bar_rect；拿起默认关；浅睡 350mg/3 拍；
 * 说明栏随模式变；KEY3 走主循环菜单把手。
 * Frozen: try-sleep / read use ui_bar_rect; pickup defaults off; light sleep
 * is 350mg / 3 samples; description follows the mode. KEY3 is the loop menu handle.
 */

#include "app.h"
#include "display.h"
#include "esp_log.h"
#include "read_pico_pmu.h"
#include "settings.h"
#include "sleep.h"
#include "ui_kit.h"
#include "ui_menu.h"

extern const app_desc_t app_sleep;

#define TAG "app_sleep"

#define SLEEP_TITLE "睡眠与唤醒 Sleep"
#define SLEEP_POLL_MS 1500
#define SLEEP_CHIP_H 56
#define SLEEP_CHIP_GAP 8
#define SLEEP_CHIP_COLS 4

#define SLEEP_HIT_NONE (-1)
#define SLEEP_HIT_LIGHT 0
#define SLEEP_HIT_DEEP 1
#define SLEEP_HIT_OFF 2
#define SLEEP_HIT_TRY 10
#define SLEEP_HIT_READ 11
#define SLEEP_HIT_PICKUP 12

typedef struct {
    int wake_y;
    int chip_y;
    int lock_y;
    int mode_y;
    int desc_y;
    EpdRect pickup;
    EpdRect try_btn;
    EpdRect read_btn;
} sleep_geom_t;

typedef struct {
    bool pickup_ok;
    uint8_t wake_reason;
    uint8_t alarm_mode;
    uint8_t wake_on_charge;
    bool pmu_ok;
} sleep_ui_t;

static sleep_ui_t s_ui;
static int64_t s_last_poll_ms;

static const char* const k_mode_btn[] = {
    "浅睡 Lightsleep", "深睡 Deepsleep", "关机 Off",
};
static const int k_mode_ids[] = {
    SLEEP_HIT_LIGHT, SLEEP_HIT_DEEP, SLEEP_HIT_OFF,
};
static const char* const k_chip_btn[] = {
    "按键 Key", "拿起 Pickup", "闹钟 Alarm", "来电开机 AC On",
};
static const char* const k_bar_btn[] = { "试睡 Try", "读取 Read" };

static EpdRect sleep_chip_rect(int i, int y) {
    const int w = (ui_content_width() - (SLEEP_CHIP_COLS - 1) * SLEEP_CHIP_GAP)
        / SLEEP_CHIP_COLS;
    return (EpdRect){
        .x = UI_MARGIN + i * (w + SLEEP_CHIP_GAP),
        .y = y,
        .width = w,
        .height = SLEEP_CHIP_H,
    };
}

static sleep_geom_t sleep_geom(void) {
    const int wake_y = UI_CONTENT_TOP;
    const int chip_y = wake_y + UI_SEC_HEAD + 2 * UI_ROW_H_SM + UI_GAP;
    const int lock_y = chip_y + SLEEP_CHIP_H + UI_SECTION_GAP;
    const int mode_y = lock_y + UI_SEC_HEAD;
    return (sleep_geom_t){
        .wake_y = wake_y,
        .chip_y = chip_y,
        .lock_y = lock_y,
        .mode_y = mode_y,
        .desc_y = mode_y + UI_BTN_H + UI_SECTION_GAP,
        .pickup = sleep_chip_rect(1, chip_y),
        .try_btn = ui_bar_rect(0, 2),
        .read_btn = ui_bar_rect(1, 2),
    };
}

static void sleep_draw_btn(uint8_t* framebuffer, EpdRect rect, const char* label, bool on) {
    ui_draw_choice_round_rect(framebuffer, rect, UI_BTN_RADIUS, on);
    ui_text_vc(
        framebuffer, rect.x + rect.width / 2, rect.y + rect.height / 2,
        UI_PX_LABEL_SM, label, EPD_DRAW_ALIGN_CENTER, false
    );
}

static void sleep_draw_grid(
    uint8_t* framebuffer, int cols, int y0, const char* const* labels, const bool* on
) {
    for (int i = 0; i < cols; i++) {
        sleep_draw_btn(
            framebuffer, ui_grid_rect(i, cols, 0, y0, UI_BTN_H),
            labels[i], on != NULL && on[i]
        );
    }
}

static bool sleep_pickup_on(void) {
    return s_ui.pickup_ok && app_settings_pickup_wake();
}

static const char* trial_text(app_wake_source_t src) {
    switch (src) {
        case APP_WAKE_KEY: return "按键 Key";
        case APP_WAKE_PICKUP: return "拿起 Pickup";
        default: return "-";
    }
}

static const char* boot_text(uint8_t reason) {
    static const char* const t[] = {
        "未知 Unknown",
        "通过冷启动 By Cold",
        "通过按键 By Key",
        "通过主机 By Host",
        "通过看门狗 By WDT",
        "通过来电开机 By AC On",
        "通过闹钟 By Alarm",
    };
    if (reason < sizeof(t) / sizeof(t[0])) return t[reason];
    return "其它 Other";
}

static void sleep_mode_note(app_sleep_mode_t mode, bool pickup, const char** zh, const char** en) {
    switch (mode) {
        case APP_SLEEP_DEEP:
            *zh = "断电，再短按开机。";
            *en = "Power off. Short-press to boot.";
            break;
        case APP_SLEEP_OFF:
            *zh = "断电，长按约 1 秒开机。";
            *en = "Power off. Long-press about 1 s to boot.";
            break;
        case APP_SLEEP_LIGHT:
        default:
            *zh = pickup ? "短按或拿起，回到当前页。" : "短按电源键，回到当前页。";
            *en = pickup ? "Short-press or pick up to resume."
                         : "Short-press the power key to resume.";
            break;
    }
}

static void sleep_notice_text(app_sleep_mode_t mode, const char** zh, const char** en) {
    switch (mode) {
        case APP_SLEEP_DEEP:
            *zh = "已深度睡眠";
            *en = "Deepsleep";
            break;
        case APP_SLEEP_OFF:
            *zh = "已关机";
            *en = "Power Off";
            break;
        case APP_SLEEP_LIGHT:
        default:
            *zh = "已浅度睡眠";
            *en = "Lightsleep";
            break;
    }
}

static void draw_wake_chips(uint8_t* framebuffer, int y) {
    const bool on[] = {
        true,
        sleep_pickup_on(),
        s_ui.pmu_ok && s_ui.alarm_mode != 0,
        s_ui.pmu_ok && s_ui.wake_on_charge != 0,
    };
    for (int i = 0; i < SLEEP_CHIP_COLS; i++) {
        ui_draw_chip(framebuffer, sleep_chip_rect(i, y), k_chip_btn[i], on[i]);
    }
}

static uint8_t shown_boot(void) {
    if (s_ui.pmu_ok && s_ui.wake_reason != 0) return s_ui.wake_reason;
    return app_settings_last_boot();
}

static const char* last_text(void) {
    app_wake_source_t src = app_last_wake_source();
    if (src != APP_WAKE_NONE) return trial_text(src);
    uint8_t boot = shown_boot();
    return boot != 0 ? boot_text(boot) : "-";
}

static void sleep_sync_pmu(app_ctx_t* ctx) {
    const pmu_snapshot_t* s = read_pico_pmu_get();
    s_ui.pmu_ok = s->present && s->status_ok;
    s_ui.wake_reason = s->wake_reason;
    s_ui.alarm_mode = s->alarm_mode;
    s_ui.wake_on_charge = s->config.wake_on_charge;
    s_ui.pickup_ok = ctx->sensor_ready && ctx->acc != NULL;
    if (s_ui.pmu_ok) app_settings_set_last_boot(s_ui.wake_reason);
}

static void sleep_reload(app_ctx_t* ctx) {
    read_pico_pmu_refresh();
    sleep_sync_pmu(ctx);
    s_last_poll_ms = ctx->now_ms;
}

static bool sleep_ui_same(const sleep_ui_t* a, const sleep_ui_t* b) {
    return a->wake_reason == b->wake_reason && a->alarm_mode == b->alarm_mode
        && a->wake_on_charge == b->wake_on_charge && a->pmu_ok == b->pmu_ok;
}

// 试睡先整屏定稿，避免客人把静止的演示页当成死机。/ Settle the whole screen before a trial sleep so a still demo page is not taken for a hang.
static void sleep_show_notice(app_ctx_t* ctx, app_sleep_mode_t mode) {
    const char* zh;
    const char* en;
    sleep_notice_text(mode, &zh, &en);
    ui_clear_page(ctx->fb);
    const int cx = epd_rotated_display_width() / 2;
    const int gap = 16;
    const int block = UI_PX_TITLE + gap + UI_PX_BODY;
    const int y0 = (epd_rotated_display_height() - block) / 2;
    ui_text(ctx->fb, cx, y0, UI_PX_TITLE, zh, EPD_DRAW_ALIGN_CENTER, false);
    ui_text(
        ctx->fb, cx, y0 + UI_PX_TITLE + gap, UI_PX_BODY, en,
        EPD_DRAW_ALIGN_CENTER, false
    );
    guard_draw_result(ctx->hl, update_display_full(ctx->hl));
}

static void draw_sleep_page(uint8_t* framebuffer) {
    const sleep_geom_t g = sleep_geom();
    const app_sleep_mode_t mode = app_settings_sleep_mode();
    const bool pickup = sleep_pickup_on();
    const char* note_zh;
    const char* note_en;
    sleep_mode_note(mode, pickup, &note_zh, &note_en);

    ui_clear_page(framebuffer);
    ui_draw_header(
        framebuffer, SLEEP_TITLE,
        s_ui.pmu_ok ? app_sleep.detail : "CW32 无应答 No reply"
    );

    uint8_t boot = shown_boot();
    int y = ui_draw_section(framebuffer, g.wake_y, "唤醒源 Wake");
    y = ui_draw_row2(
        framebuffer, y, "上次 Last", last_text(),
        "开机 Boot", boot != 0 ? boot_text(boot) : "-"
    );
    ui_draw_row2(
        framebuffer, y, "闹钟 Alarm",
        s_ui.pmu_ok && s_ui.alarm_mode != 0 ? "已设 Armed" : "关 Off",
        "来电开机 AC On",
        s_ui.pmu_ok && s_ui.wake_on_charge != 0 ? "开 On" : "关 Off"
    );
    draw_wake_chips(framebuffer, g.chip_y);

    ui_draw_section(framebuffer, g.lock_y, "锁屏后 Lock");
    bool mode_on[] = {
        mode == APP_SLEEP_LIGHT,
        mode == APP_SLEEP_DEEP,
        mode == APP_SLEEP_OFF,
    };
    sleep_draw_grid(framebuffer, 3, g.mode_y, k_mode_btn, mode_on);

    int y_desc = ui_draw_section(framebuffer, g.desc_y, "说明 Description");
    ui_text(
        framebuffer, UI_MARGIN, y_desc, UI_PX_BODY, note_zh,
        EPD_DRAW_ALIGN_LEFT, false
    );
    ui_text(
        framebuffer, UI_MARGIN, y_desc + UI_PX_BODY + 10, UI_PX_CAPTION, note_en,
        EPD_DRAW_ALIGN_LEFT, false
    );

    ui_draw_button(framebuffer, g.try_btn, k_bar_btn[0], false);
    ui_draw_button(framebuffer, g.read_btn, k_bar_btn[1], false);
    ui_draw_menu_handle(framebuffer, false);
}

static int sleep_hit_test(const sleep_geom_t* g, uint16_t x, uint16_t y) {
    if (ui_rect_hit(g->pickup, x, y)) return SLEEP_HIT_PICKUP;
    int hit = ui_grid_hit(x, y, 3, 1, g->mode_y, UI_BTN_H, k_mode_ids);
    if (hit >= 0) return hit;
    if (ui_rect_hit(g->try_btn, x, y)) return SLEEP_HIT_TRY;
    if (ui_rect_hit(g->read_btn, x, y)) return SLEEP_HIT_READ;
    return SLEEP_HIT_NONE;
}

static void sleep_try(app_ctx_t* ctx) {
    app_sleep_mode_t mode = app_settings_sleep_mode();
    sleep_show_notice(ctx, mode);
    app_lock_wait_key_idle(800);
    if (mode == APP_SLEEP_LIGHT) {
        epd_poweroff();
        app_light_sleep_wait(s_ui.pickup_ok ? ctx->acc : NULL);
        app_lock_ignore_for(APP_LOCK_IGNORE_BOOT_MS);
        return;
    }
    app_enter_host_sleep(mode);
}

static app_redraw_t sleep_on_hit(app_ctx_t* ctx, int hit) {
    switch (hit) {
        case SLEEP_HIT_LIGHT:
        case SLEEP_HIT_DEEP:
        case SLEEP_HIT_OFF:
            app_settings_set_sleep_mode((app_sleep_mode_t)hit);
            ESP_LOGI(TAG, "lock mode %s", app_sleep_mode_name((app_sleep_mode_t)hit));
            return APP_REDRAW_PAGE;
        case SLEEP_HIT_PICKUP:
            if (!s_ui.pickup_ok) return APP_REDRAW_NONE;
            app_settings_set_pickup_wake(!app_settings_pickup_wake());
            ESP_LOGI(TAG, "pickup wake %s", sleep_pickup_on() ? "on" : "off");
            return APP_REDRAW_PAGE;
        case SLEEP_HIT_READ:
            sleep_reload(ctx);
            return APP_REDRAW_PAGE;
        case SLEEP_HIT_TRY:
            sleep_try(ctx);
            sleep_reload(ctx);
            return APP_REDRAW_FULL;
        default:
            return APP_REDRAW_NONE;
    }
}

static void on_enter(app_ctx_t* ctx) {
    sleep_reload(ctx);
}

static void render(app_ctx_t* ctx, uint8_t* fb) {
    (void)ctx;
    draw_sleep_page(fb);
}

static EpdRect area_hint(app_ctx_t* ctx) {
    (void)ctx;
    return ui_content_refresh_area();
}

static app_redraw_t on_touch(app_ctx_t* ctx, const cst836u_touch_t* touch) {
    const sleep_geom_t g = sleep_geom();
    return sleep_on_hit(ctx, sleep_hit_test(&g, touch->x, touch->y));
}

static app_redraw_t on_tick(app_ctx_t* ctx) {
    if (ctx->now_ms - s_last_poll_ms < SLEEP_POLL_MS) return APP_REDRAW_NONE;
    s_last_poll_ms = ctx->now_ms;
    if (read_pico_pmu_poll() != ESP_OK) return APP_REDRAW_NONE;
    sleep_ui_t prev = s_ui;
    sleep_sync_pmu(ctx);
    if (sleep_ui_same(&s_ui, &prev)) return APP_REDRAW_NONE;
    draw_sleep_page(ctx->fb);
    return APP_REDRAW_AREA;
}

const app_desc_t app_sleep = {
    .title = SLEEP_TITLE,
    .detail = "试睡与锁屏 Try Sleep & Lock",
    .enter_full = true,
    .render = render,
    .on_enter = on_enter,
    .on_touch = on_touch,
    .on_tick = on_tick,
    .area_hint = area_hint,
};
