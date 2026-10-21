/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 基于 MindReset Read Pico 官方 E0470 波形与刷新路径实现。
 * Built on the MindReset Read Pico E0470 waveform and refresh path.
 * 错相揭页引擎。应用只依赖本公开接口，不依赖条带或 LUT 的内部布局。
 * Staggered page-turn engine. Applications depend on this public API, not the internal band or LUT layout.
 * 用户最新指定阅读与主页均用8带、11090us节拍；保留各自灰阶序列，原错开启动接口仍可用。
 * Latest user request: reader and main pages use eight bands at 11090us with distinct gray sequences, retaining the spaced-launch API.
 */

#pragma once

#include "epd_highlevel.h"
#include "epdiy.h"

#ifdef __cplusplus
extern "C" {
#endif

/// 逻辑屏幕上的揭页方向；库内按当前旋转映射到 framebuffer。/ Logical direction mapped through the current rotation.
typedef enum {
    E0470_TURN_LTR = 0,
    E0470_TURN_RTL = 1,
    E0470_TURN_TTB = 2,
    E0470_TURN_BTT = 3,
} e0470_turn_dir_t;

#define E0470_TURN_DEFAULT_TICK_US 12000
// PR17快档只缩短软件补等，不改变单次扫描或每像素相位。/ PR17 fast pacing changes only software padding, never a scan or a pixel's phases.
#define E0470_TURN_FAST_TICK_US 11090
#define E0470_TURN_FAST_BANDS 8

const char* e0470_turn_dir_name(e0470_turn_dir_t dir);

/// 每拍目标间隔；默认12ms，实际扫描更慢时不截短；不改变驱动脉冲宽度。/ Target tick interval: 12ms; never truncate a slower scan or change pulse widths.
void e0470_page_turn_set_tick_us(int us);
int e0470_page_turn_tick_us(void);
/// 离开阅读时释放按需分配的相位表。/ Release the lazily allocated phase table when leaving the reader.
void e0470_page_turn_release(void);

/// `area` 是逻辑坐标；无可用 GL16 时返回 `EPD_DRAW_NO_PHASES_AVAILABLE`，不刷屏。
/// `area` uses logical coordinates; unavailable GL16 phases return without scanning.
/// 阅读8带按一拍一带启动，37相共44拍；调用方选择11090us并负责FAST扫描与HV轨保活。
/// Reader launches 8 bands one per tick, totaling 44 ticks for 37 phases; the caller selects 11090us and owns FAST scanning and HV rail keepalive.
enum EpdDrawError e0470_page_turn(
    EpdiyHighlevelState* hl, EpdRect area, e0470_turn_dir_t dir
);

/// 显式灰阶序列与2..32条带；阅读入口固定原GL16/8带。/ Explicit gray sequence and 2..32 bands; the reader entry uses original GL16/8 bands.
enum EpdDrawError e0470_page_turn_with_waveform(
    EpdiyHighlevelState* hl, EpdRect area, e0470_turn_dir_t dir,
    const EpdWaveform* waveform, unsigned bands
);

/// 显式序列的一拍一带入口；保留波形、指定条带数与当前节拍，8带37相共44拍。/ Compact explicit sequence: retain waveform, requested bands and current pacing; eight bands/37 phases take 44 ticks.
enum EpdDrawError e0470_page_turn_with_waveform_compact(
    EpdiyHighlevelState* hl, EpdRect area, e0470_turn_dir_t dir,
    const EpdWaveform* waveform, unsigned bands
);

#ifdef __cplusplus
}
#endif
