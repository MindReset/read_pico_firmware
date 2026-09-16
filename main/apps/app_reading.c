/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 阅读页。抗锯齿正文，底栏 DU / GL16 / GC16 翻页。页眉右栏调字号。
 * 正文来自内置 assets/reading.md，按字号动态分页。
 *
 * Reading page. Antialiased body; bar turns pages with DU / GL16 / GC16.
 * Header right changes size. Body is built-in assets/reading.md, paginated
 * from the current size.
 */

#include "app.h"
#include "display.h"
#include "e0470_epaper_waveform.h"
#include "ttf_font.h"
#include "ui_kit.h"
#include "ui_menu.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define READING_TITLE "阅读测试 Reading"
#define READING_PX_MIN 36
#define READING_PX_MAX 72
#define READING_PX_STEP 4
#define READING_PX_DEFAULT 48
#define READING_SIZE_BTN 44
#define READING_SIZE_GAP 6
#define READING_SIZE_LABEL 48
#define READING_PAGE_MAX 80
#define READING_BLK_MAX 160
#define READING_BOLD_WGHT 700
#define READING_SIZE_REPEAT_FIRST_MS 200
#define READING_SIZE_REPEAT_MS 90
#define READING_SIZE_SETTLE_MS 400
#define READING_BODY_TOP (UI_CONTENT_TOP + 8)

extern const uint8_t reading_md_start[] asm("_binary_reading_md_start");
extern const uint8_t reading_md_end[] asm("_binary_reading_md_end");

enum {
    READING_DU = 0,
    READING_GL16,
    READING_GC16,
};

enum {
    BLK_H1 = 0,
    BLK_H2,
    BLK_P,
    BLK_BOLD,
};

typedef struct {
    uint8_t kind;
    const char* text;
} blk_t;

typedef struct {
    EpdRect minus;
    EpdRect plus;
    int label_cx;
    int label_cy;
} size_layout_t;

static const char* TAG = "reading";

static int s_px = READING_PX_DEFAULT;
static int s_layout_px;
static int s_page_count = 1;
static int s_page_blk[READING_PAGE_MAX];
static int s_page_off[READING_PAGE_MAX];
static char* s_md;
static blk_t s_doc[READING_BLK_MAX];
static int s_doc_n;

// 下一页画到独立 PSRAM。core 1、低于 epdiy 喂数；扫描时被抢占，拍间空档跑。
// Next page paints into its own PSRAM. Core 1, below epdiy feed; preempted during scan, runs in the inter-frame gap.
// TTF 有全局状态，画页必须和主线程互斥，不能写正在扫的 front/back。
// TTF has global state; page paint must lock against the main thread and must not write the front/back being scanned.
static uint8_t* s_next_fb;
static int s_next_page = -1;
static int s_next_px;
static volatile int s_prep_req = -1;
static SemaphoreHandle_t s_draw_lock;
static TaskHandle_t s_prep_task;
static int s_size_dir;
static int64_t s_size_next_ms;
static int64_t s_size_settle_ms;

static size_t reading_fb_bytes(void) {
    return (size_t)epd_width() / 2 * (size_t)epd_height();
}

static void lock_draw(void) {
    if (s_draw_lock != NULL) xSemaphoreTake(s_draw_lock, portMAX_DELAY);
}

static void unlock_draw(void) {
    if (s_draw_lock != NULL) xSemaphoreGive(s_draw_lock);
}

static char* trim_line(char* s) {
    while (*s == ' ' || *s == '\t') s++;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) {
        s[--n] = '\0';
    }
    return s;
}

static bool line_is_bold(char* line, const char** out) {
    size_t n = strlen(line);
    if (n < 4 || line[0] != '*' || line[1] != '*' || line[n - 2] != '*' || line[n - 1] != '*') {
        return false;
    }
    line[n - 2] = '\0';
    *out = line + 2;
    return true;
}

// 把内置 reading.md 拆成标题 / 加粗行 / 段落，空行只当分隔。/ Split built-in reading.md into headings / bold lines / paragraphs; blank lines are separators only.
static void parse_md(void) {
    if (s_md != NULL) return;
    size_t len = (size_t)(reading_md_end - reading_md_start);
    s_md = malloc(len + 1);
    if (s_md == NULL) return;
    memcpy(s_md, reading_md_start, len);
    s_md[len] = '\0';

    char* p = s_md;
    while (*p != '\0' && s_doc_n < READING_BLK_MAX) {
        char* nl = strchr(p, '\n');
        if (nl != NULL) *nl = '\0';
        char* line = trim_line(p);
        if (line[0] != '\0') {
            const char* bold = NULL;
            if (strncmp(line, "## ", 3) == 0) {
                s_doc[s_doc_n++] = (blk_t){ .kind = BLK_H2, .text = line + 3 };
            } else if (strncmp(line, "# ", 2) == 0) {
                s_doc[s_doc_n++] = (blk_t){ .kind = BLK_H1, .text = line + 2 };
            } else if (line_is_bold(line, &bold)) {
                s_doc[s_doc_n++] = (blk_t){ .kind = BLK_BOLD, .text = bold };
            } else {
                s_doc[s_doc_n++] = (blk_t){ .kind = BLK_P, .text = line };
            }
        }
        if (nl == NULL) break;
        p = nl + 1;
    }
}

// 页眉字号条到正文都要刷；底栏三个波形钮进页时已经画好，后面不再动。/ Refresh from the header size bar through the body; the three waveform buttons are painted on enter and stay put.
static EpdRect reading_refresh_area(void) {
    return (EpdRect){
        .x = 0,
        .y = 0,
        .width = epd_rotated_display_width(),
        .height = UI_BAR_TOP,
    };
}

static int blk_px(int kind) {
    if (kind == BLK_H1) return s_px + 18;
    if (kind == BLK_H2) return s_px + 8;
    return s_px;
}

static int blk_lh(int kind) {
    int px = blk_px(kind);
    return px + px / 2;
}

static int para_gap(int kind) {
    int lh = blk_lh(kind);
    return (kind == BLK_H1 || kind == BLK_H2) ? lh / 2 : lh / 3;
}

static const char* utf8_next(const char* s) {
    unsigned char c = (unsigned char)*s;
    if (c < 0x80) return s + 1;
    if (c < 0xE0) return s + 2;
    if (c < 0xF0) return s + 3;
    return s + 4;
}

static int wrap_one(const char* text, int px, char* buf, size_t buf_n) {
    const int max_w = ui_content_width();
    const char* p = text;
    const char* end = text;
    if (*p == '\0' || buf_n < 2) return 0;
    while (*p) {
        const char* next = utf8_next(p);
        size_t n = (size_t)(next - text);
        if (n + 1 > buf_n) break;
        memcpy(buf, text, n);
        buf[n] = '\0';
        if (ttf_text_width_px(px, buf) > max_w && end != text) break;
        end = next;
        p = next;
    }
    if (end == text) end = utf8_next(text);
    size_t n = (size_t)(end - text);
    if (n + 1 > buf_n) n = buf_n - 1;
    memcpy(buf, text, n);
    buf[n] = '\0';
    return (int)n;
}

static bool take_line(int* blk, int* off, char* buf, size_t buf_n, int* kind, bool* end_para) {
    while (*blk < s_doc_n) {
        const char* text = s_doc[*blk].text + *off;
        if (*text == '\0') {
            (*blk)++;
            *off = 0;
            continue;
        }
        *kind = s_doc[*blk].kind;
        int n = wrap_one(text, blk_px(*kind), buf, buf_n);
        if (n <= 0) {
            (*blk)++;
            *off = 0;
            continue;
        }
        *off += n;
        *end_para = (s_doc[*blk].text[*off] == '\0');
        if (*end_para) {
            (*blk)++;
            *off = 0;
        }
        return true;
    }
    return false;
}

static void rebuild_pages(int keep_blk, int keep_off, int* page_out) {
    s_page_blk[0] = 0;
    s_page_off[0] = 0;
    s_page_count = 1;
    s_layout_px = s_px;

    int blk = 0;
    int off = 0;
    int y = READING_BODY_TOP;
    int best = 0;

    for (;;) {
        int peek_blk = blk;
        int peek_off = off;
        char buf[160];
        int kind = 0;
        bool end_para = false;
        if (!take_line(&peek_blk, &peek_off, buf, sizeof(buf), &kind, &end_para)) break;

        int h = blk_lh(kind);
        if (y + h > UI_CONTENT_BOTTOM) {
            if (s_page_count >= READING_PAGE_MAX) break;
            if (blk == s_page_blk[s_page_count - 1] && off == s_page_off[s_page_count - 1]) {
                break;
            }
            s_page_blk[s_page_count] = blk;
            s_page_off[s_page_count] = off;
            s_page_count++;
            y = READING_BODY_TOP;
            continue;
        }

        take_line(&blk, &off, buf, sizeof(buf), &kind, &end_para);
        y += h;
        if (end_para) y += para_gap(kind);

        if (blk < keep_blk || (blk == keep_blk && off <= keep_off)) {
            best = s_page_count - 1;
        }
    }

    if (page_out != NULL) *page_out = best;
}

static void clamp_page(int* page) {
    if (*page < 0) *page = 0;
    if (*page >= s_page_count) *page = s_page_count - 1;
}

static void ensure_layout(int* page) {
    parse_md();
    if (s_doc_n <= 0) {
        s_page_count = 1;
        s_page_blk[0] = 0;
        s_page_off[0] = 0;
        *page = 0;
        return;
    }
    if (s_layout_px == s_px && s_page_count > 0) {
        clamp_page(page);
        return;
    }
    int keep_blk = 0;
    int keep_off = 0;
    if (*page >= 0 && *page < s_page_count) {
        keep_blk = s_page_blk[*page];
        keep_off = s_page_off[*page];
    }
    rebuild_pages(keep_blk, keep_off, page);
}

static void reading_sub(char* buf, size_t n, int page) {
    snprintf(
        buf, n, "第 %d / %d 页　Page %d / %d",
        page + 1, s_page_count, page + 1, s_page_count
    );
}

static size_layout_t size_from_acc(EpdRect acc) {
    return (size_layout_t){
        .minus = {
            .x = acc.x,
            .y = acc.y,
            .width = READING_SIZE_BTN,
            .height = READING_SIZE_BTN,
        },
        .plus = {
            .x = acc.x + acc.width - READING_SIZE_BTN,
            .y = acc.y,
            .width = READING_SIZE_BTN,
            .height = READING_SIZE_BTN,
        },
        .label_cx = acc.x + acc.width / 2,
        .label_cy = acc.y + acc.height / 2,
    };
}

static ui_header_skel_t reading_head(const char* sub) {
    const int w = READING_SIZE_BTN * 2 + READING_SIZE_LABEL + READING_SIZE_GAP * 2;
    return ui_header_skel_box(READING_TITLE, sub, w, READING_SIZE_BTN);
}

static size_layout_t size_layout(int page) {
    char sub[64];
    reading_sub(sub, sizeof(sub), page);
    return size_from_acc(reading_head(sub).accessory);
}

static void draw_line(uint8_t* fb, int y, int kind, const char* text) {
    int px = blk_px(kind);
    int old = ttf_get_weight();
    if (kind == BLK_BOLD || kind == BLK_H1 || kind == BLK_H2) {
        ttf_set_weight(READING_BOLD_WGHT);
    }
    ui_text(fb, UI_MARGIN, y, px, text, EPD_DRAW_ALIGN_LEFT, false);
    ttf_set_weight(old);
}

static void draw_size_controls(uint8_t* fb, const size_layout_t* L) {
    ui_draw_button(fb, L->minus, "－", false);
    ui_draw_button(fb, L->plus, "＋", false);
    char label[8];
    snprintf(label, sizeof(label), "%d", s_px);
    ui_text_vc(
        fb, L->label_cx, L->label_cy, UI_PX_SUB, label, EPD_DRAW_ALIGN_CENTER, false
    );
}

static void draw_page(uint8_t* fb, int* page) {
    ensure_layout(page);
    ui_clear_page(fb);

    char sub[64];
    reading_sub(sub, sizeof(sub), *page);
    ui_header_skel_t head = reading_head(sub);
    ui_draw_header_skel(fb, &head, READING_TITLE, sub);
    size_layout_t size = size_from_acc(head.accessory);
    draw_size_controls(fb, &size);

    int blk = s_page_blk[*page];
    int off = s_page_off[*page];
    int y = READING_BODY_TOP;
    char buf[160];

    while (y + 12 <= UI_CONTENT_BOTTOM) {
        int peek_blk = blk;
        int peek_off = off;
        int kind = 0;
        bool end_para = false;
        if (!take_line(&peek_blk, &peek_off, buf, sizeof(buf), &kind, &end_para)) break;
        int h = blk_lh(kind);
        if (y + h > UI_CONTENT_BOTTOM) break;
        take_line(&blk, &off, buf, sizeof(buf), &kind, &end_para);
        draw_line(fb, y, kind, buf);
        y += h;
        if (end_para) y += para_gap(kind);
    }

    ui_draw_button(fb, ui_bar_rect(0, 3), "DU", false);
    ui_draw_button(fb, ui_bar_rect(1, 3), "GL16", false);
    ui_draw_button(fb, ui_bar_rect(2, 3), "GC16", false);
    ui_draw_menu_handle(fb, false);
}

static void prep_task(void* arg) {
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (s_draw_lock == NULL || s_next_fb == NULL) continue;
        lock_draw();
        int page = s_prep_req;
        if (page >= 0 && s_page_count > 0) {
            if (page >= s_page_count) page = s_page_count - 1;
            const int64_t t0 = esp_timer_get_time();
            draw_page(s_next_fb, &page);
            s_next_page = page;
            s_next_px = s_px;
            ESP_LOGI(TAG, "prep %d/%d %d ms", page + 1, s_page_count,
                     (int)((esp_timer_get_time() - t0) / 1000));
        }
        unlock_draw();
    }
}

static void ensure_prep(void) {
    if (s_draw_lock == NULL) s_draw_lock = xSemaphoreCreateMutex();
    if (s_next_fb == NULL) {
        s_next_fb = heap_caps_aligned_alloc(
            16, reading_fb_bytes(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        );
    }
    if (s_prep_task == NULL && s_next_fb != NULL && s_draw_lock != NULL) {
        xTaskCreatePinnedToCore(prep_task, "read_prep", 12 * 1024, NULL, 3, &s_prep_task, 1);
    }
}

static void kick_next(int shown) {
    if (s_prep_task == NULL || s_next_fb == NULL || s_page_count <= 1) return;
    const int n = (shown + 1) % s_page_count;
    if (s_next_page == n && s_next_px == s_px) return;
    s_prep_req = n;
    xTaskNotifyGive(s_prep_task);
}

static bool apply_page(uint8_t* fb, int page) {
    if (s_next_fb != NULL && s_next_page == page && s_next_px == s_px) {
        memcpy(fb, s_next_fb, reading_fb_bytes());
        return true;
    }
    int p = page;
    draw_page(fb, &p);
    return false;
}

static enum EpdDrawError refresh_turn(EpdiyHighlevelState* hl, int kind) {
    const EpdRect area = reading_refresh_area();
    if (kind == READING_GC16) {
        return update_display_area_with(hl, &E0470_WAVEFORM, MODE_GC16, area);
    }
    if (kind == READING_GL16) {
        return update_display_area_with(hl, &E0470_FULL_WAVEFORM, MODE_GL16, area);
    }
    return update_display_area_with(hl, &E0470_WAVEFORM, MODE_DU, area);
}

static void on_enter(app_ctx_t* ctx) {
    ctx->leaf = 0;
    s_layout_px = 0;
    s_next_page = -1;
    s_prep_req = -1;
    s_size_dir = 0;
    s_size_settle_ms = 0;
    ensure_prep();
}

static void reading_on_exit(app_ctx_t* ctx) {
    (void)ctx;
    s_size_dir = 0;
    s_size_settle_ms = 0;
    s_prep_req = -1;
    if (s_draw_lock == NULL) return;
    lock_draw();
    s_next_page = -1;
    unlock_draw();
}

static void render(app_ctx_t* ctx, uint8_t* fb) {
    ensure_prep();
    lock_draw();
    draw_page(fb, &ctx->leaf);
    unlock_draw();
    kick_next(ctx->leaf);
}

static bool reading_present(app_ctx_t* ctx, app_redraw_t redraw) {
    if (redraw == APP_REDRAW_DONE) return true;
    if (redraw != APP_REDRAW_PAGE && redraw != APP_REDRAW_FULL) return false;
    // 从菜单进来时底栏还是「上一页/下一页」，先铺白再出这一页，避免残边。/ Coming from the menu the bar still says Prev/Next; flash white then present this page to avoid leftover edges.
    guard_draw_result(ctx->hl, update_display_white(ctx->hl));
    render(ctx, ctx->fb);
    guard_draw_result(ctx->hl, update_display_from_white(ctx->hl));
    return true;
}

static app_redraw_t turn_page(app_ctx_t* ctx, int kind) {
    ensure_prep();
    lock_draw();
    ensure_layout(&ctx->leaf);
    ctx->leaf = (ctx->leaf + 1) % s_page_count;
    const bool hit = apply_page(ctx->fb, ctx->leaf);
    unlock_draw();
    ESP_LOGI(TAG, "turn %s %d/%d", hit ? "hit" : "miss", ctx->leaf + 1, s_page_count);
    kick_next(ctx->leaf);
    guard_draw_result(ctx->hl, refresh_turn(ctx->hl, kind));
    return APP_REDRAW_DONE;
}

static app_redraw_t apply_size_delta(app_ctx_t* ctx, int dir) {
    const int next = s_px + dir * READING_PX_STEP;
    if (next < READING_PX_MIN || next > READING_PX_MAX) return APP_REDRAW_NONE;
    ensure_prep();
    lock_draw();
    s_px = next;
    s_layout_px = 0;
    s_next_page = -1;
    s_prep_req = -1;
    draw_page(ctx->fb, &ctx->leaf);
    unlock_draw();
    guard_draw_result(
        ctx->hl,
        update_display_area_with(ctx->hl, &E0470_WAVEFORM, MODE_DU, reading_refresh_area())
    );
    s_size_settle_ms = ctx->now_ms + READING_SIZE_SETTLE_MS;
    return APP_REDRAW_DONE;
}

static app_redraw_t start_size(app_ctx_t* ctx, int dir) {
    app_redraw_t redraw = apply_size_delta(ctx, dir);
    if (redraw == APP_REDRAW_NONE) return APP_REDRAW_NONE;
    s_size_dir = dir;
    s_size_next_ms = ctx->now_ms + READING_SIZE_REPEAT_FIRST_MS;
    return redraw;
}

static app_redraw_t on_tick(app_ctx_t* ctx) {
    if (s_size_dir != 0) {
        const bool up = ctx->released || ctx->touch == NULL || !ctx->touch->touched;
        size_layout_t size = size_layout(ctx->leaf);
        const EpdRect hit = s_size_dir < 0 ? size.minus : size.plus;
        if (up || !ui_rect_hit(hit, ctx->touch->x, ctx->touch->y)) {
            s_size_dir = 0;
        } else if (ctx->now_ms >= s_size_next_ms) {
            s_size_next_ms = ctx->now_ms + READING_SIZE_REPEAT_MS;
            return apply_size_delta(ctx, s_size_dir);
        }
    }
    if (s_size_dir == 0 && s_size_settle_ms != 0 && ctx->now_ms >= s_size_settle_ms) {
        s_size_settle_ms = 0;
        // 调字号停手后定稿。GL16 白底不压黑，避免正文区闪一条黑带；残影靠周期 GC16 清。/ Settle after size change stops. GL16 keeps a white ground so the body does not flash a black band; leftover ghosting is cleared by periodic GC16.
        guard_draw_result(
            ctx->hl,
            update_display_area_with(
                ctx->hl, &E0470_WAVEFORM, MODE_GL16, reading_refresh_area()
            )
        );
        kick_next(ctx->leaf);
        return APP_REDRAW_DONE;
    }
    return APP_REDRAW_NONE;
}

static app_redraw_t on_touch(app_ctx_t* ctx, const cst836u_touch_t* touch) {
    size_layout_t size = size_layout(ctx->leaf);
    if (ui_rect_hit(size.minus, touch->x, touch->y)) return start_size(ctx, -1);
    if (ui_rect_hit(size.plus, touch->x, touch->y)) return start_size(ctx, 1);
    int hit = ui_bar_hit(touch->x, touch->y, 3);
    if (hit >= 0) return turn_page(ctx, hit);
    if (touch->y >= UI_CONTENT_TOP && touch->y < UI_BAR_TOP) {
        return turn_page(ctx, READING_GL16);
    }
    return APP_REDRAW_NONE;
}

const app_desc_t app_reading = {
    .title = READING_TITLE,
    .detail = "正文翻页对照 Reading Page Turn",
    .enter_full = true,
    .render = render,
    .present = reading_present,
    .on_enter = on_enter,
    .on_exit = reading_on_exit,
    .on_touch = on_touch,
    .on_tick = on_tick,
};
