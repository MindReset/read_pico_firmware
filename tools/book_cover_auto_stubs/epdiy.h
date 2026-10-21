#pragma once
#include <stdint.h>
enum EpdFontFlags { EPD_DRAW_ALIGN_LEFT, EPD_DRAW_ALIGN_CENTER, EPD_DRAW_ALIGN_RIGHT };
enum EpdRotation { EPD_ROT_LANDSCAPE, EPD_ROT_PORTRAIT,
                   EPD_ROT_INVERTED_LANDSCAPE, EPD_ROT_INVERTED_PORTRAIT };
static inline int epd_width(void) { return 1216; }
static inline int epd_height(void) { return 684; }
static inline int epd_rotated_display_width(void) { return 684; }
static inline int epd_rotated_display_height(void) { return 1216; }
static inline enum EpdRotation epd_get_rotation(void) { return EPD_ROT_INVERTED_PORTRAIT; }
