/* SPDX-License-Identifier: Apache-2.0
 * 中文：九宫格、全键盘、数字符号与中英文共用交互；键盘常亮，不做闪烁刷新。
 * English: Shared T9, QWERTY, symbols and bilingual input; steady keys without timed screen flashing.
 * 用户修订：普通输入只画变化区域，布局切换才重画键盘，不累计整页清屏次数。
 * User revision: paint changed input regions; repaint keys only on layout switches, without counting whole-page cleanup.
 * 用户修订：九宫格只输入中文拼音，英文使用全键盘；数字仅在数字页输入，内部九键编码不展示。
 * User revision: Chinese Pinyin only in T9, English in QWERTY, digits only in the numeric panel; never display internal T9 codes.
 * 用户修订：删除键长按连续删除；抬手、移开、多点或输入会话结束立即取消，仅局部快刷。
 * 用户修订：按键按下加深、下沉两像素，松开复原，只刷该键；同音字可继续翻页。
 * User revision: darken/inset a pressed key, restore on release with local updates, and page through all homophones.
 * User revision: held backspace repeats; release, movement, multitouch or session exit cancels immediately, with local fast updates only.
 * 用户修订：候选高频变化后在无触摸停顿时仅整理词栏，不打断连打、不全屏黑闪。
 * User revision: settle only the word strip after frequent changes and a touch-free pause, without interrupting typing or flashing the whole screen.
 */
#include "ui_keyboard.h"
#include "ui_ime.h"
#include <ui_kit.h>
#include "read_pico_search.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static ui_text_edit_t *s_edit;
static ui_ime_result_t *s_candidates;
static char s_raw[UI_IME_RAW_MAX + 1], s_preferred[9], s_notice[80];
static bool s_ascii, s_chinese, s_nine, s_upper;
static unsigned s_panel;
static size_t s_page, s_window, s_syllable_offset;
static char s_best_roman[UI_IME_RAW_MAX + 1];
static size_t s_best_consume;
static bool s_pressed, s_probe, s_keys_only;
static int s_probe_x, s_probe_y;
static EpdRect s_pressed_area, s_feedback_area, s_key_clip;
enum { DIRTY_FIELD = 1, DIRTY_WORDS = 2, DIRTY_SYLLABLES = 4, DIRTY_SPACE = 8, DIRTY_LAYOUT = 16, DIRTY_FEEDBACK = 32, DIRTY_WORDS_SETTLE = 64 };
static unsigned s_dirty, s_candidates_revision;
enum { WORDS_SETTLE_CHANGES = 6, WORDS_SETTLE_IDLE_MS = 700 };
static unsigned s_word_updates;
static int64_t s_words_idle_since = -1;
static bool s_delete_hold;
static EpdRect s_delete_area;
static int s_delete_top;
static int64_t s_delete_next_ms, s_delete_last_ms;
static uint32_t stamp(const char *text) {
    uint32_t value = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) value = (value ^ *p) * 16777619u;
    return value;
}
static const char *const groups[] = {"", "abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz"};
static const char *const symbols[] = {"，","。","？","！","：","；","“","”","（","）","、","…","—","·","-","_","/","@","#","&","+","=","%","\\","*","$","<",">","[","]"};
static const char *const ascii_symbols[] = {"!","@","#","$","%","^","&","*","(",")","-","_","+","=","/","\\",":",";","\"","'",".",",","?","<",">","[","]","{","}","|","`","~"};
static EpdRect rect(int x, int y, int w, int h, int top) { return (EpdRect){x, y + top, w, h}; }
static bool load_candidates(size_t start) {
    if (!s_candidates || !s_edit || !s_chinese) return false;
    char before[129]; size_t n = s_edit->cursor;
    if (n >= sizeof(before)) n = sizeof(before) - 1;
    memcpy(before, s_edit->text, n); before[n] = 0;
    return ui_ime_candidates_page(s_raw, s_nine, s_preferred, before, start, s_candidates);
}
static void refresh(void) {
    s_page = s_window = s_syllable_offset = 0;
    s_best_roman[0] = 0; s_best_consume = 0;
    if (!s_candidates || !s_edit || !s_chinese) return;
    ++s_candidates_revision;
    if (!load_candidates(0)) snprintf(s_notice, sizeof(s_notice), "内存不足，请缩短拼音后重试");
    else if (s_candidates->count) {
        snprintf(s_best_roman, sizeof(s_best_roman), "%s", s_candidates->items[0].roman);
        s_best_consume = s_candidates->items[0].consume;
    }
}
void ui_keyboard_end(void) {
    ui_keyboard_hold_cancel();
    s_pressed = s_probe = s_keys_only = false; s_feedback_area = (EpdRect){0};
    free(s_candidates); s_candidates = NULL; s_edit = NULL; ui_ime_release(); s_dirty = 0;
    s_word_updates = 0; s_words_idle_since = -1;
    memset(s_raw, 0, sizeof(s_raw)); memset(s_preferred, 0, sizeof(s_preferred));
    memset(s_notice, 0, sizeof(s_notice)); s_page = 0;
}
void ui_keyboard_begin(ui_text_edit_t *edit, bool ascii_only) {
    ui_keyboard_end(); s_edit = edit; s_ascii = ascii_only;
    s_chinese = !ascii_only; s_nine = !ascii_only; s_upper = false; s_panel = 0;
    if (!ascii_only) s_candidates = heap_caps_malloc(sizeof(*s_candidates), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ascii_only && !s_candidates) { s_chinese = s_nine = false; snprintf(s_notice, sizeof(s_notice), "内存不足，暂用英文键盘"); }
    refresh();
}
bool ui_keyboard_pending(void) { return s_raw[0] != 0; }
static bool insert(const char *text) {
    if (s_edit && ui_text_edit_insert(s_edit, text)) { s_notice[0] = 0; return true; }
    snprintf(s_notice, sizeof(s_notice), "文字已达到长度上限"); return false;
}
static bool commit(size_t index) {
    if (!s_candidates || index >= s_candidates->count) return false;
    const ui_ime_candidate_t *item = &s_candidates->items[index];
    if (!insert(item->text)) return false;
    size_t consume = item->consume, length = strlen(s_raw);
    if (consume > length) return false;
    memmove(s_raw, s_raw + consume, length - consume + 1);
    s_preferred[0] = 0; refresh(); return true;
}
static bool same_rect(EpdRect a, EpdRect b) { return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height; }
static bool intersects(EpdRect a, EpdRect b) { return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height; }
static void key_paint(uint8_t *fb, EpdRect r, const char *text, int px, bool dark, bool utility) {
    if (s_probe) { if (ui_rect_hit(r, s_probe_x, s_probe_y)) s_key_clip = r; return; }
    if (s_keys_only && !intersects(r, s_key_clip)) return;
    bool pressed = s_pressed && same_rect(r, s_pressed_area);
    ui_fill_round_rect(fb, r, 12, dark ? (pressed ? 0x50 : UI_GRAY_BLACK) : pressed ? 0xb0 : utility ? 0xd8 : UI_GRAY_WHITE);
    ui_draw_round_rect(fb, r, 12, pressed ? 0x20 : 0x60);
    if (!strcmp(text, "⌫")) {
        int cx = r.x + r.width / 2, cy = r.y + r.height / 2 + (pressed ? 2 : 0);
        for (int d = 0; d < 2; ++d) {
            epd_draw_line(cx - 17, cy, cx - 7, cy - 11 + d, UI_GRAY_BLACK, fb);
            epd_draw_line(cx - 17, cy, cx - 7, cy + 11 - d, UI_GRAY_BLACK, fb);
            epd_draw_line(cx - 7, cy - 11 + d, cx + 17, cy - 11 + d, UI_GRAY_BLACK, fb);
            epd_draw_line(cx - 7, cy + 11 - d, cx + 17, cy + 11 - d, UI_GRAY_BLACK, fb);
            epd_draw_line(cx + 17 - d, cy - 11, cx + 17 - d, cy + 11, UI_GRAY_BLACK, fb);
            epd_draw_line(cx - 2, cy - 5 + d, cx + 8, cy + 5 + d, UI_GRAY_BLACK, fb);
            epd_draw_line(cx - 2, cy + 5 + d, cx + 8, cy - 5 + d, UI_GRAY_BLACK, fb);
        }
    } else ui_text_fixed_vc(fb, r.x + r.width / 2, r.y + r.height / 2 + (pressed ? 2 : 0), px, text, EPD_DRAW_ALIGN_CENTER, dark);
}
static size_t visible(size_t start, EpdRect boxes[5], int top) {
    if (!s_candidates || !s_chinese) return 0;
    int left = 78, limit = 606; size_t count = 0;
    while (start + count < s_candidates->count && count < 5) {
        const char *text = s_candidates->items[start + count].text;
        int width = ui_text_fixed_width_px(25, text) + 24;
        if (width < 78) width = 78;
        if (width > limit - 78) width = limit - 78;
        if (left + width > limit) break;
        boxes[count] = rect(left, 104, width - 6, 56, top);
        left += width; ++count;
    }
    return count;
}
bool ui_keyboard_page(int direction) {
    if (!s_candidates) return false;
    EpdRect boxes[5]; size_t count = visible(s_page, boxes, 0);
    if (direction > 0 && count) {
        if (s_page + count < s_candidates->count) s_page += count;
        else if (s_candidates->has_more) {
            size_t next = s_window + s_candidates->count;
            if (!load_candidates(next)) return false;
            s_window = next; s_page = 0;
        } else return false;
        s_dirty |= DIRTY_WORDS; return true;
    }
    if (direction < 0 && (s_page || s_window)) {
        if (!s_page) {
            size_t previous = s_window >= UI_IME_CANDIDATES ? s_window - UI_IME_CANDIDATES : 0;
            if (!load_candidates(previous)) return false;
            s_window = previous; s_page = s_candidates->count;
        }
        size_t begin = 0, previous = 0;
        while (begin < s_page) { previous = begin; size_t n = visible(begin, boxes, 0); if (!n) break; begin += n; }
        s_page = previous; s_dirty |= DIRTY_WORDS; return true;
    }
    return false;
}
static size_t syllables(const char *out[4], bool *more) {
    size_t count = 0, seen = 0;
    if (more) *more = false;
    if (!s_chinese || !s_nine || !s_raw[0]) return 0;
    // 先展示匹配最长的完整读音；更多键可遍历全部读音，避免只保留四种解释。
    // Prefer longer matching syllables; More cycles through every reading instead of keeping only four.
    for (size_t length = 8; length; --length) for (size_t i = 0;; ++i) {
        const char *word = read_pico_search_syllable(i); if (!word) break;
        if (strlen(word) != length) continue;
        char digits[16]; if (!ui_ime_digits(word, digits, sizeof(digits))) continue;
        if (strlen(s_raw) < length || strncmp(s_raw, digits, length)) continue;
        if (seen++ < s_syllable_offset) continue;
        if (count < 3) out[count++] = word;
        else { if (more) *more = true; return count; }
    }
    return count;
}
// 未识别的九键编码显示为字母组，避免将数字误当作拼音或可提交的输入。
// Show unresolved T9 codes as letter groups, never as Pinyin or committable digits.
static void composition_text(char out[UI_IME_RAW_MAX * 7 + 1]) {
    if (!s_nine) { snprintf(out, UI_IME_RAW_MAX * 7 + 1, "%s", s_raw); return; }
    size_t consumed = 0, used = 0, length = strlen(s_raw);
    out[0] = 0;
    if (s_best_consume <= length && s_best_consume == strlen(s_best_roman)) {
        consumed = s_best_consume; used = strlen(s_best_roman);
        memcpy(out, s_best_roman, used + 1);
    }
    for (size_t i = consumed; i < length; ++i) {
        const char *letters = s_raw[i] >= '2' && s_raw[i] <= '9' ? groups[s_raw[i] - '1'] : "?";
        used += (size_t)snprintf(out + used, UI_IME_RAW_MAX * 7 + 1 - used, "%s[%s]", used ? " " : "", letters);
    }
}
static void paint_words(uint8_t *fb, int top) {
    if (!s_probe && !s_keys_only && s_notice[0]) ui_text_fixed_vc(fb, 28, top + 75, 22, s_notice, EPD_DRAW_ALIGN_LEFT, false);
    else if (!s_probe && !s_keys_only && s_raw[0]) {
        char text[UI_IME_RAW_MAX * 7 + 1]; composition_text(text);
        ui_text_input_composition(fb, text, rect(28, 57, 552, 35, top), 25);
    }
    else if (!s_probe && !s_keys_only) ui_text_fixed_vc(fb, 28, top + 75, 22, s_chinese ? "连续输入拼音，选择词语" : "轻点输入，可切换大小写", EPD_DRAW_ALIGN_LEFT, false);
    EpdRect boxes[5]; size_t count = visible(s_page, boxes, top);
    for (size_t i = 0; i < count; ++i) {
        char text[UI_IME_TEXT_MAX]; snprintf(text, sizeof(text), "%s", s_candidates->items[s_page + i].text);
        while (strlen(text) && ui_text_fixed_width_px(25, text) > boxes[i].width - 12) {
            size_t n = strlen(text) - 1; while (n && ((unsigned char)text[n] & 192) == 128) --n; text[n] = 0;
        }
        key_paint(fb, boxes[i], text, 25, false, i == 0);
    }
    key_paint(fb, rect(24, 104, 44, 56, top), "‹", 30, false, true);
    key_paint(fb, rect(616, 104, 44, 56, top), "›", 30, false, true);
}
static void paint_syllables(uint8_t *fb, int top) {
        const char *choices[4]; bool more; size_t n = syllables(choices, &more);
        for (int i = 0; i < 4; ++i) key_paint(fb, rect(24, 180 + i * 58, 82, 50, top),
            i < (int)n ? choices[i] : i == 3 && (more || s_syllable_offset) ? "更多" : "", 22, i < (int)n && !strcmp(s_preferred, choices[i]), true);
}
void ui_keyboard_draw(uint8_t *fb, int top) {
    if (!s_probe && !s_keys_only) {
    ui_fill_round_rect(fb, rect(16, -12, 652, 534, top), 26, 0xe8);
    ui_draw_round_rect(fb, rect(16, -12, 652, 534, top), 26, 0x60);
    }
    key_paint(fb, rect(24, 0, 144, 44, top), "九宫格", 24, s_nine, s_ascii || !s_candidates);
    key_paint(fb, rect(176, 0, 144, 44, top), "全键盘", 24, !s_nine, false);
    if (!s_probe && !s_keys_only) ui_text_fixed_vc(fb, 651, top + 22, 24, s_ascii ? "英文密码" : s_chinese ? "中文拼音" : s_upper ? "英文大写" : "英文小写", EPD_DRAW_ALIGN_RIGHT, false);
    paint_words(fb, top);
    if (s_panel || !s_nine) {
        static const char *const rows[] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
        for (int row = 0; row < 3; ++row) {
            int len = s_panel ? 10 : (int)strlen(rows[row]);
            int left = s_panel || !row ? 24 : row == 1 ? 55 : 117;
            for (int col = 0; col < len; ++col) {
                char letter[2] = {0}; const char *label;
                if (s_panel == 1) { letter[0] = row == 0 ? "1234567890"[col] : ascii_symbols[(row - 1) * 10 + col][0]; label = letter; }
                else if (s_panel >= 2) label = (s_chinese && !s_ascii && s_panel != 3 ? symbols : ascii_symbols)[s_panel == 3 && row == 0 && col < 6 ? (const int[]){30,31,28,29,26,27}[col] : row * 10 + col];
                else { letter[0] = s_upper ? rows[row][col] - 32 : rows[row][col]; label = letter; }
                key_paint(fb, rect(left + col * 64, 180 + row * 78, 56, 68, top), label, 28, false, false);
            }
        }
        if (s_panel) {
            key_paint(fb, rect(536, 336, 56, 68, top), "重输", 21, false, true);
            key_paint(fb, rect(600, 336, 56, 68, top), "⌫", 28, false, true);
        } else {
            key_paint(fb, rect(24, 336, 80, 68, top), s_upper ? "A" : "a", 30, s_upper, true);
            key_paint(fb, rect(578, 336, 82, 68, top), "⌫", 31, false, true);
        }
    } else {
        paint_syllables(fb, top);
        for (int i = 0; i < 9; ++i) {
            char upper[6]; snprintf(upper, sizeof(upper), "%s", groups[i]);
            for (char *c = upper; *c; ++c) *c -= 32;
            EpdRect r = rect(116 + (i % 3) * 150, 180 + (i / 3) * 78, 140, 68, top);
            key_paint(fb, r, i == 0 ? "选词" : upper, 30, false, false);
        }
        key_paint(fb, rect(568, 180, 92, 68, top), "⌫", 32, false, true);
        key_paint(fb, rect(568, 258, 92, 68, top), "重输", 25, false, true);
        key_paint(fb, rect(568, 336, 92, 68, top), "清空", 25, false, true);
    }
    key_paint(fb, rect(24, 430, 86, 72, top), s_panel ? "ABC" : "123", 25, false, true);
    key_paint(fb, rect(118, 430, 80, 72, top), s_panel == 2 ? "更多" : "符号", 24, false, true);
    key_paint(fb, rect(206, 430, 220, 72, top), s_raw[0] ? "选择" : "空格", 26, false, false);
    key_paint(fb, rect(434, 430, 92, 72, top), s_nine ? "中文" : s_chinese ? "中 / En" : "En / 中", 21, false, true);
    key_paint(fb, rect(534, 430, 126, 72, top), "确定", 28, true, false);
}
static void backspace(void) {
    size_t n = strlen(s_raw);
    if (n) { s_raw[n - 1] = 0; s_preferred[0] = 0; refresh(); }
    else if (s_edit) { ui_text_edit_backspace(s_edit); refresh(); }
    s_notice[0] = 0;
}
static void letter_key(char letter) {
    if (!s_chinese) { char text[2] = {s_upper ? letter - 32 : letter, 0}; insert(text); return; }
    size_t n = strlen(s_raw);
    if (n == UI_IME_RAW_MAX) { snprintf(s_notice, sizeof(s_notice), "拼音过长，请先选词"); return; }
    s_notice[0] = 0; s_raw[n] = letter; s_raw[n + 1] = 0; refresh();
}
static ui_keyboard_result_t tap(int x, int y, int top) {
    if (!s_edit || y < top || y >= top + 510 || x < 24 || x >= 660) return UI_KEYBOARD_NONE;
    if (ui_rect_hit(rect(24, 0, 144, 44, top), x, y) || ui_rect_hit(rect(176, 0, 144, 44, top), x, y)) {
        bool nine = x < 168;
        if ((nine && (s_ascii || !s_candidates)) || (nine == s_nine && !s_panel)) return UI_KEYBOARD_NONE;
        if (s_raw[0] && nine != s_nine) {
            char converted[UI_IME_RAW_MAX + 1];
            if (nine && ui_ime_digits(s_raw, converted, sizeof(converted))) snprintf(s_raw, sizeof(s_raw), "%s", converted);
            else if (!nine && s_best_consume == strlen(s_raw) && s_best_roman[0])
                snprintf(s_raw, sizeof(s_raw), "%s", s_best_roman);
            else { snprintf(s_notice, sizeof(s_notice), "请先选词或重输，再切换键盘"); return UI_KEYBOARD_CHANGED; }
        }
        s_nine = nine; s_panel = 0;
        if (nine) { s_chinese = true; s_upper = false; }
        s_preferred[0] = 0; refresh(); return UI_KEYBOARD_CHANGED;
    }
    if (y >= top + 104 && y < top + 160) {
        if (x < 68) return ui_keyboard_page(-1) ? UI_KEYBOARD_CHANGED : UI_KEYBOARD_NONE;
        if (x >= 616) return ui_keyboard_page(1) ? UI_KEYBOARD_CHANGED : UI_KEYBOARD_NONE;
        EpdRect boxes[5]; size_t count = visible(s_page, boxes, top);
        for (size_t i = 0; i < count; ++i) if (ui_rect_hit(boxes[i], x, y)) { commit(s_page + i); return UI_KEYBOARD_CHANGED; }
        return UI_KEYBOARD_NONE;
    }
    if (y >= top + 430 && y < top + 502) {
        if (x >= 534) {
            if (!s_raw[0]) return UI_KEYBOARD_DONE;
            snprintf(s_notice, sizeof(s_notice), "请先选择词语，再保存");
        } else if (x >= 434) {
            if (s_nine) return UI_KEYBOARD_NONE;
            if (s_raw[0]) snprintf(s_notice, sizeof(s_notice), "请先选词，再切换语言");
            else if (!s_ascii && s_candidates) { s_chinese = !s_chinese; refresh(); }
            else snprintf(s_notice, sizeof(s_notice), "密码使用英文、数字与符号");
        } else if (x >= 206) {
            if (s_raw[0]) { if (!commit(s_page)) snprintf(s_notice, sizeof(s_notice), "未识别拼音，请继续输入或重输"); }
            else insert(" ");
        } else if (s_raw[0]) snprintf(s_notice, sizeof(s_notice), "请先选词，再输入数字符号");
        else if (x < 110) s_panel = s_panel ? 0 : 1;
        else s_panel = s_panel == 2 ? 3 : 2;
        return UI_KEYBOARD_CHANGED;
    }
    if (s_panel || !s_nine) {
        static const char *const rows[] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
        for (int row = 0; row < 3; ++row) {
            if (s_panel && row == 2 && ui_rect_hit(rect(600, 336, 56, 68, top), x, y)) { backspace(); return UI_KEYBOARD_CHANGED; }
            if (s_panel && row == 2 && ui_rect_hit(rect(536, 336, 56, 68, top), x, y)) {
                s_raw[0] = s_preferred[0] = 0;
                s_edit->text[0] = 0; ui_text_edit_place(s_edit, 0); refresh(); return UI_KEYBOARD_CHANGED;
            }
            if (!s_panel && row == 2 && ui_rect_hit(rect(24, 336, 80, 68, top), x, y)) { s_upper = !s_upper; return UI_KEYBOARD_CHANGED; }
            if (!s_panel && row == 2 && ui_rect_hit(rect(578, 336, 82, 68, top), x, y)) { backspace(); return UI_KEYBOARD_CHANGED; }
            int left = s_panel || !row ? 24 : row == 1 ? 55 : 117;
            int len = s_panel ? 10 : (int)strlen(rows[row]);
            for (int col = 0; col < len; ++col) if (ui_rect_hit(rect(left + col * 64, 180 + row * 78, 56, 68, top), x, y)) {
                if (!s_panel) letter_key(rows[row][col]);
                else if (s_panel == 1) { char value[2] = {row == 0 ? "1234567890"[col] : ascii_symbols[(row - 1) * 10 + col][0], 0}; insert(value); }
                else insert((s_chinese && !s_ascii && s_panel != 3 ? symbols : ascii_symbols)[s_panel == 3 && row == 0 && col < 6 ? (const int[]){30,31,28,29,26,27}[col] : row * 10 + col]);
                return UI_KEYBOARD_CHANGED;
            }
        }
    } else {
        const char *choices[4]; bool more; size_t n = syllables(choices, &more);
        if (ui_rect_hit(rect(24, 354, 82, 50, top), x, y) && (more || s_syllable_offset)) {
            s_syllable_offset = more ? s_syllable_offset + 3 : 0;
            s_dirty |= DIRTY_SYLLABLES; return UI_KEYBOARD_CHANGED;
        }
        for (size_t i = 0; i < n; ++i) if (ui_rect_hit(rect(24, 180 + (int)i * 58, 82, 50, top), x, y)) {
            snprintf(s_preferred, sizeof(s_preferred), "%s", choices[i]); refresh(); return UI_KEYBOARD_CHANGED;
        }
        if (ui_rect_hit(rect(568, 180, 92, 68, top), x, y)) { backspace(); return UI_KEYBOARD_CHANGED; }
        if (ui_rect_hit(rect(568, 258, 92, 68, top), x, y)) { s_raw[0] = s_preferred[0] = s_notice[0] = 0; refresh(); return UI_KEYBOARD_CHANGED; }
        if (ui_rect_hit(rect(568, 336, 92, 68, top), x, y)) {
            s_raw[0] = s_preferred[0] = s_notice[0] = 0;
            s_edit->text[0] = 0; ui_text_edit_place(s_edit, 0); refresh(); return UI_KEYBOARD_CHANGED;
        }
        for (int i = 0; i < 9; ++i) if (ui_rect_hit(rect(116 + (i % 3) * 150, 180 + (i / 3) * 78, 140, 68, top), x, y)) {
            if (!i) {
                if (!commit(s_page)) snprintf(s_notice, sizeof(s_notice), "请先输入拼音并选择词语");
                return UI_KEYBOARD_CHANGED;
            }
            size_t len = strlen(s_raw);
            if (len < UI_IME_RAW_MAX) { s_notice[0] = 0; s_raw[len] = (char)('1' + i); s_raw[len + 1] = 0; refresh(); }
            else snprintf(s_notice, sizeof(s_notice), "拼音过长，请先选词");
            return UI_KEYBOARD_CHANGED;
        }
    }
    return UI_KEYBOARD_NONE;
}

static void feedback_dirty(EpdRect area) {
    s_feedback_area = s_feedback_area.width ? ui_rect_union(s_feedback_area, area) : area;
    s_dirty |= DIRTY_FEEDBACK;
}
bool ui_keyboard_release(void) {
    ui_keyboard_hold_cancel();
    if (!s_pressed) return false;
    feedback_dirty(s_pressed_area); s_pressed = false; return true;
}
bool ui_keyboard_press(int x, int y, int top, int64_t now_ms) {
    s_words_idle_since = -1;
    ui_keyboard_release();
    ui_keyboard_hold_start(x, y, top, now_ms);
    if (!s_edit || x < 0 || y < 0) return false;
    s_probe = true; s_probe_x = x; s_probe_y = y; s_key_clip = (EpdRect){0};
    ui_keyboard_draw(NULL, top); s_probe = false;
    if (!s_key_clip.width) return false;
    s_pressed_area = s_key_clip; s_pressed = true;
    feedback_dirty(s_pressed_area); return true;
}
void ui_keyboard_hold_cancel(void) { s_delete_hold = false; }
void ui_keyboard_hold_start(int x, int y, int top, int64_t now_ms) {
    ui_keyboard_hold_cancel();
    if (!s_edit || now_ms < 0) return;
    EpdRect area = s_panel ? rect(600, 336, 56, 68, top)
        : s_nine ? rect(568, 180, 92, 68, top) : rect(578, 336, 82, 68, top);
    if (!ui_rect_hit(area, x, y)) return;
    s_delete_area = area; s_delete_top = top;
    s_delete_hold = true; s_delete_last_ms = now_ms; s_delete_next_ms = now_ms + UI_LONG_PRESS_MS;
}
ui_keyboard_result_t ui_keyboard_hold_tick(bool held, int x, int y, int64_t now_ms) {
    if (!s_delete_hold) return UI_KEYBOARD_NONE;
    if (!s_edit || !held || now_ms < s_delete_last_ms || !ui_rect_hit(s_delete_area, x, y)) {
        ui_keyboard_hold_cancel(); return UI_KEYBOARD_NONE;
    }
    s_delete_last_ms = now_ms;
    if (now_ms < s_delete_next_ms) return UI_KEYBOARD_NONE;
    if (!s_raw[0] && !s_edit->cursor) { ui_keyboard_hold_cancel(); return UI_KEYBOARD_NONE; }
    // 推屏耗时不补发连删，保持可控；每次复用普通删除的脏区标记。
    // Never catch up after display delays; reuse ordinary backspace dirty-region tracking.
    s_delete_next_ms = now_ms + 120;
    ui_keyboard_result_t result = ui_keyboard_tap(s_delete_area.x + s_delete_area.width / 2,
        s_delete_area.y + s_delete_area.height / 2, s_delete_top, now_ms);
    if (result == UI_KEYBOARD_NONE) ui_keyboard_hold_cancel();
    return result;
}

ui_keyboard_result_t ui_keyboard_idle_tick(bool touching, int64_t now_ms) {
    // 连打、按住或多指时不整理；一次停顿只推一次，不追补累计的候选变化。
    // Skip typing, held keys and multitouch; settle once per pause without replaying accumulated changes.
    if (!s_edit || s_dirty || touching || s_pressed || s_delete_hold ||
        now_ms < 0 || s_word_updates < WORDS_SETTLE_CHANGES) {
        s_words_idle_since = -1; return UI_KEYBOARD_NONE;
    }
    if (s_words_idle_since < 0 || now_ms < s_words_idle_since) {
        s_words_idle_since = now_ms; return UI_KEYBOARD_NONE;
    }
    if (now_ms - s_words_idle_since < WORDS_SETTLE_IDLE_MS) return UI_KEYBOARD_NONE;
    s_dirty |= DIRTY_WORDS_SETTLE;
    return UI_KEYBOARD_CHANGED;
}

ui_keyboard_result_t ui_keyboard_tap(int x, int y, int top, int64_t now_ms) {
    (void)now_ms;
    if (!s_edit) return UI_KEYBOARD_NONE;
    uint32_t text = stamp(s_edit->text), raw = stamp(s_raw), notice = stamp(s_notice), preferred = stamp(s_preferred);
    size_t cursor = s_edit->cursor; unsigned revision = s_candidates_revision;
    bool nine = s_nine, chinese = s_chinese, upper = s_upper, pending = s_raw[0] != 0;
    unsigned panel = s_panel;
    ui_keyboard_result_t result = tap(x, y, top);
    if (result != UI_KEYBOARD_CHANGED) return result == UI_KEYBOARD_NONE && s_dirty ? UI_KEYBOARD_CHANGED : result;
    if (text != stamp(s_edit->text) || cursor != s_edit->cursor) s_dirty |= DIRTY_FIELD;
    if (nine != s_nine || chinese != s_chinese || upper != s_upper || panel != s_panel) s_dirty |= DIRTY_LAYOUT;
    else {
        if (revision != s_candidates_revision || raw != stamp(s_raw) || notice != stamp(s_notice)) s_dirty |= DIRTY_WORDS;
        if (s_nine && s_chinese && (raw != stamp(s_raw) || preferred != stamp(s_preferred))) s_dirty |= DIRTY_SYLLABLES;
        if (pending != (s_raw[0] != 0)) s_dirty |= DIRTY_SPACE;
    }
    return s_dirty ? result : UI_KEYBOARD_NONE;
}
static void include(ui_keyboard_update_t *update, EpdRect area) {
    update->area = update->area.width ? ui_rect_union(update->area, area) : area;
}
ui_keyboard_update_t ui_keyboard_update(uint8_t *fb, int top, EpdRect field, int px,
                                        bool masked, const char *suffix, bool force_field) {
    ui_keyboard_update_t update = {.layout = (s_dirty & DIRTY_LAYOUT) != 0,
        .settle = s_dirty == DIRTY_WORDS_SETTLE && !force_field};
    if (!s_edit) return update;
    if (force_field || (s_dirty & DIRTY_FIELD)) {
        ui_clear_rect_fast(fb, field);
        ui_text_input_draw(fb, s_edit, field, px, masked, suffix);
        include(&update, field); update.field = true;
    }
    if (update.layout) {
        ui_keyboard_draw(fb, top);
        include(&update, rect(16, -12, 652, 534, top));
        s_word_updates = 0; s_words_idle_since = -1;
    } else {
        if (s_dirty & (DIRTY_WORDS | DIRTY_WORDS_SETTLE)) {
            EpdRect area = rect(24, 52, 636, 114, top);
            epd_fill_rect(area, 0xe8, fb); paint_words(fb, top); include(&update, area);
            if (update.settle) s_word_updates = 0;
            else if ((s_dirty & DIRTY_WORDS) && s_word_updates < WORDS_SETTLE_CHANGES) ++s_word_updates;
            s_words_idle_since = -1;
        }
        if (s_dirty & DIRTY_SYLLABLES) {
            EpdRect area = rect(24, 180, 82, 224, top);
            epd_fill_rect(area, 0xe8, fb); paint_syllables(fb, top); include(&update, area);
        }
        if (s_dirty & DIRTY_SPACE) {
            EpdRect area = rect(206, 430, 220, 72, top);
            key_paint(fb, area, s_raw[0] ? "选择" : "空格", 26, false, false); include(&update, area);
        }
    }
    if (!update.layout && (s_dirty & DIRTY_FEEDBACK)) {
        s_keys_only = true; s_key_clip = s_feedback_area;
        ui_keyboard_draw(fb, top); s_keys_only = false;
        include(&update, s_feedback_area);
    }
    s_feedback_area = (EpdRect){0}; s_dirty = 0;
    return update;
}
