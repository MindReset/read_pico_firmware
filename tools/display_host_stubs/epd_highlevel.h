/* SPDX-License-Identifier: Apache-2.0
 * 高层显示边界声明。/ High-level display boundary declarations.
 */
#pragma once
#include "epdiy.h"
typedef struct { uint8_t* front_fb; uint8_t* back_fb; const EpdWaveform* waveform; } EpdiyHighlevelState;
void epd_hl_set_all_white(EpdiyHighlevelState*);
void epd_hl_waveform(EpdiyHighlevelState*, const EpdWaveform*);
enum EpdDrawError epd_hl_update_screen(EpdiyHighlevelState*, enum EpdDrawMode, int);
enum EpdDrawError epd_hl_update_screen_full(EpdiyHighlevelState*, enum EpdDrawMode, int);
enum EpdDrawError epd_hl_update_screen_from_white(EpdiyHighlevelState*, enum EpdDrawMode, int);
enum EpdDrawError epd_hl_update_area(EpdiyHighlevelState*, enum EpdDrawMode, int, EpdRect);
enum EpdDrawError epd_hl_update_area_full(EpdiyHighlevelState*, enum EpdDrawMode, int, EpdRect);
