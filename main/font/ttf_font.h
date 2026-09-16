/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 可变 TTF 字形缓存：卡上字体或内置子集，按字重光栅化后画到 framebuffer。
 *
 * Variable TTF glyph cache: SD fonts or the built-in subset, rasterized
 * at the current weight onto the framebuffer.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "epdiy.h"
#include "esp_err.h"

#define TTF_SIZE_SMALL 0
#define TTF_SIZE_LARGE 1

#define TTF_FONT_MAX 24
#define TTF_FONT_NAME_MAX 64
#define TTF_FONT_PATH_MAX 160

typedef struct {
    char name[TTF_FONT_NAME_MAX];
    char path[TTF_FONT_PATH_MAX];
} ttf_font_item_t;

typedef struct {
    uint32_t glyphs;
    uint32_t hits;
    uint32_t misses;
    int64_t read_us;
    int64_t raster_us;
    int64_t total_us;
} ttf_bench_stats_t;

#define TTF_FONT_BUILTIN "builtin"

/// 灰阶字覆盖率伽马。小于 1 抬中间覆盖率，抗锯齿边缘更深；满墨仍是 0。
/// Coverage gamma for gray glyphs. Below 1 lifts mid coverage so AA edges are darker; full ink stays 0.
#ifndef TTF_COVER_GAMMA
#define TTF_COVER_GAMMA 0.6f
#endif

esp_err_t ttf_font_init(void);
esp_err_t ttf_font_open(const char* path);
esp_err_t ttf_font_open_builtin(void);
bool ttf_font_is_builtin(void);
bool ttf_font_path_is_builtin(const char* path);
void ttf_font_unload(void);
int ttf_font_scan(void);
int ttf_font_count(void);
const ttf_font_item_t* ttf_font_item(int index);
const char* ttf_font_path(void);
const char* ttf_font_display_name(void);
bool ttf_font_ready(void);
int ttf_ascender(int size);
int ttf_ascender_px(int pixel_height);

void ttf_draw_text(
    uint8_t* framebuffer, int x, int y, int size, const char* text,
    enum EpdFontFlags align, uint8_t fg, uint8_t bg
);

void ttf_draw_text_px(
    uint8_t* framebuffer, int x, int y, int pixel_height, const char* text,
    enum EpdFontFlags align, uint8_t fg, uint8_t bg
);
/// 覆盖率过半才落墨，像素只有 fg/bg。/ Ink only when coverage is over half; pixels are fg/bg only.
void ttf_draw_text_px_bw(
    uint8_t* framebuffer, int x, int y, int pixel_height, const char* text,
    enum EpdFontFlags align, uint8_t fg, uint8_t bg
);

void ttf_measure_line(int size, const char* text, int* above, int* below);
void ttf_measure_line_px(int pixel_height, const char* text, int* above, int* below);
int ttf_text_width_px(int pixel_height, const char* text);
void ttf_set_weight(int wght);
int ttf_get_weight(void);

void ttf_font_cache_clear(void);
void ttf_bench_begin(void);
void ttf_bench_end(ttf_bench_stats_t* out);
