/* SPDX-License-Identifier: Apache-2.0
 * 中文：主页面灰阶保持未变像素；书架退出加强同向擦白，主页水波纹端点单向推动，中灰保持原序列。
 * English: Main-page grays hold unchanged pixels; shelf exit strengthens directional whitening and main water uses directional endpoints while retaining original midgray sequences.
 */
#pragma once
#include "epdiy.h"
const EpdWaveform* display_main_gray_waveform(void);
/// 离开书架单向变白并补四相，避免GC16反向压黑；其他迁移用原DU，等值保持。/ Shelf exit whitens directionally with four extra phases, avoiding GC16 reverse darkening; other transitions use original DU and equal values hold.
const EpdWaveform* display_main_shelf_exit_waveform(void);
/// 水波纹与普通切页共用：端点单向驱动，中灰变色保留完整序列，等灰保持；调用者决定是否错相。
/// Shared by water and ordinary transitions: directional endpoints, complete changed midgray sequences and held equal grays; callers choose staggering.
const EpdWaveform* display_main_water_waveform(void);
/// 快刷首帧保留原DU并保持所有等值像素，亚克力首帧不驱动。/ Fast first pass retains original DU while holding every equal pair, including skipped acrylic.
const EpdWaveform* display_main_bw_waveform(void);

/// 普通模式灰边交错，端点/相数/推动数量不变；OOM回退原柔和灰阶。
/// Ordinary gray-edge interleaving keeps endpoints/phase counts/pulse counts; OOM falls back to original soft grays.
const EpdWaveform* display_main_normal_waveform(void);
