/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：单遍提取章节文字，折叠空白并保留非空块、标题和图片占位。
 * English: Extract chapter text in one pass, preserving nonempty blocks, headings and image placeholders.
 *
 * 冻结：不执行脚本、不加载资源；输出有界，失败释放全部临时分配。
 * Frozen: Never execute scripts or load resources; bound output and release temporary allocations on failure.
 */
#include "html_text.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"

#define PSRAM_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

typedef struct {
    html_text_t text;
    size_t text_cap, block_cap, start;
    bool active, heading, block_heading, space;
} writer_t;

static unsigned char lower(unsigned char c) {
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

static bool ascii_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static bool name_char(unsigned char c) {
    c = lower(c);
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ':' || c == '-' || c == '_';
}

static bool name_equal(const char* name, const char* expected) {
    return strcmp(name, expected) == 0;
}

void html_text_free(html_text_t* text) {
    if (!text) return;
    free(text->utf8);
    html_blocks_free(text->blocks, text->count);
    *text = (html_text_t){0};
}

void html_blocks_free(blk_t* blocks, size_t count) {
    if (!blocks) return;
    for (size_t i = 0; i < count; ++i) { free(blocks[i].image_src); free(blocks[i].image); }
    free(blocks);
}

static esp_err_t reserve_text(writer_t* w, size_t extra) {
    if (extra > HTML_TEXT_MAX_BYTES - w->text.len) return ESP_ERR_INVALID_SIZE;
    size_t need = w->text.len + extra + 1;
    if (need <= w->text_cap) return ESP_OK;
    size_t cap = w->text_cap ? w->text_cap : 256;
    while (cap < need) {
        if (cap > (HTML_TEXT_MAX_BYTES + 1) / 2) { cap = HTML_TEXT_MAX_BYTES + 1; break; }
        cap *= 2;
    }
    char* text = heap_caps_realloc(w->text.utf8, cap, PSRAM_CAPS);
    if (!text) return ESP_ERR_NO_MEM;
    w->text.utf8 = text;
    w->text_cap = cap;
    return ESP_OK;
}

static esp_err_t finish_block(writer_t* w) {
    w->space = false;
    if (!w->active) return ESP_OK;
    if (w->text.count == HTML_TEXT_MAX_BLOCKS) return ESP_ERR_INVALID_SIZE;
    if (w->text.count == w->block_cap) {
        size_t cap = w->block_cap ? w->block_cap * 2 : 32;
        if (cap > HTML_TEXT_MAX_BLOCKS) cap = HTML_TEXT_MAX_BLOCKS;
        blk_t* blocks = heap_caps_realloc(w->text.blocks, cap * sizeof(*blocks), PSRAM_CAPS);
        if (!blocks) return ESP_ERR_NO_MEM;
        w->text.blocks = blocks;
        w->block_cap = cap;
    }
    w->text.blocks[w->text.count++] = (blk_t){
        .offset = w->start, .len = w->text.len - w->start, .heading = w->block_heading,
    };
    w->active = false;
    return ESP_OK;
}

static esp_err_t emit(writer_t* w, uint32_t cp) {
    if (cp == 0xa0 || (cp < 128 && ascii_space((unsigned char)cp))) {
        if (w->active) w->space = true;
        return ESP_OK;
    }
    char bytes[4];
    size_t n;
    if (cp < 0x80) { bytes[0] = (char)cp; n = 1; }
    else if (cp < 0x800) {
        bytes[0] = (char)(0xc0 | (cp >> 6)); bytes[1] = (char)(0x80 | (cp & 63)); n = 2;
    } else if (cp < 0x10000) {
        bytes[0] = (char)(0xe0 | (cp >> 12)); bytes[1] = (char)(0x80 | ((cp >> 6) & 63));
        bytes[2] = (char)(0x80 | (cp & 63)); n = 3;
    } else {
        bytes[0] = (char)(0xf0 | (cp >> 18)); bytes[1] = (char)(0x80 | ((cp >> 12) & 63));
        bytes[2] = (char)(0x80 | ((cp >> 6) & 63)); bytes[3] = (char)(0x80 | (cp & 63)); n = 4;
    }
    bool separator = !w->active && w->text.count > 0;
    esp_err_t err = reserve_text(w, n + (separator || w->space ? 1 : 0));
    if (err != ESP_OK) return err;
    if (!w->active) {
        if (separator) w->text.utf8[w->text.len++] = '\n';
        w->start = w->text.len;
        w->active = true;
        w->block_heading = w->heading;
    } else if (w->space) w->text.utf8[w->text.len++] = ' ';
    w->space = false;
    memcpy(w->text.utf8 + w->text.len, bytes, n);
    w->text.len += n;
    return ESP_OK;
}

// 实体查找有固定上限；未知名称原样保留，非法数值替换为 U+FFFD。
// Bound entity lookahead; preserve unknown names and replace invalid numeric values with U+FFFD.
static size_t entity(const char* s, size_t len, uint32_t* cp) {
    size_t end = 1;
    while (end < len && end <= 32 && s[end] != ';' && !ascii_space((unsigned char)s[end]) && s[end] != '&' && s[end] != '<') end++;
    if (end >= len || end > 32 || s[end] != ';') return 0;
    static const struct { const char* name; uint32_t cp; } names[] = {
        {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", ' '},
    };
    if (end > 2 && s[1] == '#') {
        size_t i = 2;
        unsigned base = 10;
        if (i < end && (s[i] == 'x' || s[i] == 'X')) { base = 16; i++; }
        if (i == end) return 0;
        uint32_t value = 0;
        bool overflow = false;
        for (; i < end; i++) {
            unsigned char c = lower((unsigned char)s[i]);
            unsigned digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : 255;
            if (digit >= base) return 0;
            if (value > (0x10ffffu - digit) / base) overflow = true;
            else if (!overflow) value = value * base + digit;
        }
        *cp = overflow || !value || (value >= 0xd800 && value <= 0xdfff) ? 0xfffd : value;
        return end + 1;
    }
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (strlen(names[i].name) == end - 1 && memcmp(s + 1, names[i].name, end - 1) == 0) {
            *cp = names[i].cp;
            return end + 1;
        }
    }
    return 0;
}

static size_t utf8(const char* s, size_t len, uint32_t* cp) {
    const unsigned char* p = (const unsigned char*)s;
    if (p[0] && p[0] < 0x80) { *cp = p[0]; return 1; }
    size_t n = p[0] >= 0xc2 && p[0] <= 0xdf ? 2 : p[0] >= 0xe0 && p[0] <= 0xef ? 3 : p[0] >= 0xf0 && p[0] <= 0xf4 ? 4 : 0;
    if (!n || n > len) return 0;
    uint32_t value = p[0] & (0x7f >> n);
    for (size_t i = 1; i < n; i++) {
        if ((p[i] & 0xc0) != 0x80) return 0;
        value = (value << 6) | (p[i] & 63);
    }
    if ((n == 2 && value < 0x80) || (n == 3 && value < 0x800) || (n == 4 && value < 0x10000) ||
        value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return 0;
    *cp = value;
    return n;
}

static bool block_tag(const char* name) {
    static const char* const tags[] = {"p", "div", "li", "tr", "br", "hr", "blockquote"};
    if (name[0] == 'h' && name[1] >= '1' && name[1] <= '6' && !name[2]) return true;
    for (size_t i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) if (name_equal(name, tags[i])) return true;
    return false;
}

static bool image_tag(const char* name) {
    const char* local = strrchr(name, ':');
    if (local) name = local + 1;
    return name_equal(name, "img") || name_equal(name, "image");
}

// 只保留有界引用；资源解析由 EPUB 后端处理，不在 HTML 转换器内打开文件。
// Retain a bounded reference only; EPUB resolves resources without file access in the HTML converter.
static char* image_source(const char* attrs, size_t len) {
    size_t at = 0;
    while (at < len) {
        while (at < len && ascii_space((unsigned char)attrs[at])) ++at;
        size_t start = at;
        while (at < len && name_char((unsigned char)attrs[at])) ++at;
        if (at == start) break;
        size_t name_len = at - start;
        bool wanted = (name_len == 3 && !memcmp(attrs + start, "src", 3)) ||
                      (name_len == 4 && !memcmp(attrs + start, "href", 4)) ||
                      (name_len == 10 && !memcmp(attrs + start, "xlink:href", 10));
        while (at < len && ascii_space((unsigned char)attrs[at])) ++at;
        if (at == len || attrs[at++] != '=') break;
        while (at < len && ascii_space((unsigned char)attrs[at])) ++at;
        if (at == len || (attrs[at] != '\'' && attrs[at] != '"')) break;
        char quote = attrs[at++]; start = at;
        while (at < len && attrs[at] != quote) ++at;
        if (at == len) break;
        size_t end = at++;
        if (!wanted) continue;
        char value[512]; size_t used = 0;
        for (size_t i = start; i < end;) {
            uint32_t cp;
            size_t n = attrs[i] == '&' ? entity(attrs + i, end - i, &cp) : 0;
            if (!n) n = utf8(attrs + i, end - i, &cp);
            if (!n || cp < 32 || used + 4 >= sizeof(value)) return NULL;
            i += n;
            if (cp < 0x80) value[used++] = (char)cp;
            else if (cp < 0x800) { value[used++] = 0xc0 | (cp >> 6); value[used++] = 0x80 | (cp & 63); }
            else if (cp < 0x10000) { value[used++] = 0xe0 | (cp >> 12); value[used++] = 0x80 | ((cp >> 6) & 63); value[used++] = 0x80 | (cp & 63); }
            else { value[used++] = 0xf0 | (cp >> 18); value[used++] = 0x80 | ((cp >> 12) & 63); value[used++] = 0x80 | ((cp >> 6) & 63); value[used++] = 0x80 | (cp & 63); }
        }
        if (!used) return NULL;
        char* result = heap_caps_realloc(NULL, used + 1, PSRAM_CAPS);
        if (result) { memcpy(result, value, used); result[used] = 0; }
        return result;
    }
    return NULL;
}

esp_err_t html_to_blocks(const char* html, size_t len, html_text_t* out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = (html_text_t){0};
    if (!html && len) return ESP_ERR_INVALID_ARG;
    if (len > HTML_TEXT_MAX_BYTES) return ESP_ERR_INVALID_SIZE;
    writer_t w = {0};
    esp_err_t err = ESP_OK;
    char skip[16] = "";
    bool resume_head = false;
    size_t pos = len >= 3 && memcmp(html, "\xef\xbb\xbf", 3) == 0 ? 3 : 0;
    while (pos < len) {
        if ((!skip[0] || name_equal(skip, "head")) && len - pos >= 4 && memcmp(html + pos, "<!--", 4) == 0) {
            pos += 4;
            while (len - pos >= 3 && memcmp(html + pos, "-->", 3)) pos++;
            pos = len - pos >= 3 ? pos + 3 : len;
            continue;
        }
        if (html[pos] == '<') {
            size_t at = pos + 1;
            bool closing = at < len && html[at] == '/';
            if (closing) at++;
            size_t start = at;
            while (at < len && name_char((unsigned char)html[at])) at++;
            char name[16] = "";
            size_t name_len = at - start;
            if (name_len < sizeof(name)) {
                for (size_t i = 0; i < name_len; i++) name[i] = (char)lower((unsigned char)html[start + i]);
            }
            bool head_child = name_equal(skip, "head") && !closing &&
                              (name_equal(name, "script") || name_equal(name, "style"));
            if (skip[0] && !head_child && (!closing || !name_equal(name, skip))) { pos++; continue; }
            bool declaration = start < len && (html[start] == '!' || html[start] == '?');
            unsigned char initial = start < len ? lower((unsigned char)html[start]) : 0;
            if ((name_len && initial >= 'a' && initial <= 'z') || declaration) {
                char quote = 0;
                size_t end = at;
                for (; end < len; end++) {
                    char c = html[end];
                    if (quote) { if (c == quote) quote = 0; }
                    else if (c == '\'' || c == '"') quote = c;
                    else if (c == '>') break;
                }
                if (end == len) { err = ESP_ERR_INVALID_RESPONSE; goto fail; }
                bool self_closing = end > at && html[end - 1] == '/';
                pos = end + 1;
                if (skip[0]) {
                    if (head_child) {
                        if (!self_closing) { memcpy(skip, name, sizeof(skip)); resume_head = true; }
                    } else if (resume_head) {
                        memcpy(skip, "head", 5);
                        resume_head = false;
                    } else skip[0] = 0;
                    continue;
                }
                if (!closing && !self_closing && (name_equal(name, "head") || name_equal(name, "style") || name_equal(name, "script"))) {
                    memcpy(skip, name, sizeof(skip));
                } else if (!closing && image_tag(name)) {
                    // 图片独立成块；后端可替换占位，原文字节位置保持稳定。
                    // Isolate images so the backend can replace placeholders without changing text offsets.
                    err = finish_block(&w);
                    if (err != ESP_OK) goto fail;
                    bool heading = w.heading;
                    w.heading = false;
                    const char* label = "[图片]";
                    for (size_t i = 0; label[i];) {
                        uint32_t cp;
                        size_t n = utf8(label + i, strlen(label + i), &cp);
                        err = emit(&w, cp);
                        if (err != ESP_OK) goto fail;
                        i += n;
                    }
                    err = finish_block(&w);
                    if (err != ESP_OK) goto fail;
                    w.text.blocks[w.text.count - 1].image_src = image_source(html + at, end - at);
                    w.heading = heading;
                } else if (block_tag(name)) {
                    err = finish_block(&w);
                    if (err != ESP_OK) goto fail;
                    if (name[0] == 'h' && name[1] >= '1' && name[1] <= '3' && !name[2]) w.heading = !closing && !self_closing;
                }
                continue;
            }
        }
        if (skip[0]) { pos++; continue; }
        uint32_t cp;
        size_t n = html[pos] == '&' ? entity(html + pos, len - pos, &cp) : 0;
        if (!n) n = utf8(html + pos, len - pos, &cp);
        if (!n) { err = ESP_ERR_INVALID_RESPONSE; goto fail; }
        err = emit(&w, cp);
        if (err != ESP_OK) goto fail;
        pos += n;
    }
    err = finish_block(&w);
    if (err == ESP_OK) err = reserve_text(&w, 0);
    if (err != ESP_OK) goto fail;
    w.text.utf8[w.text.len] = 0;
    *out = w.text;
    return ESP_OK;
fail:
    html_text_free(&w.text);
    return err;
}
