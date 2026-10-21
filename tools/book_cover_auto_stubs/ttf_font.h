#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "epdiy.h"
bool ttf_font_ready(void);
int ttf_ascender_px(int px);
int ttf_text_width_px(int px, const char *text);
void ttf_draw_text_px(uint8_t *fb, int x, int baseline, int px, const char *text,
                      enum EpdFontFlags align, uint8_t fg, uint8_t bg);
