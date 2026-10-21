/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：内建 Noto Sans SC 子集与独立的 PSRAM 位图缓存。常用汉字以分块压缩位图补齐，其他未知字交给阅读字体回退。
 * English: Built-in Noto Sans SC subset with its own PSRAM bitmap cache; block-compressed common Han glyphs, with other unknown glyphs falling back to the reader face.
 * 用户反馈书名锯齿：补充位图缩放改为覆盖率插值，保留字体来源、字宽及缓存上限；快刷最终仍为黑白。
 * User-reported jagged titles: interpolate supplemental bitmap coverage, retaining font sources, advances and cache bounds; final fast output remains monochrome.
 * 用户最新修订：书名及章节标题禁止放大补充位图；整行采用其原生24px，轮廓字形直接生成，并统一标题笔画黑度。
 * Latest user revision: book/chapter titles never enlarge supplemental bitmaps; use their native 24px for the entire line, rasterize outlines directly and keep title strokes equally black.
 */
#include "ui_font.h"
#include "ui_hanzi.h"
#include "ui_hanzi_scale.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "stb_truetype.h"
#pragma GCC diagnostic pop

#define UI_GLYPH_SLOTS 256
#define UI_GLYPH_LIMIT (512u * 1024u)

typedef struct {
    uint32_t cp, age;
    uint16_t px;
    int16_t x0, y0, width, height, advance;
    uint8_t *bitmap;
    size_t bytes;
    bool used;
} ui_glyph_t;

extern const uint8_t builtin_ttf_start[] asm("_binary_builtin_ttf_start");
static stbtt_fontinfo s_font;
static bool s_ready, s_attempted;
// 字形缓存放 PSRAM。每次取字形都会读一遍，但渲染本来就逐行读 PSRAM 里的
// framebuffer，多这一处不改变量级。
// The glyph cache lives in PSRAM. Every lookup walks it, but rendering already streams the
// framebuffer out of PSRAM, so this does not change the order of magnitude.
static ui_glyph_t *s_glyphs;

static bool glyphs_alloc(void) {
    if (!s_glyphs)
        s_glyphs = heap_caps_calloc(UI_GLYPH_SLOTS, sizeof(ui_glyph_t),
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return s_glyphs != NULL;
}
static size_t s_bytes;
static uint32_t s_age;
static uint8_t s_cover[256];

static bool ready(void) {
    if (s_attempted) return s_ready;
    s_attempted = true;
    int offset = stbtt_GetFontOffsetForIndex(builtin_ttf_start, 0);
    s_ready = offset >= 0 && stbtt_InitFont(&s_font, builtin_ttf_start, offset);
    for (int i = 0; i < 256; ++i)
        s_cover[i] = (uint8_t)lroundf(255.f * powf((float)i / 255.f, 0.4f));
    return s_ready;
}

static uint32_t next_cp(const char **p) {
    const unsigned char *s = (const unsigned char *)*p;
    uint32_t cp = *s++;
    if (cp < 0x80) { *p = (const char *)s; return cp; }
    int count = cp >= 0xc2 && cp <= 0xdf ? 1 : cp >= 0xe0 && cp <= 0xef ? 2 : cp >= 0xf0 && cp <= 0xf4 ? 3 : 0;
    if (!count) { *p = (const char *)s; return '?'; }
    cp &= (1u << (6 - count)) - 1;
    for (int i = 0; i < count; ++i) {
        if ((s[i] & 0xc0) != 0x80) { *p = (const char *)s; return '?'; }
        cp = (cp << 6) | (s[i] & 0x3f);
    }
    *p = (const char *)(s + count);
    return cp;
}

bool ui_font_has_text(const char *text) {
    if (!text || !ready()) return false;
    while (*text) {
        uint32_t cp = next_cp(&text);
        if (!stbtt_FindGlyphIndex(&s_font, (int)cp) && !ui_hanzi_has(cp)) return false;
    }
    return true;
}

static int clamp_px(int px) { return px < 8 ? 8 : px > 72 ? 72 : px; }
static float scale_for(int px) { return stbtt_ScaleForPixelHeight(&s_font, (float)clamp_px(px)); }

int ui_font_title_px(int preferred_px, const char *text) {
    if (text && ready()) while (*text) {
        uint32_t cp = next_cp(&text);
        if (!stbtt_FindGlyphIndex(&s_font, (int)cp) && ui_hanzi_has(cp))
            return UI_HANZI_BASE_PX;
    }
    return clamp_px(preferred_px);
}

int ui_font_ascender_px(int px) {
    if (!ready()) return 0;
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&s_font, &asc, &desc, &gap);
    return (int)lroundf(asc * scale_for(px));
}
void ui_font_measure_line_px(int px, const char *text, int *above, int *below) {
    int top = 0, bottom = 0;
    if (text && ready()) {
        float scale = scale_for(px);
        while (*text) {
            int x0, y0, x1, y1;
            uint32_t cp = next_cp(&text); uint8_t extra[UI_HANZI_RECORD];
            if (!stbtt_FindGlyphIndex(&s_font, (int)cp) && ui_hanzi_get(cp, extra)) {
                y0 = (int)floorf((int8_t)extra[1] * (float)clamp_px(px) / UI_HANZI_BASE_PX);
                y1 = y0 + (extra[3] * clamp_px(px) + UI_HANZI_BASE_PX - 1) / UI_HANZI_BASE_PX;
                x0 = 0; x1 = 1;
            } else stbtt_GetCodepointBitmapBox(&s_font, (int)cp, scale, scale, &x0, &y0, &x1, &y1);
            if (x1 <= x0 || y1 <= y0) continue;
            if (-y0 > top) top = -y0;
            if (y1 > bottom) bottom = y1;
        }
    }
    if (above) *above = top;
    if (below) *below = bottom;
}

static void evict(ui_glyph_t *g) {
    if (!g->used) return;
    free(g->bitmap);
    s_bytes -= g->bytes;
    memset(g, 0, sizeof(*g));
}

static ui_glyph_t *glyph(uint32_t cp, int px) {
    if (!ready()) return NULL;
    if (!glyphs_alloc()) return NULL;
    px = clamp_px(px);
    for (int i = 0; i < UI_GLYPH_SLOTS; ++i)
        if (s_glyphs[i].used && s_glyphs[i].cp == cp && s_glyphs[i].px == px) {
            s_glyphs[i].age = ++s_age;
            return &s_glyphs[i];
        }
    int slot = 0;
    for (int i = 0; i < UI_GLYPH_SLOTS; ++i)
        if (!s_glyphs[i].used || s_glyphs[i].age < s_glyphs[slot].age) slot = i;
    ui_glyph_t *g = &s_glyphs[slot];
    evict(g);
    float scale = scale_for(px);
    int advance, left;
    stbtt_GetCodepointHMetrics(&s_font, (int)cp, &advance, &left);
    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(&s_font, (int)cp, scale, scale, &x0, &y0, &x1, &y1);
    uint8_t extra[UI_HANZI_RECORD];
    bool supplemental = !stbtt_FindGlyphIndex(&s_font, (int)cp) && ui_hanzi_get(cp, extra);
    int fallback_advance = 0;
    if (supplemental) {
        x0 = (int)floorf((int8_t)extra[0] * (float)px / UI_HANZI_BASE_PX);
        y0 = (int)floorf((int8_t)extra[1] * (float)px / UI_HANZI_BASE_PX);
        x1 = x0 + (extra[2] * px + UI_HANZI_BASE_PX - 1) / UI_HANZI_BASE_PX;
        y1 = y0 + (extra[3] * px + UI_HANZI_BASE_PX - 1) / UI_HANZI_BASE_PX;
        fallback_advance = (extra[4] * px + UI_HANZI_BASE_PX / 2) / UI_HANZI_BASE_PX;
    }
    *g = (ui_glyph_t){.cp = cp, .px = px, .age = ++s_age, .used = true,
        .x0 = x0, .y0 = y0, .width = x1 - x0, .height = y1 - y0,
        .advance = (int16_t)(supplemental ? fallback_advance : lroundf(advance * scale))};
    if (g->width <= 0 || g->height <= 0) return g;
    g->bytes = (size_t)g->width * g->height;
    while (s_bytes + g->bytes > UI_GLYPH_LIMIT) {
        ui_glyph_t *oldest = NULL;
        for (int i = 0; i < UI_GLYPH_SLOTS; ++i)
            if (&s_glyphs[i] != g && s_glyphs[i].bitmap && (!oldest || s_glyphs[i].age < oldest->age)) oldest = &s_glyphs[i];
        if (!oldest) break;
        evict(oldest);
    }
    g->bitmap = heap_caps_malloc(g->bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (g->bitmap) {
        if (supplemental) for (int y = 0; y < g->height; ++y) for (int x = 0; x < g->width; ++x) {
            g->bitmap[y * g->width + x] = ui_hanzi_coverage(extra, g->width, g->height, x, y);
        }
        else stbtt_MakeCodepointBitmap(&s_font, g->bitmap, g->width, g->height, g->width,
                                      scale, scale, (int)cp);
        s_bytes += g->bytes;
    } else g->bytes = 0;
    return g;
}

int ui_font_text_width_px(int px, const char *text) {
    if (!text || !ready()) return 0;
    int width = 0;
    while (*text) {
        ui_glyph_t *g = glyph(next_cp(&text), px);
        if (g) width += g->advance;
    }
    return width;
}

static void draw_text_px(uint8_t *fb, int x, int baseline, int px,
                          const char *text, enum EpdFontFlags align,
                          uint8_t fg, uint8_t bg, unsigned threshold) {
    if (!fb || !text || !ready()) return;
    int cursor = x;
    if (align & EPD_DRAW_ALIGN_CENTER) cursor -= ui_font_text_width_px(px, text) / 2;
    else if (align & EPD_DRAW_ALIGN_RIGHT) cursor -= ui_font_text_width_px(px, text);
    while (*text) {
        ui_glyph_t *g = glyph(next_cp(&text), px);
        if (!g) continue;
        if (g->bitmap) for (int y = 0; y < g->height; ++y)
            for (int xx = 0; xx < g->width; ++xx) {
                uint8_t alpha = g->bitmap[y * g->width + xx];
                if (!alpha) continue;
                uint8_t shade = threshold ? (alpha >= threshold ? fg : bg) :
                    (uint8_t)(bg + s_cover[alpha] * ((int)fg - (int)bg) / 255);
                if (shade != bg) epd_draw_pixel(cursor + g->x0 + xx, baseline + g->y0 + y,
                                                 shade << 4, fb);
            }
        cursor += g->advance;
    }
}

void ui_font_draw_text_px(uint8_t *fb, int x, int baseline, int px,
                          const char *text, enum EpdFontFlags align,
                          uint8_t fg, uint8_t bg, bool black_white) {
    draw_text_px(fb, x, baseline, px, text, align, fg, bg, black_white ? 128 : 0);
}

void ui_font_draw_title_px(uint8_t *fb, int x, int baseline, int px,
                          const char *text, enum EpdFontFlags align) {
    // 和补充位图生成器的96门限一致，避免同一行出现不同笔画深度。
    // Match the supplement generator's threshold of 96 to keep stroke weight consistent within a line.
    draw_text_px(fb, x, baseline, ui_font_title_px(px, text), text, align, 0, 15, 96);
}
