/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 无堆分配的离线书名匹配；字典只读，调用可重入。
 * Allocation-free offline filename matching; immutable dictionary and reentrant calls.
 * 冻结：逐字多音候选，不猜词义；状态集合线性推进，不枚举读音组合。
 * Frozen: per-character polyphonic alternatives without semantic guesses; advance state sets without enumerating pronunciation combinations.
 */
#include "read_pico_search.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "search_table.h"

static unsigned char lower(unsigned char c) {
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}
static bool letter(uint32_t c) { return c >= 'a' && c <= 'z'; }
static bool digit(uint32_t c) { return c >= '0' && c <= '9'; }
static bool space(uint32_t c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
static bool bounded_length(const char* s, size_t limit, size_t* length) {
    if (!s) return false;
    size_t n = 0;
    while (n <= limit && s[n]) ++n;
    *length = n;
    return n <= limit;
}
static bool utf8(const char* s, size_t length, size_t* index, uint32_t* out) {
    if (*index >= length) return false;
    unsigned char c = (unsigned char)s[(*index)++];
    if (c < 128) { *out = c; return true; }
    unsigned following;
    uint32_t value, minimum;
    if (c >= 0xc2 && c <= 0xdf) { following = 1; value = c & 31; minimum = 128; }
    else if (c >= 0xe0 && c <= 0xef) { following = 2; value = c & 15; minimum = 2048; }
    else if (c >= 0xf0 && c <= 0xf4) { following = 3; value = c & 7; minimum = 65536; }
    else return false;
    while (following--) {
        if (*index >= length) return false;
        c = (unsigned char)s[(*index)++];
        if ((c & 0xc0) != 0x80) return false;
        value = (value << 6) | (c & 63);
    }
    if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    *out = value; return true;
}
static bool valid_utf8(const char* s, size_t length) {
    size_t i = 0; uint32_t cp;
    while (i < length) if (!utf8(s, length, &i, &cp)) return false;
    return true;
}
static bool equal_ascii(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; ++i) if (lower((unsigned char)a[i]) != lower((unsigned char)b[i])) return false;
    return true;
}
static size_t stem_length(const char* name, size_t n) {
    if (n >= 4 && equal_ascii(name + n - 4, ".txt", 4)) return n - 4;
    if (n >= 5 && equal_ascii(name + n - 5, ".epub", 5)) return n - 5;
    return n;
}
static int lookup(uint32_t cp) {
    size_t low = 0, high = sizeof(search_codepoints) / sizeof(search_codepoints[0]);
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (search_codepoints[mid] < cp) low = mid + 1;
        else high = mid;
    }
    return low < sizeof(search_codepoints) / sizeof(search_codepoints[0]) && search_codepoints[low] == cp
        ? (int)search_groups[low] : -1;
}
// 位j表示已匹配查询前j字节；每步允许重新开始子串。/ Bit j means j matched query bytes; every step can start a new substring.
static bool advance(uint64_t* state, unsigned char c, const uint64_t masks[128], uint64_t finish) {
    uint64_t matched = *state & masks[c];
    *state = (matched << 1) | 1;
    return (matched & finish) != 0;
}
bool read_pico_search_match(const char* filename, const char* query) {
    size_t length, query_length;
    if (!bounded_length(filename, 255, &length) || !bounded_length(query, 64, &query_length) ||
        !valid_utf8(filename, length) || !valid_utf8(query, query_length)) return false;
    length = stem_length(filename, length);
    if (!query_length) return true;
    // 原文独立匹配，中文与符号不必有拼音表条目。/ Literal matching also supports Chinese and symbols absent from the table.
    if (query_length <= length) {
        for (size_t i = 0; i <= length - query_length; ++i)
            if (equal_ascii(filename + i, query, query_length)) return true;
    }
    uint64_t masks[128] = {0};
    size_t i = 0, n = 0;
    while (i < query_length) {
        uint32_t cp;
        if (!utf8(query, query_length, &i, &cp)) return false;
        if (space(cp)) continue;
        if (cp == 0xfc || cp == 0xdc) cp = 'v';
        if (cp < 128) cp = lower((unsigned char)cp);
        if (!letter(cp) && !digit(cp)) return false;
        masks[cp] |= UINT64_C(1) << n++;
    }
    if (!n) return true;
    uint64_t finish = UINT64_C(1) << (n - 1);
    uint64_t full = 1, initials = 1;
    bool english_word = false;
    i = 0;
    while (i < length) {
        uint32_t cp;
        if (!utf8(filename, length, &i, &cp)) return false;
        if (cp < 128) {
            cp = lower((unsigned char)cp);
            if (letter(cp) || digit(cp)) {
                if (advance(&full, cp, masks, finish)) return true;
                if ((!english_word || digit(cp)) && advance(&initials, cp, masks, finish)) return true;
                english_word = letter(cp);
            } else {
                english_word = false;
            }
            continue;
        }
        english_word = false;
        int group = lookup(cp);
        if (group < 0) { full = initials = 1; continue; }
        uint64_t next_full = 1, next_initials = 1;
        for (unsigned j = search_group_offsets[group]; j < search_group_offsets[group + 1]; ++j) {
            const char* syllable = search_syllables + search_syllable_offsets[search_readings[j]];
            uint64_t branch = full, initial_branch = initials;
            if (advance(&initial_branch, (unsigned char)syllable[0], masks, finish)) return true;
            next_initials |= initial_branch;
            for (const char* at = syllable; *at; ++at)
                if (advance(&branch, (unsigned char)*at, masks, finish)) return true;
            next_full |= branch;
        }
        full = next_full; initials = next_initials;
    }
    return false;
}
