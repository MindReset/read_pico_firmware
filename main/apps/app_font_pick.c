/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 字体。选 TTF、看排版、测冷/热缓存。页眉右栏循环字重，底栏冷/热启动。
 *
 * Fonts. Pick a TTF, preview type, and bench cold/warm cache. Header right
 * cycles weight; bar is cold/warm start.
 *
 * 冻结：列表走 UI_BTN_H；行数和样本数由剩余高度解出；换字体或字重整屏 GC16。
 * Frozen: list rows use UI_BTN_H; row and sample counts come from leftover
 * height; switching font or weight is a full-screen GC16.
 */

#include <stdio.h>
#include <string.h>

#include "app.h"
#include "display.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "read_pico_sd.h"
#include "settings.h"
#include "ttf_font.h"
#include "ui_kit.h"
#include "ui_menu.h"

#define TAG "app_font"
#define FONT_TITLE "字体 Font"
#define FONT_ROW_H UI_BTN_H
#define FONT_NAV_H 56
#define FONT_MIN_LIST 2
#define FONT_SCORE_ROWS 2
#define FONT_WGHT_W 96
#define FONT_WGHT_H 44
#define FONT_HIT_NONE (-1)
#define FONT_HIT_PREV (-2)
#define FONT_HIT_NEXT (-3)
#define FONT_HIT_COLD (-4)
#define FONT_HIT_WARM (-5)
#define FONT_HIT_WGHT (-6)

typedef struct {
    int list_y;
    int nav_y;
    int sample_y;
    int score_y;
    int per_page;
    int sample_n;
    int spec_w;
    bool paged;
    EpdRect prev;
    EpdRect next;
    EpdRect cold;
    EpdRect warm;
    EpdRect wght;
} font_geom_t;

typedef struct {
    ttf_bench_stats_t cold;
    ttf_bench_stats_t warm;
    int32_t last_refresh_ms;
    bool cold_ok;
    bool warm_ok;
    bool last_cold;
} font_bench_t;

static font_bench_t s_bench;
static const int k_wght[] = { 300, 400, 500, 600, 700, 800 };
#define FONT_WGHT_N ((int)(sizeof(k_wght) / sizeof(k_wght[0])))
static int s_wght_i = 1;

static int font_total(void) {
    return 1 + ttf_font_scan();
}

static int current_wght(void) {
    return k_wght[s_wght_i];
}

static void font_sub(char* buf, size_t n) {
    const char* name = ttf_font_display_name();
    snprintf(buf, n, "当前 %s", name[0] != '\0' ? name : "内建 Built-in");
}

static ui_header_skel_t font_head(const char* sub) {
    return ui_header_skel_box(FONT_TITLE, sub, FONT_WGHT_W, FONT_WGHT_H);
}

static int sample_inner_h(int n) {
    int h = 0;
    for (int i = 0; i < n; i++) {
        if (i) h += UI_GAP;
        h += ui_sample_lines[i].px;
    }
    return h;
}

static int sample_fit(int budget) {
    const int lines = ui_sample_line_count();
    int n = 0;
    int rest = budget;
    for (int i = 0; i < lines; i++) {
        const int need = ui_sample_lines[i].px + (n ? UI_GAP : 0);
        if (need > rest) break;
        rest -= need;
        n++;
    }
    return n < 1 ? 1 : n;
}

static int leaf_count(int per_page) {
    const int n = font_total();
    if (per_page < 1) return 1;
    return (n + per_page - 1) / per_page;
}

static int clamp_leaf(int leaf, int leaves) {
    if (leaf < 0) return 0;
    if (leaf >= leaves) return leaves - 1;
    return leaf;
}

static int rows_for(int budget) {
    int per = (budget - UI_SEC_HEAD + UI_GAP) / (FONT_ROW_H + UI_GAP);
    return per < 1 ? 1 : per;
}

static font_geom_t font_geom(void) {
    const int content = UI_CONTENT_BOTTOM - UI_CONTENT_TOP;
    const int score_h = UI_SEC_HEAD + FONT_SCORE_ROWS * UI_ROW_H_SM;
    const int min_list = UI_SEC_HEAD + FONT_MIN_LIST * FONT_ROW_H
        + (FONT_MIN_LIST - 1) * UI_GAP;
    const int sample_n = sample_fit(
        content - min_list - score_h - 2 * UI_SECTION_GAP - UI_SEC_HEAD
    );
    const int sample_h = UI_SEC_HEAD + sample_inner_h(sample_n);
    int budget = content - sample_h - score_h - 2 * UI_SECTION_GAP;
    int per = rows_for(budget);
    bool paged = leaf_count(per) > 1;
    if (paged) {
        per = rows_for(budget - UI_GAP - FONT_NAV_H);
        paged = leaf_count(per) > 1;
    }

    const int list_h = UI_SEC_HEAD + per * FONT_ROW_H + (per - 1) * UI_GAP;
    const int nav_y = paged ? UI_CONTENT_TOP + list_h + UI_GAP : 0;
    const int sample_y = UI_CONTENT_TOP + list_h
        + (paged ? UI_GAP + FONT_NAV_H : 0) + UI_SECTION_GAP;
    char sub[80];
    font_sub(sub, sizeof(sub));

    return (font_geom_t){
        .list_y = UI_CONTENT_TOP,
        .nav_y = nav_y,
        .sample_y = sample_y,
        .score_y = sample_y + sample_h + UI_SECTION_GAP,
        .per_page = per,
        .sample_n = sample_n,
        .spec_w = ttf_text_width_px(UI_PX_CAPTION, "000") + UI_GAP,
        .paged = paged,
        .prev = ui_row_rect(0, 2, nav_y, FONT_NAV_H),
        .next = ui_row_rect(1, 2, nav_y, FONT_NAV_H),
        .cold = ui_bar_rect(0, 2),
        .warm = ui_bar_rect(1, 2),
        .wght = font_head(sub).accessory,
    };
}

static EpdRect font_item_rect(const font_geom_t* g, int row) {
    return (EpdRect){
        .x = UI_MARGIN,
        .y = g->list_y + UI_SEC_HEAD + row * (FONT_ROW_H + UI_GAP),
        .width = ui_content_width(),
        .height = FONT_ROW_H,
    };
}

static const char* font_dir_label(const char* path) {
    if (ttf_font_path_is_builtin(path)) return "内建 Built-in";
    if (strstr(path, "/assets/fonts/") != NULL) return "assets/fonts";
    if (strstr(path, "/fonts/") != NULL) return "fonts";
    return path;
}

static void font_item(int index, const char** name, const char** dir) {
    *name = "内建 Built-in";
    *dir = "内建 Built-in";
    if (index <= 0) return;
    const ttf_font_item_t* item = ttf_font_item(index - 1);
    if (item == NULL) return;
    *name = item->name;
    *dir = font_dir_label(item->path);
}

static bool font_is_current(int index) {
    if (index == 0) {
        return ttf_font_is_builtin() || ttf_font_path_is_builtin(ttf_font_path());
    }
    const ttf_font_item_t* item = ttf_font_item(index - 1);
    const char* cur = ttf_font_path();
    return item != NULL && cur != NULL && strcmp(cur, item->path) == 0;
}

static int leaf_for_current(int per_page) {
    if (per_page < 1) return 0;
    if (ttf_font_is_builtin() || ttf_font_path_is_builtin(ttf_font_path())) return 0;
    const int n = ttf_font_scan();
    const char* cur = ttf_font_path();
    for (int i = 0; i < n; i++) {
        const ttf_font_item_t* item = ttf_font_item(i);
        if (item != NULL && cur != NULL && strcmp(cur, item->path) == 0) {
            return (i + 1) / per_page;
        }
    }
    return 0;
}

static void bench_reset(void) {
    memset(&s_bench, 0, sizeof(s_bench));
}

static void fmt_ms(char* buf, size_t n, bool ok, int64_t us) {
    if (!ok) {
        strlcpy(buf, "-", n);
        return;
    }
    snprintf(buf, n, "%d ms", (int)((us + 500) / 1000));
}

static int64_t bench_rest_us(const ttf_bench_stats_t* s) {
    const int64_t used = s->read_us + s->raster_us;
    return s->total_us > used ? s->total_us - used : 0;
}

static int draw_sample_lines(uint8_t* fb, int y, int spec_w, int n) {
    const int old = ttf_get_weight();
    const int wght = current_wght();
    for (int i = 0; i < n; i++) {
        const ui_sample_line_t* line = &ui_sample_lines[i];
        char spec[16];
        snprintf(spec, sizeof(spec), "%d", line->px);
        ttf_set_weight(400);
        ui_text_vc(
            fb, UI_MARGIN, y + line->px / 2, UI_PX_CAPTION, spec,
            EPD_DRAW_ALIGN_LEFT, false
        );
        ttf_set_weight(wght);
        ui_text(
            fb, UI_MARGIN + spec_w, y, line->px, line->text,
            EPD_DRAW_ALIGN_LEFT, false
        );
        y += line->px + UI_GAP;
    }
    ttf_set_weight(old);
    return y;
}

static void draw_scores(uint8_t* fb, int y) {
    char cold[16], warm[16], rast[16], epd[16];
    const ttf_bench_stats_t* last = NULL;
    if (s_bench.cold_ok || s_bench.warm_ok) {
        last = s_bench.last_cold ? &s_bench.cold : &s_bench.warm;
    }
    fmt_ms(cold, sizeof(cold), s_bench.cold_ok, s_bench.cold.total_us);
    fmt_ms(warm, sizeof(warm), s_bench.warm_ok, s_bench.warm.total_us);
    fmt_ms(rast, sizeof(rast), last != NULL, last ? last->raster_us : 0);
    if (s_bench.last_refresh_ms > 0) {
        snprintf(epd, sizeof(epd), "%d ms", (int)s_bench.last_refresh_ms);
    } else {
        strlcpy(epd, "-", sizeof(epd));
    }

    y = ui_draw_section(fb, y, "成绩 Bench");
    y = ui_draw_row2(fb, y, "冷启动 Cold", cold, "热启动 Warm", warm);
    ui_draw_row2(fb, y, "光栅 Raster", rast, "刷屏 EPD", epd);
}

static void draw_fonts(uint8_t* fb, const font_geom_t* g, int leaf) {
    const int n = font_total();
    const int first = leaf * g->per_page;
    ui_draw_section(fb, g->list_y, "字体 Fonts");
    for (int row = 0; row < g->per_page; row++) {
        const int index = first + row;
        if (index >= n) break;
        const char* name = NULL;
        const char* dir = NULL;
        font_item(index, &name, &dir);
        const bool on = font_is_current(index);
        const EpdRect card = font_item_rect(g, row);
        const int cy = card.y + card.height / 2;
        ui_draw_choice_round_rect(fb, card, UI_BTN_RADIUS, on);
        ui_text_vc(
            fb, card.x + UI_PAD, cy, UI_PX_LABEL, name,
            EPD_DRAW_ALIGN_LEFT, false
        );
        ui_text_vc(
            fb, card.x + card.width - UI_PAD, cy, UI_PX_CAPTION,
            on ? "使用中 On" : dir, EPD_DRAW_ALIGN_RIGHT, false
        );
    }
    if (!g->paged) return;
    const int leaves = leaf_count(g->per_page);
    ui_draw_button(fb, g->prev, "上一页 Prev", leaf > 0);
    ui_draw_button(fb, g->next, "下一页 Next", leaf + 1 < leaves);
}

static void draw_page(uint8_t* fb, int leaf, bool time_samples) {
    if (!ttf_font_ready()) {
        read_pico_sd_info_t sd = { 0 };
        read_pico_sd_get_info(&sd);
        ui_draw_no_font_page(fb, sd.present, false);
        return;
    }

    ttf_set_weight(400);
    const font_geom_t g = font_geom();
    const int leaves = leaf_count(g.per_page);
    leaf = clamp_leaf(leaf, leaves);

    char sub[80];
    char wght[8];
    font_sub(sub, sizeof(sub));
    snprintf(wght, sizeof(wght), "%d", current_wght());
    ui_header_skel_t head = font_head(sub);

    ui_clear_page(fb);
    ui_draw_header_skel(fb, &head, FONT_TITLE, sub);
    ui_draw_button(fb, g.wght, wght, false);
    draw_fonts(fb, &g, leaf);
    ui_draw_section(fb, g.sample_y, "排版 Type");
    if (time_samples) ttf_bench_begin();
    draw_sample_lines(fb, g.sample_y + UI_SEC_HEAD, g.spec_w, g.sample_n);
    if (time_samples) {
        ttf_bench_stats_t st;
        ttf_bench_end(&st);
        if (s_bench.last_cold) {
            s_bench.cold = st;
            s_bench.cold_ok = true;
        } else {
            s_bench.warm = st;
            s_bench.warm_ok = true;
        }
    }
    draw_scores(fb, g.score_y);
    ui_draw_button(fb, g.cold, "冷启动 Cold", false);
    ui_draw_button(fb, g.warm, "热启动 Warm", false);
    ui_draw_menu_handle(fb, false);
}

static int hit_test(uint16_t x, uint16_t y, int leaf) {
    const font_geom_t g = font_geom();
    const int n = font_total();
    const int leaves = leaf_count(g.per_page);
    leaf = clamp_leaf(leaf, leaves);

    if (ui_rect_hit(g.wght, x, y)) return FONT_HIT_WGHT;
    if (ui_rect_hit(g.cold, x, y)) return FONT_HIT_COLD;
    if (ui_rect_hit(g.warm, x, y)) return FONT_HIT_WARM;
    if (g.paged) {
        if (ui_rect_hit(g.prev, x, y)) return leaf > 0 ? FONT_HIT_PREV : FONT_HIT_NONE;
        if (ui_rect_hit(g.next, x, y)) return leaf + 1 < leaves ? FONT_HIT_NEXT : FONT_HIT_NONE;
    }

    const int first = leaf * g.per_page;
    for (int row = 0; row < g.per_page; row++) {
        const int index = first + row;
        if (index >= n) break;
        if (ui_rect_hit(font_item_rect(&g, row), x, y)) return index;
    }
    return FONT_HIT_NONE;
}

static void bench_log(bool cold) {
    const ttf_bench_stats_t* s = cold ? &s_bench.cold : &s_bench.warm;
    ESP_LOGI(
        TAG,
        "ttf_bench %s font=%s n=%u hit=%u miss=%u "
        "total=%dms read=%dms raster=%dms rest=%dms epd=%dms",
        cold ? "cold" : "warm", ttf_font_display_name(),
        (unsigned)s->glyphs, (unsigned)s->hits, (unsigned)s->misses,
        (int)((s->total_us + 500) / 1000),
        (int)((s->read_us + 500) / 1000),
        (int)((s->raster_us + 500) / 1000),
        (int)(bench_rest_us(s) / 1000),
        (int)s_bench.last_refresh_ms
    );
}

static app_redraw_t bench_run(app_ctx_t* ctx, bool cold) {
    if (cold) ttf_font_cache_clear();
    s_bench.last_cold = cold;
    draw_page(ctx->fb, ctx->leaf, true);
    const int64_t t0 = esp_timer_get_time();
    guard_draw_result(ctx->hl, update_display_mode(ctx->hl, APP_PAGE_REFRESH_MODE));
    s_bench.last_refresh_ms = (int32_t)((esp_timer_get_time() - t0) / 1000);
    bench_log(cold);
    return APP_REDRAW_PAGE;
}

static app_redraw_t pick(int index) {
    if (font_is_current(index)) return APP_REDRAW_NONE;
    bench_reset();
    if (index == 0) {
        app_settings_set_font_path("");
        ttf_font_open_builtin();
    } else {
        const ttf_font_item_t* item = ttf_font_item(index - 1);
        if (item == NULL) return APP_REDRAW_NONE;
        app_settings_set_font_path(item->path);
        esp_err_t err = ttf_font_open(item->path);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "font switch %s: %s", item->path, esp_err_to_name(err));
        }
    }
    ESP_LOGI(TAG, "font %s", ttf_font_path());
    return APP_REDRAW_FULL;
}

static void render(app_ctx_t* ctx, uint8_t* fb) {
    draw_page(fb, ctx->leaf, false);
}

static void on_enter(app_ctx_t* ctx) {
    ctx->leaf = leaf_for_current(font_geom().per_page);
}

static void font_on_exit(app_ctx_t* ctx) {
    (void)ctx;
    ttf_set_weight(400);
}

static app_redraw_t on_touch(app_ctx_t* ctx, const cst836u_touch_t* touch) {
    if (!ttf_font_ready()) return APP_REDRAW_NONE;
    const int hit = hit_test(touch->x, touch->y, ctx->leaf);
    if (hit == FONT_HIT_PREV) {
        ctx->leaf--;
        return APP_REDRAW_PAGE;
    }
    if (hit == FONT_HIT_NEXT) {
        ctx->leaf++;
        return APP_REDRAW_PAGE;
    }
    if (hit == FONT_HIT_WGHT) {
        s_wght_i = (s_wght_i + 1) % FONT_WGHT_N;
        bench_reset();
        return APP_REDRAW_FULL;
    }
    if (hit == FONT_HIT_COLD) return bench_run(ctx, true);
    if (hit == FONT_HIT_WARM) return bench_run(ctx, false);
    if (hit < 0) return APP_REDRAW_NONE;
    return pick(hit);
}

static app_redraw_t on_key(app_ctx_t* ctx, int key) {
    if (key == UI_KEY_1 && ctx->leaf > 0) {
        ctx->leaf--;
        return APP_REDRAW_PAGE;
    }
    return APP_REDRAW_NONE;
}

const app_desc_t app_font_pick = {
    .title = FONT_TITLE,
    .detail = "选择、排版与基准 Select, Type & Bench",
    .enter_full = true,
    .render = render,
    .on_enter = on_enter,
    .on_exit = font_on_exit,
    .on_touch = on_touch,
    .on_key = on_key,
};
