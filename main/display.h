/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 墨水屏刷新、HV 轨空闲超时、pclk 回退。
 *
 * EPD present, HV-rail idle timeout, and pclk fallback.
 */

#pragma once

#include <stdint.h>

#include "epd_highlevel.h"
#include "epdiy.h"
#include "read_pico_epd_timing.h"

#ifdef __cplusplus
extern "C" {
#endif

// LCD 像素时钟：只决定有效像素段占一行的多少，剩下的由行结束段补足——行周期被锁在
// 波形标定的帧周期上，所以调 pclk 不会让刷新变快，只影响 DMA 的供数余量。12MHz 是
// epdiy 的保守默认值，18MHz 实测稳定（前提是 PSRAM 跑在 120MHz，80MHz 下 16MHz
// 就喂不满 DMA），开机锁 18MHz 留足余量。
// LCD pixel clock: it only sets how much of the line the active pixels occupy;
// the line-end pad fills the rest. The line period is locked to the waveform
// frame time, so pclk does not make refresh faster — it only changes DMA slack.
// 12 MHz is epdiy's conservative default; 18 MHz is stable here when PSRAM runs
// at 120 MHz (at 80 MHz even 16 MHz starves DMA). Boot locks 18 MHz for margin.
#define DISPLAY_PCLK_DEFAULT_MHZ 18
// 出现供数不足（EPD_DRAW_EMPTY_LINE_QUEUE）时退回这个确定安全的频率。
// Fall back to this known-safe clock on underrun (EPD_DRAW_EMPTY_LINE_QUEUE).
#define DISPLAY_PCLK_SAFE_MHZ READ_PICO_EPD_PCLK_MIN_MHZ
// 工程页上还能继续往上试。行消隐与 CKV 宽度会跟着频率重解。
// The lab page can still step higher. Line blanking and CKV width re-solve with the clock.
#define DISPLAY_PCLK_MIN_MHZ READ_PICO_EPD_PCLK_MIN_MHZ
#define DISPLAY_PCLK_MAX_MHZ READ_PICO_EPD_PCLK_MAX_MHZ
#define DISPLAY_PCLK_STEP_MHZ 1

void rails_keepalive(void);
void rails_idle_check(int64_t now_ms);

enum EpdDrawError update_display_mode(EpdiyHighlevelState* hl, enum EpdDrawMode mode);
enum EpdDrawError update_display_from_white(EpdiyHighlevelState* hl);
enum EpdDrawError update_display_from_white_with(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode
);
/// 把前缓冲铺白再 GC16 全刷，物理屏回到白底。
/// Paint the front buffer white and GC16 the panel back to white.
enum EpdDrawError update_display_white(EpdiyHighlevelState* hl);
enum EpdDrawError update_display_full(EpdiyHighlevelState* hl);
enum EpdDrawError update_display_with(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode
);
/// 灰阶图还在屏上时置位：菜单盖上来或离页先刷白，避免从中间灰差分。
/// Set while a gray image is still on panel: wipe to white before the menu or leave so the next update is not a mid-gray differential.
void display_hold_white_exit(bool hold);
bool display_take_white_exit(void);
enum EpdDrawError update_display_area_with(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode,
    EpdRect area
);

/// 当前像素时钟。/ Current pixel clock.
int display_pclk_mhz(void);
/// 出现供数不足就退回安全频率并整屏重刷，其它错误码原样忽略。
/// On underrun, drop to the safe clock and full-refresh; other error bits are ignored.
void guard_draw_result(EpdiyHighlevelState* hl, enum EpdDrawError result);

#ifdef __cplusplus
}
#endif
