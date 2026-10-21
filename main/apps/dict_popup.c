/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：点词查译的底部释义卡片；内容与命中都封在本模块，由 app_book 调用。
 * English: Bottom definition card for tap-to-lookup; state stays in this module.
 *
 * 冻结：draw 为纯绘制；释义行缓冲在 PSRAM；不含手势状态。
 * Frozen: draw paints only; definition lines live in PSRAM; no gesture state.
 */
#include "dict_popup.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dict_lookup.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ui_kit.h"
#include "vocab_log.h"

static const char* TAG = "dict";

#define DICT_POPUP_LINES 14
#define DICT_POPUP_PAD 36
#define DICT_POPUP_WORD_PX 46
#define DICT_POPUP_TEXT_PX 27
#define DICT_POPUP_LINE_H 42
#define DICT_POPUP_WORD_MAX 48
// 保存 chip 放在卡片头部右侧：不占正文行，释义仍是可显示的 10 行。
// The save chip sits in the card header on the right: it costs no body line, so the
// definition still shows its usual ten lines.
#define DICT_POPUP_SAVE_W 120
#define DICT_POPUP_SAVE_H 52
#define DICT_POPUP_SAVE_PX 26
#define DICT_POPUP_SUMMARY_LINES 3

static char s_word[DICT_POPUP_WORD_MAX];
static char* s_lines[DICT_POPUP_LINES];
static int s_line_count;
static bool s_truncated;
static bool s_saved; ///< 词是否已在生词本 / whether the word is in the vocabulary book
static int s_anchor_col;  ///< 点词所在侧：0=左半，1=右半 / tapped side: 0=left half, 1=right half
static bool s_anchor_split; ///< 分半几何开关：双栏生效，单栏横屏维持铺满 / split geometry: two-column only

// 避让锚点：按点词 x 选弹出侧（tap 分界与 gutter 中心重合，单栏横屏同一谓词成立）。
// Avoidance anchor: pick the popup side by the tap x (the tap boundary coincides with
// the gutter center; the same predicate holds in single-column landscape).
void dict_popup_set_anchor(int col, bool split) {
    s_anchor_col = col > 0 ? 1 : 0;
    s_anchor_split = split;
}

EpdRect dict_popup_rect(void) {
    // 竖屏：卡片放在页脚上方，正文上半仍可见(与原布局逐像素一致)。
    // 横屏：贴屏幕下沿、宽度铺满、高度按较短边钳制，避免 528 高的竖屏卡片溢出 684 的横屏。
    // Portrait: card above the footer, body stays visible above (byte-identical to the old layout).
    // Landscape: bottom-anchored, full width, height clamped to the short axis so the 528-tall
    // portrait card cannot overflow the 684-tall landscape frame.
    if (epd_get_rotation() == EPD_ROT_LANDSCAPE) {
        int h = ui_h() - 120;         // 顶部留 ~120px 正文可见 / keep ~120px of body visible on top
        if (h > 528) h = 528;
        if (h < 240) h = 240;
        if (s_anchor_split) {
            // 双栏避让：卡片占点词对侧半宽——点左栏词的卡片在右半（右缘对齐联合区右缘），
            // 点右栏词的卡片在左半，被点的词所在栏全程可见（v2 §5.2）。
            // Two-column avoidance: the card takes the half opposite the tapped word — a
            // left-side tap puts the card on the right (right edge on the union area's
            // right edge), a right-side tap on the left, so the tapped column stays visible.
            // 第 4 步接真栏矩形时按 reader_column_width()/reader_column_offset() 精化分界。
            // Refine the boundary against the real column rects (reader_column_width()/
            // reader_column_offset()) at step 4.
            int half = (ui_w() - 48) / 2;
            int x = s_anchor_col == 0 ? ui_w() - 24 - half : 24;
            return (EpdRect){x, ui_h() - 24 - h, half, h};
        }
        return (EpdRect){24, ui_h() - 24 - h, ui_w() - 48, h};
    }
    return (EpdRect){24, 540, UI_LOCK_WIDTH - 48, 528};
}

EpdRect dict_popup_save_rect(void) {
    EpdRect card = dict_popup_rect();
    return (EpdRect){card.x + card.width - DICT_POPUP_PAD - DICT_POPUP_SAVE_W, card.y + 22,
                     DICT_POPUP_SAVE_W, DICT_POPUP_SAVE_H};
}

void dict_popup_reset(void) {
    for (int i = 0; i < s_line_count; ++i) {
        free(s_lines[i]);
        s_lines[i] = NULL;
    }
    s_line_count = 0;
    s_truncated = false;
    s_saved = false;
    s_word[0] = 0;
}

// 释义摘要：给生词本当背面用，取前几行拼起来，够读就行。
// Definition summary used as the back of a flashcard: the first few lines joined.
static void summary_text(char* out, size_t cap) {
    out[0] = 0;
    size_t o = 0;
    for (int i = 0; i < s_line_count && i < DICT_POPUP_SUMMARY_LINES; ++i) {
        if (o && o + 4 < cap) { memcpy(out + o, " / ", 3); o += 3; out[o] = 0; }
        size_t n = strlen(s_lines[i]);
        if (o + n + 1 > cap) n = cap - o - 1;
        if (!n) break;
        memcpy(out + o, s_lines[i], n);
        o += n;
        out[o] = 0;
    }
}

dict_popup_tap_t dict_popup_tap(uint16_t x, uint16_t y) {
    if (!s_word[0] || !ui_rect_hit(dict_popup_save_rect(), x, y)) return DICT_POPUP_TAP_MISSED;
    char summary[288];
    summary_text(summary, sizeof(summary));
    // 移除失败就保持「已存」，保存失败就保持「保存」：chip 永远反映真实落盘结果。
    // A failed removal keeps "saved" and a failed save keeps "unsaved": the chip always
    // reflects what actually landed on the card.
    bool removing = s_saved;
    s_saved = removing ? !vocab_log_remove(s_word) : vocab_log_save(s_word, summary);
    ESP_LOGI(TAG, "vocab %s \"%s\" -> saved=%d", s_saved ? "save" : "unsave", s_word, (int)s_saved);
    return removing && !s_saved ? DICT_POPUP_TAP_REMOVED : DICT_POPUP_TAP_KEPT;
}

static int utf8_step(const char* p) {
    unsigned char c = (unsigned char)*p;
    if (!c) return 0;
    if (c < 0x80) return 1;
    if (c >= 0xc2 && c <= 0xdf) return 2;
    if (c >= 0xe0 && c <= 0xef) return 3;
    if (c >= 0xf0 && c <= 0xf4) return 4;
    return 1; // 非法字节按单字节跳过 / skip invalid bytes one by one
}

static bool store_line(const char* text, size_t len) {
    if (s_line_count >= DICT_POPUP_LINES) {
        s_truncated = true;
        return false;
    }
    char* copy = heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!copy) return false;
    memcpy(copy, text, len);
    copy[len] = 0;
    s_lines[s_line_count++] = copy;
    return true;
}

// 逐字累加宽度折行；比整串反复测量便宜，与正文排版同一做法。
// Wrap by per-glyph advance, the same approach the body layout uses.
static void wrap_text(const char* text, int px, int max_w) {
    const char* line = text;
    int width = 0;
    const char* p = text;
    while (*p) {
        int n = utf8_step(p);
        char glyph[5];
        if (n > 4) n = 4;
        memcpy(glyph, p, n);
        glyph[n] = 0;
        int w = ui_text_fixed_width_px(px, glyph);
        if (width + w > max_w && p > line) {
            // 分配失败也要标记截断，否则卡片少了一段却不显示省略号。
            // Mark truncation on allocation failure too, or the card silently loses the tail.
            if (!store_line(line, p - line)) { s_truncated = true; return; }
            line = p;
            width = 0;
            continue;
        }
        width += w;
        p += n;
    }
    if (*line) store_line(line, strlen(line));
}

bool dict_popup_open(const char* word) {
    // 计时：真机上想知道「打开 / 查词 / 折行」各占多少，串口直接读。
    // Timing: report how the cost splits across open, lookup and wrapping.
    const int64_t t0 = esp_timer_get_time();
    dict_popup_reset();
    if (!word || !*word) return false;
    // 释义缓冲放 PSRAM：主任务栈只有 8KB，不能在这里开大数组。
    // The definition buffer lives in PSRAM; the main task stack is only 8KB.
    char* out = heap_caps_malloc(DICT_ENTRY_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!out) return false;
    bool ok = false;
    int64_t open_us = 0, look_us = 0, wrap_us = 0;
    bool ready = dict_ready();
    if (!ready) {
        const int64_t a = esp_timer_get_time();
        ready = dict_open();
        open_us = esp_timer_get_time() - a;
    }
    if (ready) {
        const int64_t a = esp_timer_get_time();
        const bool found = dict_lookup_word(word, out, DICT_ENTRY_MAX);
        look_us = esp_timer_get_time() - a;
        if (found) {
            const int64_t b = esp_timer_get_time();
            snprintf(s_word, sizeof(s_word), "%.*s", DICT_POPUP_WORD_MAX - 1, word);
            int text_w = dict_popup_rect().width - 2 * DICT_POPUP_PAD;
            char* p = out;
            while (*p) {
                char* nl = strchr(p, '\n');
                size_t len = nl ? (size_t)(nl - p) : strlen(p);
                char saved = p[len];
                p[len] = 0;
                if (len) wrap_text(p, DICT_POPUP_TEXT_PX, text_w);
                p[len] = saved;
                if (s_truncated) break;
                if (!nl) break;
                p = nl + 1;
            }
            wrap_us = esp_timer_get_time() - b;
            ok = s_line_count > 0;
            // 索引未就绪时返回 false，chip 显示「保存」——绝不在这里读卡。
            // Returns false when the index is unavailable and the chip shows "unsaved";
            // the card is never read here.
            if (ok) s_saved = vocab_log_contains(s_word);
        }
    }
    ESP_LOGI(TAG, "lookup \"%s\" ok=%d open=%lldms io=%lldms wrap=%lldms total=%lldms",
             word, (int)ok, (long long)(open_us / 1000), (long long)(look_us / 1000),
             (long long)(wrap_us / 1000), (long long)((esp_timer_get_time() - t0) / 1000));
    free(out);
    if (!ok) dict_popup_reset();
    return ok;
}

void dict_popup_draw(uint8_t* fb) {
    EpdRect card = dict_popup_rect();
    if (!s_word[0]) return;
    ui_fill_round_rect(fb, card, 24, UI_GRAY_WHITE);
    ui_draw_round_rect(fb, card, 24, 0x30);
    const int inner_x = card.x + DICT_POPUP_PAD;
    const int inner_w = card.width - 2 * DICT_POPUP_PAD;
    // 词头可能超出卡片宽，按宽度从左截短；可用宽度要扣掉右侧的保存 chip。
    // Trim overlong heads from the left; the usable width excludes the save chip.
    const int head_w = inner_w - DICT_POPUP_SAVE_W - 12;
    char head[DICT_POPUP_WORD_MAX];
    snprintf(head, sizeof(head), "%s", s_word);
    while (head[0] && ui_text_fixed_width_px(DICT_POPUP_WORD_PX, head) > head_w) {
        size_t len = strlen(head);
        head[len - 1] = 0; // ASCII 词头逐字节收缩 / ASCII heads shrink byte by byte
    }
    ui_text_fixed(fb, inner_x, card.y + 26, DICT_POPUP_WORD_PX, head, EPD_DRAW_ALIGN_LEFT, false);
    // 保存 chip：未保存是描边，已保存是选中态。
    // Save chip: outlined when unsaved, selected when saved.
    EpdRect chip = dict_popup_save_rect();
    const char* label = s_saved ? "已存" : "保存";
    if (s_saved) ui_draw_choice_round_rect(fb, chip, 12, true);
    else ui_draw_round_rect(fb, chip, 12, UI_GRAY_BLACK);
    int label_w = ui_text_fixed_width_px(DICT_POPUP_SAVE_PX, label);
    ui_text_fixed(fb, chip.x + (chip.width - label_w) / 2, chip.y + 12, DICT_POPUP_SAVE_PX,
                  label, EPD_DRAW_ALIGN_LEFT, false);
    int y = card.y + 26 + DICT_POPUP_WORD_PX + 18;
    for (int i = 0; i < s_line_count; ++i) {
        const char* line = s_lines[i];
        ui_text_fixed(fb, inner_x, y, DICT_POPUP_TEXT_PX, line, EPD_DRAW_ALIGN_LEFT, false);
        y += DICT_POPUP_LINE_H;
        if (y + DICT_POPUP_TEXT_PX > card.y + card.height - 20) {
            if (i + 1 < s_line_count || s_truncated) {
                ui_text_fixed(fb, card.x + card.width - DICT_POPUP_PAD, y - DICT_POPUP_LINE_H + DICT_POPUP_TEXT_PX + 8,
                              21, "……", EPD_DRAW_ALIGN_RIGHT, false);
            }
            break;
        }
    }
}
