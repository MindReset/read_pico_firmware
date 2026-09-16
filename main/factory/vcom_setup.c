/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 出厂 VCOM 标定。PMU 未标定时拦住开机；数字区跟随 DU，三位凑齐进确认页再写。
 *
 * Factory VCOM calibration. Gates boot when the PMU is unset; the number band
 * uses FOLLOW DU; three digits go to the confirm page, then write.
 *
 * 冻结：副标题不写范围；数字只刷上方一块 FOLLOW DU；三位合法自动进确认；
 * 确认页 168px 大号 + mV；写入失败留在确认页；不写 ESP NVS。
 * Frozen: subtitle has no range; the number band uses FOLLOW DU only;
 * three valid digits go to confirm automatically; confirm page is 168px plus mV;
 * write failure stays on confirm; do not write ESP NVS.
 */

#include "vcom_setup.h"

#include <stdio.h>
#include <string.h>

#include "display.h"
#include "e0470_epaper_waveform.h"
#include "epdiy.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "read_pico_pmu.h"
#include "ui_kit.h"

#define TAG "vcom_setup"

#define VCOM_MIN_MV 500
#define VCOM_MAX_MV 2500
#define VCOM_PAD_ROWS 4
#define VCOM_PAD_COLS 3
#define VCOM_CONFIRM_PX 168
#define VCOM_KEY_OK 11

#define VCOM_HIT_NONE (-1)
#define VCOM_HIT_DEL 10
#define VCOM_HIT_OK 11
#define VCOM_HIT_WRITE 12
#define VCOM_HIT_BACK 13

typedef struct {
    int hero_y;
    int pad_y;
    int pad_h;
    EpdRect num;
    EpdRect write;
    EpdRect back;
} vcom_geom_t;

typedef struct {
    char digits[4];
    int mv;
    bool confirm;
    bool ok_on;
} vcom_state_t;

static const char* const k_keys[12] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9",
    "删除 Del", "0", "确定 OK",
};
static const int k_pad_ids[12] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, VCOM_HIT_DEL, 0, VCOM_HIT_OK,
};

static vcom_geom_t vcom_geom(void) {
    const int hero_y = UI_CONTENT_TOP + 8;
    const int pad_y = UI_CONTENT_TOP + UI_PX_HERO + 72;
    const int bottom = UI_LOCK_HEIGHT - UI_MARGIN;
    const int pad_h =
        (bottom - pad_y - (VCOM_PAD_ROWS - 1) * UI_GAP) / VCOM_PAD_ROWS;
    return (vcom_geom_t){
        .hero_y = hero_y,
        .pad_y = pad_y,
        .pad_h = pad_h,
        .num = {
            .x = UI_MARGIN - 8,
            .y = hero_y - 8,
            .width = ui_content_width() + 16,
            .height = UI_PX_HERO + 40,
        },
        .write = ui_row_rect(0, 2, UI_BAR_TOP, UI_BAR_H),
        .back = ui_row_rect(1, 2, UI_BAR_TOP, UI_BAR_H),
    };
}

static EpdRect vcom_key_rect(const vcom_geom_t* g, int i) {
    return ui_grid_rect(
        i % VCOM_PAD_COLS, VCOM_PAD_COLS, i / VCOM_PAD_COLS, g->pad_y, g->pad_h
    );
}

static int vcom_typed_mv(const char* digits) {
    int typed = 0;
    if (digits == NULL) return 0;
    for (const char* p = digits; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') return 0;
        typed = typed * 10 + (*p - '0');
    }
    return typed * 10;
}

static void vcom_fmt_volt(char* out, size_t n, int mv) {
    snprintf(out, n, "-%d.%02d V", mv / 1000, (mv % 1000) / 10);
}

static bool vcom_digits_valid(const char* digits, int* out_mv) {
    if (digits == NULL || digits[0] == '\0') return false;
    int n = 0;
    for (const char* p = digits; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') return false;
        n++;
        if (n > 3) return false;
    }
    int mv = vcom_typed_mv(digits);
    if (mv < VCOM_MIN_MV || mv > VCOM_MAX_MV) return false;
    if (out_mv != NULL) *out_mv = mv;
    return true;
}

static bool vcom_push_digit(char* digits, int d) {
    size_t n = strlen(digits);
    if (n >= 3) return false;
    digits[n] = (char)('0' + d);
    digits[n + 1] = '\0';
    return true;
}

static bool vcom_pop_digit(char* digits) {
    size_t n = strlen(digits);
    if (n == 0) return false;
    digits[n - 1] = '\0';
    return true;
}

static void draw_number(uint8_t* fb, const vcom_geom_t* g, const char* digits) {
    char shown[16];
    if (digits == NULL) digits = "";
    if (digits[0] == '\0') {
        snprintf(shown, sizeof(shown), "- —.—— V");
    } else {
        vcom_fmt_volt(shown, sizeof(shown), vcom_typed_mv(digits));
    }
    ui_clear_rect_fast(fb, g->num);
    ui_draw_hero(fb, g->hero_y, "公共电压 Panel", shown);
}

static void present_fast(EpdiyHighlevelState* hl, EpdRect area) {
    guard_draw_result(
        hl, update_display_area_with(hl, &E0470_FOLLOW_WAVEFORM, MODE_DU, area)
    );
}

static void present_setup(EpdiyHighlevelState* hl) {
    guard_draw_result(hl, update_display_mode(hl, MODE_GC16));
}

static void present_confirm(EpdiyHighlevelState* hl) {
    guard_draw_result(hl, update_display_full(hl));
}

static void sync_ok(
    uint8_t* fb, EpdiyHighlevelState* hl, const vcom_geom_t* g, bool on, bool* drawn
) {
    if (on == *drawn) return;
    *drawn = on;
    EpdRect rect = vcom_key_rect(g, VCOM_KEY_OK);
    ui_clear_rect_fast(fb, rect);
    ui_draw_button(fb, rect, k_keys[VCOM_KEY_OK], on);
    present_fast(hl, rect);
}

static void draw_setup(uint8_t* fb, const vcom_geom_t* g, const vcom_state_t* s) {
    ui_clear_page(fb);
    ui_draw_header(fb, "设定 VCOM Set", "三位 129 即 -1.29 V。");
    draw_number(fb, g, s->digits);
    for (int i = 0; i < 12; i++) {
        ui_draw_button(fb, vcom_key_rect(g, i), k_keys[i], i == VCOM_KEY_OK && s->ok_on);
    }
}

static void draw_confirm(uint8_t* fb, const vcom_geom_t* g, int mv) {
    char volt[16];
    char milli[24];
    vcom_fmt_volt(volt, sizeof(volt), mv);
    snprintf(milli, sizeof(milli), "%d mV", mv);

    ui_clear_page(fb);
    ui_draw_header(fb, "确认 VCOM Confirm", "请核对大号数值，再写入。");
    const int cx = epd_rotated_display_width() / 2;
    const int y0 = UI_CONTENT_TOP + 80;
    ui_text(fb, cx, y0, VCOM_CONFIRM_PX, volt, EPD_DRAW_ALIGN_CENTER, false);
    ui_text(
        fb, cx, y0 + VCOM_CONFIRM_PX + 24, UI_PX_TITLE, milli, EPD_DRAW_ALIGN_CENTER, false
    );
    ui_draw_button(fb, g->write, "写入 Write", false);
    ui_draw_button(fb, g->back, "改回 Back", false);
}

static void show_setup(
    EpdiyHighlevelState* hl, uint8_t* fb, const vcom_geom_t* g, vcom_state_t* s
) {
    s->ok_on = vcom_digits_valid(s->digits, NULL);
    draw_setup(fb, g, s);
    present_setup(hl);
}

static void show_confirm(
    EpdiyHighlevelState* hl, uint8_t* fb, const vcom_geom_t* g, int mv
) {
    draw_confirm(fb, g, mv);
    present_confirm(hl);
}

static int vcom_hit(const vcom_geom_t* g, bool confirm, uint16_t x, uint16_t y) {
    if (confirm) {
        if (ui_rect_hit(g->write, x, y)) return VCOM_HIT_WRITE;
        if (ui_rect_hit(g->back, x, y)) return VCOM_HIT_BACK;
        return VCOM_HIT_NONE;
    }
    return ui_grid_hit(
        x, y, VCOM_PAD_COLS, VCOM_PAD_ROWS, g->pad_y, g->pad_h, k_pad_ids
    );
}

static void wait_up(cst836u_handle_t tp) {
    while (true) {
        cst836u_touch_t up;
        if (cst836u_read(tp, &up) == ESP_OK && !up.touched) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void eat_touch(cst836u_handle_t tp, bool* was_touched) {
    wait_up(tp);
    *was_touched = false;
}

static bool persist(int mv) {
    if (read_pico_pmu_vcom_set(mv) != ESP_OK) return false;
    int got = 0;
    if (read_pico_pmu_vcom_get(&got) != ESP_OK || got != mv) return false;
    epd_set_vcom((uint16_t)mv);
    return true;
}

static void after_digits(
    EpdiyHighlevelState* hl, uint8_t* fb, const vcom_geom_t* g, vcom_state_t* s
) {
    draw_number(fb, g, s->digits);
    present_fast(hl, g->num);
    bool ok = vcom_digits_valid(s->digits, &s->mv);
    if (strlen(s->digits) == 3 && ok) {
        s->confirm = true;
        return;
    }
    sync_ok(fb, hl, g, ok, &s->ok_on);
}

void vcom_setup_run(EpdiyHighlevelState* hl, uint8_t* fb, cst836u_handle_t tp) {
    const vcom_geom_t g = vcom_geom();
    vcom_state_t s = { 0 };

    show_setup(hl, fb, &g, &s);

    bool was_touched = false;
    for (;;) {
        cst836u_touch_t touch;
        if (cst836u_read(tp, &touch) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        bool pressed = touch.touched && !was_touched;
        was_touched = touch.touched;
        if (!pressed) {
            vTaskDelay(pdMS_TO_TICKS(8));
            continue;
        }

        int hit = vcom_hit(&g, s.confirm, touch.x, touch.y);
        if (s.confirm) {
            if (hit == VCOM_HIT_BACK) {
                s.confirm = false;
                show_setup(hl, fb, &g, &s);
                eat_touch(tp, &was_touched);
            } else if (hit == VCOM_HIT_WRITE) {
                if (persist(s.mv)) {
                    ESP_LOGI(TAG, "panel VCOM -%d mV", s.mv);
                    wait_up(tp);
                    return;
                }
                ESP_LOGE(TAG, "VCOM_SET failed");
                eat_touch(tp, &was_touched);
            }
            continue;
        }

        bool changed = false;
        if (hit >= 0 && hit <= 9) {
            changed = vcom_push_digit(s.digits, hit);
        } else if (hit == VCOM_HIT_DEL) {
            changed = vcom_pop_digit(s.digits);
        } else if (hit == VCOM_HIT_OK && vcom_digits_valid(s.digits, &s.mv)) {
            s.confirm = true;
        } else {
            continue;
        }

        if (changed) after_digits(hl, fb, &g, &s);
        if (s.confirm) {
            eat_touch(tp, &was_touched);
            show_confirm(hl, fb, &g, s.mv);
        }
    }
}
