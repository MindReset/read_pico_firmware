/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 连续 DU 状态机：每像素剩余相位存在 PSRAM，每轮只扫跟随波形第 0 帧。
 *
 * Continuous DU state machine: leftover phases live in PSRAM; each
 * scan emits only phase 0 of the follow waveform.
 */

#include "continuous_du.h"

#include "read_pico_epd_timing.h"

#include <stddef.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "e0470_epaper_waveform.h"

static const char* TAG = "continuous_du";

// 每像素剩余相位：正数还要压黑几次，负数还要擦白几次，0 表示不驱动。
// / Per-pixel leftover: +N more darken, −N more erase, 0 = do not drive.
static int8_t* s_states = NULL;
static int s_pending = 0;
static int s_dark = CONTINUOUS_DARK_PHASES;
static int s_light = CONTINUOUS_LIGHT_PHASES;

// 单相位 DU：LUT 用跟随波形第 0 帧。difference 只用
// 0x0F / 0xF0 / 0x88，0x88 落在 dest==from 的保持格上，仍是空操作。
// / One-phase DU: LUT is follow-waveform frame 0. difference only uses
// 0x0F / 0xF0 / 0x88; 0x88 lands on dest==from hold and is a no-op.
static uint8_t continuous_du_lut[1][16][4];
static const int continuous_du_times[1] = { 120 };

static const EpdWaveformPhases continuous_du_phases = {
    .phases = 1,
    .phase_times = continuous_du_times,
    .luts = (const uint8_t*)&continuous_du_lut[0],
};

static const EpdWaveformPhases* continuous_du_ranges[] = {
    &continuous_du_phases,
};

// type 1 = MODE_DU。/ type 1 = MODE_DU.
static const EpdWaveformMode continuous_du_mode = {
    .type = 1,
    .temp_ranges = 1,
    .range_data = &continuous_du_ranges[0],
};

static const EpdWaveformMode* continuous_du_modes[] = {
    &continuous_du_mode,
};

static const EpdWaveformTempInterval continuous_du_intervals[] = {
    { .min = 0, .max = 50 },
};

static const EpdWaveform continuous_du_waveform = {
    .num_modes = 1,
    .num_temp_ranges = 1,
    .mode_data = continuous_du_modes,
    .temp_intervals = continuous_du_intervals,
};

int continuous_du_dark_phases(void) {
    return s_dark;
}

int continuous_du_light_phases(void) {
    return s_light;
}

bool continuous_du_init(void) {
    if (s_states != NULL) return true;
    // 单帧表 = 1 帧的跟随公式：只要 to != from 就推一下，方向看 to 在 from 的哪一侧。
    // / One-frame table = follow formula at 1 frame: push whenever to != from;
    // direction is which side of from that to sits on.
    e0470_follow_lut_build(1, continuous_du_lut);
    size_t bytes = (size_t)epd_width() * epd_height();
    s_states = heap_caps_calloc(bytes, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_states == NULL) {
        ESP_LOGE(TAG, "state buffer allocation failed: %u bytes", (unsigned)bytes);
        return false;
    }
    s_pending = 0;
    return true;
}

void continuous_du_deinit(void) {
    heap_caps_free(s_states);
    s_states = NULL;
    s_pending = 0;
}

void continuous_du_reset(void) {
    if (s_states == NULL) return;
    memset(s_states, 0, (size_t)epd_width() * epd_height());
    s_pending = 0;
}

bool continuous_du_busy(void) {
    return s_pending > 0;
}

void continuous_du_from_logical(int lx, int ly, int* px, int* py) {
    // 与 epdiy 内部 _rotate() 的 EPD_ROT_INVERTED_PORTRAIT 分支一致：轴交换后
    // 再把行号翻转。其他旋转下 UI 布局本身就不一样，这里不做处理。
    // / Matches epdiy _rotate() EPD_ROT_INVERTED_PORTRAIT: swap axes, then flip
    // the row. Other rotations have a different UI layout; ignore them here.
    *px = ly;
    *py = epd_height() - lx - 1;
}

EpdRect continuous_du_rect_from_logical(EpdRect logical) {
    // 逻辑 x 跨度落到物理 y 上并翻转，逻辑 y 跨度落到物理 x 上，宽高互换。
    // / Logical x span lands on physical y and is flipped; logical y span
    // lands on physical x; width and height swap.
    return (EpdRect){
        .x = logical.y,
        .y = epd_height() - logical.x - logical.width,
        .width = logical.height,
        .height = logical.width,
    };
}

static int fb_nibble(const uint8_t* fb, int width, int x, int y) {
    const uint8_t byte = fb[y * (width / 2) + x / 2];
    return (x & 1) ? (byte >> 4) : (byte & 0x0F);
}

static void mark_pixel(int8_t* cell, int phases) {
    if (phases == 0) return;
    if (*cell == 0) s_pending++;
    *cell = (int8_t)phases;
}

void continuous_du_mark_diff(
    const uint8_t* to, const uint8_t* from, EpdRect area, int phases, bool invert
) {
    if (s_states == NULL || to == NULL || from == NULL || phases == 0) return;

    const int width = epd_width();
    const int height = epd_height();
    int x0 = area.x;
    int y0 = area.y;
    int x1 = area.x + area.width - 1;
    int y1 = area.y + area.height - 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > width - 1) x1 = width - 1;
    if (y1 > height - 1) y1 = height - 1;
    if (x1 < x0 || y1 < y0) return;

    for (int y = y0; y <= y1; y++) {
        int8_t* row = s_states + (size_t)y * width;
        for (int x = x0; x <= x1; x++) {
            const int dest = fb_nibble(to, width, x, y);
            const int src = fb_nibble(from, width, x, y);
            if (dest == src) continue;
            const bool to_black = dest < 8;
            int dir = to_black ? phases : -phases;
            if (invert) dir = -dir;
            mark_pixel(&row[x], dir);
        }
    }
}

void continuous_du_mark_rect(EpdRect area, int phases) {
    if (s_states == NULL || phases == 0) return;

    const int width = epd_width();
    const int height = epd_height();
    int x0 = area.x;
    int y0 = area.y;
    int x1 = area.x + area.width - 1;
    int y1 = area.y + area.height - 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > width - 1) x1 = width - 1;
    if (y1 > height - 1) y1 = height - 1;

    for (int y = y0; y <= y1; y++) {
        int8_t* row = s_states + (size_t)y * width;
        for (int x = x0; x <= x1; x++) {
            if (row[x] == 0) s_pending++;
            row[x] = (int8_t)phases;
        }
    }
}

void continuous_du_mark_circle(int cx, int cy, int radius, int phases) {
    if (s_states == NULL || phases == 0) return;

    const int width = epd_width();
    const int height = epd_height();
    int x0 = cx - radius;
    int x1 = cx + radius;
    int y0 = cy - radius;
    int y1 = cy + radius;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > width - 1) x1 = width - 1;
    if (y1 > height - 1) y1 = height - 1;

    const int radius_squared = radius * radius;
    for (int y = y0; y <= y1; y++) {
        const int dy = y - cy;
        int8_t* row = s_states + (size_t)y * width;
        for (int x = x0; x <= x1; x++) {
            const int dx = x - cx;
            if (dx * dx + dy * dy > radius_squared) continue;
            if (row[x] == 0) s_pending++;
            // 反方向途中的像素直接重置：不等它走完，立刻朝新目标给足相位。
            // / Reset a pixel already going the other way: do not wait it out;
            // give a full count toward the new dest.
            row[x] = (int8_t)phases;
        }
    }
}

enum EpdDrawError continuous_du_scan(EpdiyHighlevelState* hl, EpdRect area) {
    if (s_states == NULL) return EPD_DRAW_SUCCESS;

    const int width = epd_width();
    const int height = epd_height();
    int x0 = area.x;
    int y0 = area.y;
    int x1 = area.x + area.width - 1;
    int y1 = area.y + area.height - 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > width - 1) x1 = width - 1;
    if (y1 > height - 1) y1 = height - 1;
    if (x1 < x0 || y1 < y0) return EPD_DRAW_SUCCESS;

    uint8_t* difference = hl->difference_fb;
    memset(hl->dirty_lines, 0, sizeof(bool) * height);
    memset(hl->dirty_columns, 0, width / 2);

    bool any = false;
    for (int y = y0; y <= y1; y++) {
        const size_t offset = (size_t)y * width;
        const int8_t* state_row = s_states + offset;
        uint8_t* diff_row = difference + offset;
        for (int x = x0; x <= x1; x++) {
            const int8_t state = state_row[x];
            // difference 的编码是 (目标灰阶 << 4) | 原灰阶。0x0F 是 15->0 压黑，
            // 0xF0 是 0->15 擦白；0x88 落在 LUT 的中间灰度行上，等于不驱动。
            // / difference is (dest gray << 4) | src gray. 0x0F is 15→0 darken,
            // 0xF0 is 0→15 erase; 0x88 hits a mid-gray LUT row and does not drive.
            diff_row[x] = state > 0 ? 0x0F : (state < 0 ? 0xF0 : 0x88);
            if (state != 0) {
                hl->dirty_lines[y] = true;
                hl->dirty_columns[x / 2] |= (x & 1) ? 0xF0 : 0x0F;
                any = true;
            }
        }
    }
    if (!any) return EPD_DRAW_SUCCESS;

    // 这条路每次只发一个 DU 相位，相位数就是黑度，帧周期越短越顺手；同档重复调是空操作。
    // / This path emits one DU phase per call; phase count is darkness, shorter
    // frames feel better. Same-profile calls are no-ops.
    read_pico_epd_use_scan(READ_PICO_EPD_SCAN_FAST);

    enum EpdDrawError err = epd_draw_base(
        epd_full_screen(),
        difference,
        epd_full_screen(),
        MODE_PACKING_1PPB_DIFFERENCE | MODE_DU,
        25,
        hl->dirty_lines,
        hl->dirty_columns,
        &continuous_du_waveform
    );
    if (err != EPD_DRAW_SUCCESS) return err;

    for (int y = y0; y <= y1; y++) {
        int8_t* state_row = s_states + (size_t)y * width;
        for (int x = x0; x <= x1; x++) {
            int8_t* state = &state_row[x];
            if (*state > 0) {
                if (--*state == 0) s_pending--;
            } else if (*state < 0) {
                if (++*state == 0) s_pending--;
            }
        }
    }
    return EPD_DRAW_SUCCESS;
}
