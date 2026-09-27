/* SPDX-License-Identifier: Apache-2.0
 * 显示硬件主机替身。/ Display hardware host shim.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct { int x, y, width, height; } EpdRect;
typedef struct { int id; } EpdWaveform;
enum EpdDrawMode { MODE_DU = 1, MODE_GC16 = 2, MODE_GL16 = 5 };
enum EpdDrawError { EPD_DRAW_SUCCESS = 0, EPD_DRAW_EMPTY_LINE_QUEUE = 1, EPD_DRAW_OTHER_ERROR = 2 };
void epd_poweron(void);
void epd_poweroff(void);
void epd_clear(void);
void epd_lcd_set_prefill_lines(int lines);
