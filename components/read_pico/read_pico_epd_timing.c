/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 按像素钟解出行/帧扫描参数。
 *
 * Solve line/frame scan parameters from the pixel clock.
 */

#include "read_pico_epd_timing.h"

#include "e0470_epaper_waveform.h"
#include "epdiy.h"
#include "esp_log.h"

#define TAG "read_pico"

// 规格给的消隐下界：行起始 ≥0.3µs、行后肩 ≥0.8µs、行结束 ≥4 钟。
// / Spec blanking floors: line start ≥0.3 µs, back porch ≥0.8 µs, line end ≥4 clocks.
#define LINE_START_MIN_NS 300
#define LINE_BACK_MIN_NS 800
#define LINE_END_MIN_CLK 4

// 帧消隐：起始 1 行、后肩固定 4 行、结束 12 行。
// / Frame blanking: 1 start line, 4-line back porch, 12 end lines.
#define FRAME_START_LINES 1
#define FRAME_BACK_LINES 4
#define FRAME_END_LINES 12

// 栅极开门必须严格短于一行。厂家表 LGONL≈13.4µs，低电平至少留 1.2µs。
// / Gate open must be strictly shorter than one line. Vendor LGONL≈13.4 µs; leave ≥1.2 µs low.
#define CKV_LOW_MIN_01US 12
#define CKV_HIGH_MAX_01US 134

static int s_frame_target_us = E0470_WAVEFORM_FRAME_US;
static int s_ckv_high_max_01us = CKV_HIGH_MAX_01US;
static int s_pclk_mhz;
static read_pico_epd_scan_profile_t s_profile = READ_PICO_EPD_SCAN_FULL;

int read_pico_epd_frame_target_us(void) { return s_frame_target_us; }

void read_pico_epd_set_frame_target_us(int us) { s_frame_target_us = us > 0 ? us : 0; }

int read_pico_epd_ckv_high_max_01us(void) { return s_ckv_high_max_01us; }

void read_pico_epd_set_ckv_high_max_01us(int v) {
    if (v >= CKV_LOW_MIN_01US) s_ckv_high_max_01us = v;
}

static int clampi(int v, int lo, int hi) {
    if (v > hi) v = hi;
    if (v < lo) v = lo;
    return v;
}

static int ceil_div(int n, int d) { return (n + d - 1) / d; }

// ceil(ns × f / 1000)：把一段最小时间换成 f MHz 下的整数钟。
// / ceil(ns × f / 1000): minimum time to integer clocks at f MHz.
static int clocks_for_ns(int ns, int pclk_mhz) { return ceil_div(ns * pclk_mhz, 1000); }

static LcdLineTiming_t scan_to_line(const read_pico_epd_scan_t* s) {
    return (LcdLineTiming_t){
        .le_high_time = s->line_start,
        .line_front_porch = s->line_back_porch,
        .line_end = s->line_end,
        .ckv_high_time = s->ckv_high_01us,
    };
}

static void apply_lcd(const read_pico_epd_scan_t* scan) {
    LcdLineTiming_t line = scan_to_line(scan);
    epd_lcd_set_line_timing(&line);
    epd_set_lcd_pixel_clock_MHz(scan->pclk_mhz);
}

void read_pico_epd_scan(
    int pclk_mhz, read_pico_epd_scan_profile_t profile, read_pico_epd_scan_t* out
) {
    if (out == NULL) return;
    pclk_mhz = clampi(pclk_mhz, READ_PICO_EPD_PCLK_MIN_MHZ, READ_PICO_EPD_PCLK_MAX_MHZ);

    const EpdDisplay_t* display = epd_get_display();
    const int p = display->bus_width / 2;  // 每像素 2bit，一钟推满整条总线 / 2 bits/pixel, one clock fills the bus
    const int ldl = display->width / p;    // p 必须整除 H，否则一行落不到整数钟上 / p must divide H or the line is not an integer clock count
    const int vall = FRAME_START_LINES + FRAME_BACK_LINES + display->height + FRAME_END_LINES;
    const int lsl = clocks_for_ns(LINE_START_MIN_NS, pclk_mhz);
    const int lbl = clocks_for_ns(LINE_BACK_MIN_NS, pclk_mhz);

    // 余量给 L_EL，让 L_DL 落在 CKV 高电平里。后肩只取规格下界。
    // / Slack goes to L_EL so L_DL sits inside CKV high. Back porch is the spec floor only.
    int line_us = ceil_div(ldl + lsl + lbl + LINE_END_MIN_CLK, pclk_mhz);
    if (profile == READ_PICO_EPD_SCAN_FULL) {
        const int target_line_us = (s_frame_target_us + vall / 2) / vall;
        if (target_line_us > line_us) line_us = target_line_us;
    }

    int line_clocks = line_us * pclk_mhz;
    int lel = line_clocks - ldl - lsl - lbl;
    if (lel < LINE_END_MIN_CLK) {
        line_us++;
        line_clocks = line_us * pclk_mhz;
        lel = line_clocks - ldl - lsl - lbl;
    }

    out->pclk_mhz = pclk_mhz;
    out->px_per_clk = p;
    out->line_start = lsl;
    out->line_back_porch = lbl;
    out->line_data = ldl;
    out->line_clocks = line_clocks;
    out->line_end = lel;
    out->line_us = line_us;
    out->frame_lines = vall;
    out->frame_us = line_us * vall;
    out->frame_hz_x100 = 100000000 / out->frame_us;

    // 开门至少盖住有效数据，再按厂家 LGONL 上限收；短行盖不住就收到行长减 1.2µs。
    // / Gate must cover active data, then cap at vendor LGONL; if the line is
    // too short, cap at line length minus 1.2 µs.
    const int data_end_01us = ceil_div((lsl + lbl + ldl) * 10, pclk_mhz);
    int ckv = s_ckv_high_max_01us;
    if (ckv < data_end_01us) ckv = data_end_01us;
    out->ckv_high_01us = clampi(ckv, CKV_LOW_MIN_01US, line_us * 10 - CKV_LOW_MIN_01US);
}

int read_pico_epd_refresh_ms(int pclk_mhz, read_pico_epd_scan_profile_t profile, int phases) {
    read_pico_epd_scan_t scan;
    read_pico_epd_scan(pclk_mhz, profile, &scan);
    return (phases * scan.frame_us + 500) / 1000;
}

static void apply_scan(int pclk_mhz, read_pico_epd_scan_profile_t profile) {
    read_pico_epd_scan_t scan;
    read_pico_epd_scan(pclk_mhz, profile, &scan);
    apply_lcd(&scan);
    // 每相开扫前的预填行数决定帧间隙。FULL 档一行 16us，喂线程余量大，32 行实测无欠载，
    // 帧间隙从约 1ms 归零；FAST 档一行只有 10us，32 行会欠载（EPD_DRAW_EMPTY_LINE_QUEUE，
    // 后续帧直接不输出），48/64 实测都没欠载，取 64 留余量。
    // / Prefill lines before each phase set the frame gap. FULL is 16 µs/line,
    // so 32 lines has slack and drops the ~1 ms gap to zero. FAST is 10 µs/line;
    // 32 underruns (EPD_DRAW_EMPTY_LINE_QUEUE, later frames go silent). 48/64
    // both hold; use 64.
    epd_lcd_set_prefill_lines(profile == READ_PICO_EPD_SCAN_FULL ? 32 : 64);
    s_pclk_mhz = scan.pclk_mhz;
    s_profile = profile;
    ESP_LOGI(
        TAG, "scan %s %d MHz: line %dus (%d+%d+%d+%d clk), frame %d.%02d Hz, GC16 %d phases %d ms",
        profile == READ_PICO_EPD_SCAN_FULL ? "full" : "fast", scan.pclk_mhz, scan.line_us,
        scan.line_start, scan.line_back_porch, scan.line_data, scan.line_end,
        scan.frame_hz_x100 / 100, scan.frame_hz_x100 % 100, E0470_GC16_FRAMES,
        read_pico_epd_refresh_ms(scan.pclk_mhz, profile, E0470_GC16_FRAMES)
    );
}

void read_pico_epd_init_lcd(
    const lcd_bus_config_t* bus, int bus_width, int width, int height,
    const read_pico_epd_scan_t* scan
) {
    if (bus == NULL || scan == NULL) return;
    LcdEpdConfig_t config = {
        .pixel_clock = (size_t)scan->pclk_mhz * 1000 * 1000,
        .line = scan_to_line(scan),
        .bus_width = bus_width,
        .bus = *bus,
    };
    epd_lcd_init(&config, width, height);
}

void read_pico_epd_set_pclk(int pclk_mhz) { apply_scan(pclk_mhz, s_profile); }

int read_pico_epd_pclk_mhz(void) { return s_pclk_mhz; }

void read_pico_epd_use_scan(read_pico_epd_scan_profile_t profile) {
    if (profile == s_profile) return;
    apply_scan(s_pclk_mhz, profile);
}
