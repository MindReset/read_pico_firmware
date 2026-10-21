/* SPDX-License-Identifier: Apache-2.0
 * 中文：有界动态规划组词；常用词优先，未知词可选单字前缀，选词不丢剩余拼音。
 * English: Bounded word composition; prefer common phrases, fall back to character prefixes and retain unconsumed Pinyin.
 */
#include "ui_ime.h"
#include "ui_ime_phrases.h"
#include "read_pico_search.h"
#include "esp_heap_caps.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
typedef struct { int score; char text[UI_IME_TEXT_MAX], roman[UI_IME_RAW_MAX + 1]; } path_t;
typedef struct { uint32_t points[1]; unsigned char count, limit; } glyph_cache_t;
typedef struct {
    path_t paths[UI_IME_RAW_MAX + 1];
    uint16_t matches[512];
    uint8_t seen_chars[(0x9fff - 0x4e00 + 8) / 8];
    glyph_cache_t glyphs[];
} workspace_t;
static workspace_t *s_workspace;
static size_t s_syllable_count;
void ui_ime_release(void) { free(s_workspace); s_workspace = NULL; s_syllable_count = 0; }
static bool workspace(void) {
    if (s_workspace) return true;
    size_t count = 0;
    while (read_pico_search_syllable(count)) ++count;
    if (count > 512) return false;
    s_workspace = heap_caps_malloc(sizeof(*s_workspace) + count * sizeof(glyph_cache_t), CAPS);
    if (!s_workspace) return false;
    s_syllable_count = count;
    memset(s_workspace->glyphs, 0, count * sizeof(glyph_cache_t));
    return true;
}
// 同一拼音只查字表一次；动态规划缓冲区复用，避免每个按键反复分配与扫描字表。
// Cache each syllable lookup and reuse DP storage instead of allocating and scanning the dictionary per key.
static const glyph_cache_t *characters(size_t index, unsigned limit) {
    if (index >= s_syllable_count) return NULL;
    glyph_cache_t *entry = &s_workspace->glyphs[index];
    if (entry->limit < limit) {
        entry->count = read_pico_search_candidates(read_pico_search_syllable(index), entry->points, limit, 0);
        entry->limit = limit;
    }
    return entry;
}
static char digit(char c) {
    static const char map[] = "22233344455566677778889999";
    return c >= 'a' && c <= 'z' ? map[c - 'a'] : 0;
}
bool ui_ime_digits(const char *roman, char *out, size_t cap) {
    if (!roman || !out || !cap) return false;
    size_t n = strlen(roman);
    if (n >= cap) return false;
    for (size_t i = 0; i < n; ++i) { out[i] = digit(roman[i]); if (!out[i]) return false; }
    out[n] = 0; return true;
}
static bool prefix(const char *raw, const char *roman, bool nine) {
    size_t n = strlen(roman);
    if (strlen(raw) < n) return false;
    for (size_t i = 0; i < n; ++i) if (raw[i] != (nine ? digit(roman[i]) : roman[i])) return false;
    return true;
}
static void glyph(uint32_t cp, char text[5]) {
    if (cp < 128) { text[0] = (char)cp; text[1] = 0; }
    else if (cp < 2048) { text[0] = 0xc0 | (cp >> 6); text[1] = 0x80 | (cp & 63); text[2] = 0; }
    else { text[0] = 0xe0 | (cp >> 12); text[1] = 0x80 | ((cp >> 6) & 63); text[2] = 0x80 | (cp & 63); text[3] = 0; }
}
typedef struct { ui_ime_result_t *out; size_t skip, seen; } pager_t;
static bool add(pager_t *page, const char *text, const char *roman, size_t consume) {
    if (strlen(text) >= UI_IME_TEXT_MAX) return false;
    if (page->seen++ < page->skip) return false;
    ui_ime_result_t *out = page->out;
    if (out->count == UI_IME_CANDIDATES) { out->has_more = true; return true; }
    ui_ime_candidate_t *item = &out->items[out->count++];
    snprintf(item->text, sizeof(item->text), "%s", text);
    snprintf(item->roman, sizeof(item->roman), "%s", roman);
    item->consume = consume;
    return false;
}
static void extend(path_t *paths, size_t at, size_t length, const char *text, const char *roman, int score) {
    size_t bytes = strlen(paths[at].text), more = strlen(text);
    path_t *next = &paths[at + length];
    if (bytes + more >= sizeof(next->text) || paths[at].score + score <= next->score) return;
    next->score = paths[at].score + score;
    memcpy(next->text, paths[at].text, bytes); memcpy(next->text + bytes, text, more + 1);
    size_t used = strlen(paths[at].roman);
    memcpy(next->roman, paths[at].roman, used);
    memcpy(next->roman + used, roman, strlen(roman) + 1);
}
static bool preferred_ok(const char *roman, const char *preferred, size_t at) {
    return at || !preferred || !preferred[0] || !strncmp(roman, preferred, strlen(preferred));
}
bool ui_ime_candidates_page(const char *raw, bool nine, const char *preferred,
                             const char *preceding, size_t skip, ui_ime_result_t *out) {
    if (!out || !raw) return false;
    memset(out, 0, sizeof(*out));
    pager_t page = {.out = out, .skip = skip};
    size_t n = strnlen(raw, UI_IME_RAW_MAX + 1);
    if (n > UI_IME_RAW_MAX) return false;
    for (size_t i = 0; i < n; ++i) if (nine ? (raw[i] < '2' || raw[i] > '9') : (raw[i] < 'a' || raw[i] > 'z')) return false;
    if (!n) {
        size_t bytes = preceding ? strlen(preceding) : 0;
        // 已提交文字后建议词库中可接续的词；不猜未确认的拼音。
        // Offer lexicon continuations after committed text, never guessing unconfirmed Pinyin.
        for (size_t back = bytes < 24 ? bytes : 24; back; --back) {
            const char *suffix = preceding + bytes - back;
            if (((unsigned char)*suffix & 192) == 128) continue;
            bool matched = false;
            for (size_t i = 0; i < sizeof(phrases) / sizeof(phrases[0]); ++i) {
                if (strncmp(phrases[i].text, suffix, back) || strlen(phrases[i].text) <= back) continue;
                matched = true;
                bool duplicate = false;
                for (size_t j = 0; j < i; ++j)
                    if (!strncmp(phrases[j].text, suffix, back) && !strcmp(phrases[i].text + back, phrases[j].text + back)) { duplicate = true; break; }
                if (!duplicate && add(&page, phrases[i].text + back, "", 0)) return true;
            }
            if (matched) break;
        }
        return true;
    }
    if (!workspace()) return false;
    path_t *paths = s_workspace->paths;
    memset(paths, 0, (n + 1) * sizeof(*paths));
    for (size_t i = 1; i <= n; ++i) paths[i].score = -1000000;
    for (size_t at = 0; at < n; ++at) {
        if (paths[at].score < 0) continue;
        for (size_t i = 0; i < sizeof(phrases) / sizeof(phrases[0]); ++i) {
            const char *roman = phrases[i].roman;
            if (preferred_ok(roman, preferred, at) && prefix(raw + at, roman, nine))
                extend(paths, at, strlen(roman), phrases[i].text, roman, (int)strlen(roman) * 100 + 40);
        }
        for (size_t i = 0;; ++i) {
            const char *roman = read_pico_search_syllable(i); if (!roman) break;
            if (!preferred_ok(roman, preferred, at) || !prefix(raw + at, roman, nine)) continue;
            const glyph_cache_t *entry = characters(i, 1); char text[5];
            if (entry && entry->count) {
                glyph(entry->points[0], text); extend(paths, at, strlen(roman), text, roman, (int)strlen(roman) * 100 - 10);
            }
        }
    }
    // 精确词语、完整组词、逐读音单字依次展开；跨批次去重，不限制前八个同音字。
    // Enumerate phrases, the best composition and all homophones, deduplicated across batches.
    bool best_known = false;
    for (size_t i = 0; i < sizeof(phrases) / sizeof(phrases[0]); ++i) {
        const char *roman = phrases[i].roman;
        if (strlen(roman) != n || !preferred_ok(roman, preferred, 0) || !prefix(raw, roman, nine)) continue;
        if (!strcmp(paths[n].text, phrases[i].text)) best_known = true;
        if (add(&page, phrases[i].text, roman, n)) return true;
    }
    if (paths[n].score >= 0 && !best_known && add(&page, paths[n].text, paths[n].roman, n)) return true;
    for (size_t length = n; length; --length) {
        if (length != n) for (size_t i = 0; i < sizeof(phrases) / sizeof(phrases[0]); ++i)
            if (strlen(phrases[i].roman) == length && preferred_ok(phrases[i].roman, preferred, 0) && prefix(raw, phrases[i].roman, nine))
                if (add(&page, phrases[i].text, phrases[i].roman, length)) return true;
        memset(s_workspace->seen_chars, 0, sizeof(s_workspace->seen_chars));
        size_t matches = 0;
        for (size_t i = 0; i < s_syllable_count; ++i) {
            const char *roman = read_pico_search_syllable(i);
            if (strlen(roman) == length && preferred_ok(roman, preferred, 0) && prefix(raw, roman, nine))
                s_workspace->matches[matches++] = (uint16_t)i;
        }
        // 九键多个读音的同音字交替排列，避免一个读音的长字表挡住其他读音。
        // Interleave homophones across T9 readings instead of hiding other readings behind one long list.
        for (size_t rank = 0; matches; ++rank) {
            bool found = false;
            for (size_t m = 0; m < matches; ++m) {
                const char *roman = read_pico_search_syllable(s_workspace->matches[m]);
                uint32_t cp;
                if (!read_pico_search_candidates(roman, &cp, 1, rank)) continue;
                found = true;
                if (cp < 0x4e00 || cp > 0x9fff) continue;
                unsigned index = cp - 0x4e00, mask = 1u << (index & 7);
                if (s_workspace->seen_chars[index / 8] & mask) continue;
                s_workspace->seen_chars[index / 8] |= mask;
                char text[5]; glyph(cp, text);
                if (length == n && paths[n].score >= 0 && !strcmp(text, paths[n].text)) continue;
                if (add(&page, text, roman, length)) return true;
            }
            if (!found) break;
        }

    }
    return true;
}
bool ui_ime_candidates(const char *raw, bool nine, const char *preferred,
                        const char *preceding, ui_ime_result_t *out) {
    return ui_ime_candidates_page(raw, nine, preferred, preceding, 0, out);
}
