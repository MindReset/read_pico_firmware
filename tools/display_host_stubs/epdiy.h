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
enum EpdRotation { EPD_ROT_LANDSCAPE, EPD_ROT_PORTRAIT,
                   EPD_ROT_INVERTED_LANDSCAPE, EPD_ROT_INVERTED_PORTRAIT };
enum EpdRotation epd_get_rotation(void);
enum EpdDrawError { EPD_DRAW_SUCCESS = 0, EPD_DRAW_EMPTY_LINE_QUEUE = 1,
                    EPD_DRAW_OTHER_ERROR = 2, EPD_DRAW_NO_PHASES_AVAILABLE = 4,
                    EPD_DRAW_INVALID_CROP = 0x40 };
void epd_poweron(void);
void epd_poweroff(void);
void epd_clear(void);
EpdRect epd_full_screen(void);
void epd_push_pixels(EpdRect area, short time, int color);
void epd_lcd_set_prefill_lines(int lines);
int epd_rotated_display_width(void);
int epd_rotated_display_height(void);
int epd_width(void);
int epd_height(void);
