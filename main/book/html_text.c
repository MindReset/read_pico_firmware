/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：单遍提取章节文字，折叠空白并保留非空块和标题标记。
 * English: Extract chapter text in one pass, collapsing whitespace and preserving nonempty blocks and headings.
 *
 * 冻结：不执行脚本、不加载资源；输出有界，失败释放全部临时分配。
 * Frozen: Never execute scripts or load resources; bound output and release temporary allocations on failure.
 */
#include "html_text.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "esp_heap_caps.h"

#define PSRAM_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define HTML_IMAGE_MAX 256u
#define HTML_IMAGE_PATH_MAX 511u
#define CSS_RULE_MAX 64u
#define CSS_ANCESTOR_MAX 64u

enum {
    CSS_ALIGN = 1u << 0,
    CSS_INDENT = 1u << 1,
    CSS_BEFORE = 1u << 2,
    CSS_AFTER = 1u << 3,
    CSS_FAMILY = 1u << 4,
};

typedef struct {
    uint8_t mask, align, indent, before, after, family;
} css_style_t;

typedef struct {
    const char *selector;
    size_t length;
    unsigned specificity;
    css_style_t style;
} css_rule_t;

typedef struct {
    char tag[16];
    const char *attrs, *end;
} css_node_t;

typedef struct {
    html_text_t text;
    size_t text_cap, block_cap, start;
    bool active, heading, block_heading, space;
    bool block_linked, block_auxiliary;
    uint8_t heading_level, block_heading_level;
    size_t depth, link_depth, auxiliary_depth;
    css_style_t current_style, block_style;
    // 规则表是解析器里最大的一块（64 × 24B）。留在栈上的话，任何从解析回调里往下走的
    // 长操作（解压、建表）都顶着它，主任务栈会不够用；放 PSRAM。
    // The rule table is the parser's largest object (64 × 24B). Keeping it on the stack means
    // any long call made from a parse callback -- inflate, table setup -- sits on top of it and
    // overruns the main stack, so it lives in PSRAM.
    css_rule_t* rules;
    size_t rule_count;
    css_node_t *ancestors;
    bool scoped_rules;
    const html_font_map_t* fonts;
    // run 表：块内字体一变就切一段，绘制时按段换字体，行宽算法不受影响。
    // run 表存在 text 里，成功时随输出一起交出去，失败时随 free 一起回收。
    // Run table: a new span starts wherever the face changes, so drawing can switch faces
    // without touching the line-width math. It lives in `text` so it is handed over on
    // success and reclaimed by the same free on failure.
    size_t run_cap, run_start, block_run_first;
    uint8_t family, run_slot;
    // 内联标签也会换字体，按开标签深度逐层记下当前槽，闭标签回退上一层。
    // Inline tags switch faces too: record the slot per open depth and fall back on close.
    uint8_t family_at[CSS_ANCESTOR_MAX];
    bool family_rules;
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

static const char* bounded_case_find(const char* at, const char* end, const char* needle) {
    size_t n = strlen(needle);
    if (!n || (size_t)(end - at) < n) return NULL;
    for (; at + n <= end; ++at) if (!strncasecmp(at, needle, n)) return at;
    return NULL;
}

static void css_apply(css_style_t* dst, const css_style_t* src) {
    if (src->mask & CSS_ALIGN) dst->align = src->align;
    if (src->mask & CSS_INDENT) dst->indent = src->indent;
    if (src->mask & CSS_BEFORE) dst->before = src->before;
    if (src->mask & CSS_AFTER) dst->after = src->after;
    if (src->mask & CSS_FAMILY) dst->family = src->family;
    dst->mask |= src->mask;
}

// font-family 是候选列表，取第一个解析得出来的名字；都不认得就回落到系统字体。
// 引号、空格、逗号都要吃掉，否则 "zdy1", serif 这种写法会整串匹配不上。
// A font-family is a candidate list: take the first name the map knows, otherwise fall back
// to the system face. Quotes, spaces and commas are stripped, or `"zdy1", serif` would
// never match a name.
static uint8_t font_family_slot(const writer_t* w, const char* value, size_t len) {
    if (w->fonts == NULL || w->fonts->resolve == NULL) return 0;
    for (size_t i = 0; i < len;) {
        while (i < len && (ascii_space((unsigned char)value[i]) || value[i] == ',')) ++i;
        size_t start = i;
        while (i < len && value[i] != ',') ++i;
        size_t stop = i;
        while (stop > start && ascii_space((unsigned char)value[stop - 1])) --stop;
        if (stop > start && (value[start] == '\'' || value[start] == '"')) {
            char quote = value[start++];
            if (stop > start && value[stop - 1] == quote) --stop;
        }
        if (stop > start) {
            uint8_t slot = w->fonts->resolve(w->fonts->ctx, value + start, stop - start);
            if (slot != 0) return slot;
        }
    }
    return 0;
}

static uint8_t css_length_percent(const char* value, size_t len) {
    while (len && ascii_space((unsigned char)*value)) { ++value; --len; }
    char number[20];
    size_t n = 0;
    while (n < len && n + 1 < sizeof(number) &&
           ((value[n] >= '0' && value[n] <= '9') || value[n] == '.')) {
        number[n] = value[n]; ++n;
    }
    number[n] = 0;
    if (!n) return 0;
    double amount = strtod(number, NULL), percent = amount * 100.0;
    const char* unit = value + n;
    size_t units = len - n;
    while (units && ascii_space((unsigned char)*unit)) { ++unit; --units; }
    if (units >= 2 && !strncasecmp(unit, "px", 2)) percent = amount * 100.0 / 16.0;
    else if (units >= 1 && *unit == '%') percent = amount;
    if (percent < 0) percent = 0;
    if (percent > 250) percent = 250;
    return (uint8_t)(percent + 0.5);
}

static void css_declarations(writer_t* w, const char* at, const char* end, css_style_t* style) {
    while (at < end) {
        while (at < end && (ascii_space((unsigned char)*at) || *at == ';')) ++at;
        const char* key = at;
        while (at < end && *at != ':' && *at != ';') ++at;
        if (at == end || *at != ':') { while (at < end && *at++ != ';') {} continue; }
        const char* key_end = at++;
        while (key_end > key && ascii_space((unsigned char)key_end[-1])) --key_end;
        while (at < end && ascii_space((unsigned char)*at)) ++at;
        const char* value = at;
        while (at < end && *at != ';') ++at;
        const char* value_end = at;
        while (value_end > value && ascii_space((unsigned char)value_end[-1])) --value_end;
        size_t kn = (size_t)(key_end - key), vn = (size_t)(value_end - value);
        if (kn == 10 && !strncasecmp(key, "text-align", 10)) {
            style->align = vn == 6 && !strncasecmp(value, "center", 6) ? 1 :
                           vn == 5 && !strncasecmp(value, "right", 5) ? 2 : 0;
            style->mask |= CSS_ALIGN;
        } else if (kn == 11 && !strncasecmp(key, "text-indent", 11)) {
            style->indent = css_length_percent(value, vn); style->mask |= CSS_INDENT;
        } else if (kn == 10 && !strncasecmp(key, "margin-top", 10)) {
            style->before = css_length_percent(value, vn); style->mask |= CSS_BEFORE;
        } else if (kn == 13 && !strncasecmp(key, "margin-bottom", 13)) {
            style->after = css_length_percent(value, vn); style->mask |= CSS_AFTER;
        } else if (kn == 11 && !strncasecmp(key, "font-family", 11)) {
            style->family = font_family_slot(w, value, vn);
            style->mask |= CSS_FAMILY;
            // 只有真的解析出字体才开 run 表：认得名字才值得逐段记，否则白花内存。
            // Runs start only once a name really resolves; an unmatched family is not worth
            // recording span by span.
            if (style->family != 0) w->family_rules = true;
        }
    }
}

static void css_add_rule(writer_t* w, const char* selector, size_t len, const css_style_t* style) {
    while (len && ascii_space((unsigned char)*selector)) { ++selector; --len; }
    while (len && ascii_space((unsigned char)selector[len - 1])) --len;
    if (!len || len > 95 || w->rule_count == CSS_RULE_MAX || !style->mask) return;
    unsigned specificity = 0, parts = 1;
    bool scoped = false, token = true;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)selector[i];
        if (ascii_space(c) || c == '>') { scoped = true; token = true; continue; }
        // 不支持的选择器整体忽略，绝不把局部规则降成全局规则。
        // Ignore unsupported selectors as a whole, never broaden a scoped rule into a global rule.
        if (!(name_char(c) && c != ':') && c != '.' && c != '#' && c != '*') return;
        if (token) { if (++parts > 9) return; token = false; if (c != '.' && c != '#' && c != '*') ++specificity; }
        if (c == '.') specificity += 16;
        if (c == '#') specificity += 256;
    }
    css_rule_t* rule = &w->rules[w->rule_count++];
    rule->selector = selector;
    rule->length = len;
    rule->specificity = specificity;
    w->scoped_rules |= scoped;
    rule->style = *style;
}

static void css_parse_rules(writer_t* w, const char* at, const char* end) {
    while (at < end) {
        while (at < end && ascii_space((unsigned char)*at)) ++at;
        if (end - at >= 2 && at[0] == '/' && at[1] == '*') {
            const char* close = bounded_case_find(at + 2, end, "*/");
            at = close ? close + 2 : end;
            continue;
        }
        if (at < end && *at == '@') {
            const char* semi = memchr(at, ';', (size_t)(end - at));
            const char* brace = memchr(at, '{', (size_t)(end - at));
            if (semi && (!brace || semi < brace)) { at = semi + 1; continue; }
        }
        const char* open = at;
        while (open < end && *open != '{') ++open;
        if (open == end) break;
        const char* shut = memchr(open + 1, '}', (size_t)(end - open - 1));
        if (!shut) break;
        css_style_t style = {0};
        // at-rule 里的 font-family 是在给字体本身命名（@font-face），不是正文选字体；
        // 采进来只会让每个块都白记一段 run，而且选择器本来就会被下面的规则过滤掉。
        // A font-family inside an at-rule (@font-face) names the face itself rather than
        // selecting one for body text; taking it would add a useless run to every block, and
        // the selector is rejected by the rule filter anyway.
        if (*at != '@') css_declarations(w, open + 1, shut, &style);
        const char* selector = at;
        while (selector < open) {
            const char* comma = memchr(selector, ',', (size_t)(open - selector));
            const char* selector_end = comma ? comma : open;
            css_add_rule(w, selector, (size_t)(selector_end - selector), &style);
            selector = comma ? comma + 1 : open;
        }
        at = shut + 1;
    }
}

static void css_parse_styles(writer_t* w, const char* html, size_t len) {
    const char* at = html;
    const char* end = html + len;
    while ((at = bounded_case_find(at, end, "<style"))) {
        const char* body = memchr(at, '>', (size_t)(end - at));
        if (!body) break;
        ++body;
        const char* close = bounded_case_find(body, end, "</style");
        if (!close) break;
        css_parse_rules(w, body, close);
        at = close + 7;
    }
}

static bool attr_value(const char* at, const char* end, const char* wanted,
                       const char** value_out, size_t* length_out) {
    size_t wanted_len = strlen(wanted);
    while (at < end) {
        while (at < end && (ascii_space((unsigned char)*at) || *at == '/')) ++at;
        const char* name = at;
        while (at < end && name_char((unsigned char)*at)) ++at;
        size_t n = (size_t)(at - name);
        while (at < end && ascii_space((unsigned char)*at)) ++at;
        if (at >= end || *at++ != '=') continue;
        while (at < end && ascii_space((unsigned char)*at)) ++at;
        if (at >= end) break;
        char quote = (*at == '\'' || *at == '"') ? *at++ : 0;
        const char* value = at;
        while (at < end && (quote ? *at != quote : !ascii_space((unsigned char)*at) && *at != '>')) ++at;
        size_t length = (size_t)(at - value);
        if (quote && at < end) ++at;
        if (n == wanted_len && !strncasecmp(name, wanted, n)) {
            *value_out = value; *length_out = length; return true;
        }
    }
    return false;
}

static bool class_has(const char* classes, size_t len, const char* wanted) {
    size_t n = strlen(wanted);
    for (size_t i = 0; i < len;) {
        while (i < len && ascii_space((unsigned char)classes[i])) ++i;
        size_t start = i;
        while (i < len && !ascii_space((unsigned char)classes[i])) ++i;
        if (i - start == n && !strncasecmp(classes + start, wanted, n)) return true;
    }
    return false;
}

// 语义目录/版权容器只标记来源，仍保留原文供阅读；不把目录链接当章节。
// Tag navigation/front-matter provenance without hiding its text or treating links as chapters.
static bool auxiliary_tag(const char* name, const char* at, const char* end) {
    if (name_equal(name, "nav")) return true;
    static const char* const attrs[] = {"epub:type", "type", "role", "id", "class"};
    static const char* const tokens[] = {"toc", "contents", "doc-toc", "copyright", "doc-copyright",
        "titlepage", "cover", "frontmatter", "colophon", "imprint", "dedication", "abstract"};
    for (size_t a = 0; a < sizeof(attrs) / sizeof(attrs[0]); ++a) {
        const char* value = NULL; size_t length = 0;
        if (!attr_value(at, end, attrs[a], &value, &length)) continue;
        for (size_t t = 0; t < sizeof(tokens) / sizeof(tokens[0]); ++t)
            if (class_has(value, length, tokens[t])) return true;
    }
    return false;
}

static bool void_tag(const char* name) {
    static const char* const tags[] = {"br", "hr", "img", "image", "svg:image", "meta", "link",
        "input", "area", "base", "col", "embed", "param", "source", "track", "wbr"};
    for (size_t i = 0; i < sizeof(tags) / sizeof(tags[0]); ++i)
        if (name_equal(name, tags[i])) return true;
    return false;
}

// 有界祖先链匹配后代和直接子代，选择器与属性借用本次解析的输入。
// Match descendants and direct children against a bounded ancestor chain; borrow this parse's input.
static bool css_compound(const char *selector, size_t len, const char *tag,
                         const char *attrs, const char *end) {
    size_t i = 0;
    while (i < len && selector[i] != '.' && selector[i] != '#') ++i;
    if (i && !(i == 1 && selector[0] == '*') &&
        (strlen(tag) != i || strncasecmp(selector, tag, i))) return false;
    while (i < len) {
        char kind = selector[i++]; size_t start = i;
        while (i < len && selector[i] != '.' && selector[i] != '#') ++i;
        if (i == start || i - start >= 96) return false;
        char token[96]; memcpy(token, selector + start, i - start); token[i - start] = 0;
        const char *value = NULL; size_t length = 0;
        if (!attr_value(attrs, end, kind == '#' ? "id" : "class", &value, &length)) return false;
        if (kind == '#' ? (length != i - start || memcmp(value, token, length)) :
                         !class_has(value, length, token)) return false;
    }
    return true;
}
static bool css_scope_match(const writer_t *w, const char *s, size_t len,
                            const char *tag, const char *attrs, const char *end,
                            size_t parent, unsigned budget, unsigned *work) {
    if (!budget || !len || !*work) return false;
    --*work;
    size_t begin = len;
    while (begin && !ascii_space((unsigned char)s[begin - 1]) && s[begin - 1] != '>') --begin;
    if (!css_compound(s + begin, len - begin, tag, attrs, end)) return false;
    size_t remaining = begin;
    while (remaining && ascii_space((unsigned char)s[remaining - 1])) --remaining;
    bool child = remaining && s[remaining - 1] == '>';
    if (child) --remaining;
    while (remaining && ascii_space((unsigned char)s[remaining - 1])) --remaining;
    if (!remaining) return !child;
    if (!w->ancestors || parent > CSS_ANCESTOR_MAX) return false;
    while (parent) {
        const css_node_t *node = &w->ancestors[--parent];
        if (css_scope_match(w, s, remaining, node->tag, node->attrs, node->end, parent, budget - 1, work)) return true;
        if (child) break;
    }
    return false;
}
static css_style_t style_for(writer_t* w, const char* tag, const char* attrs, const char* attrs_end) {
    css_style_t out = {0};
    unsigned priorities[5] = {0};
    for (size_t i = 0; i < w->rule_count; ++i) {
        const css_rule_t *rule = &w->rules[i];
        bool body = rule->length == 4 && !strncasecmp(rule->selector, "body", 4);
        unsigned work = 1024;
        bool matches = body || css_scope_match(w, rule->selector, rule->length,
                                               tag, attrs, attrs_end, w->depth ? w->depth - 1 : 0, 8, &work);
        if (!matches) continue;
        css_style_t chosen = rule->style;
        unsigned specificity = body ? 0 : rule->specificity;
        for (unsigned bit = 0; bit < 5; ++bit) {
            if (!(chosen.mask & (1u << bit))) continue;
            if (specificity < priorities[bit]) chosen.mask &= ~(1u << bit);
            else priorities[bit] = specificity;
        }
        css_apply(&out, &chosen);
    }
    const char *inline_css = NULL; size_t inline_len = 0;
    if (attr_value(attrs, attrs_end, "style", &inline_css, &inline_len)) {
        css_style_t inline_style = {0};
        css_declarations(w, inline_css, inline_css + inline_len, &inline_style);
        css_apply(&out, &inline_style);
    }
    return out;
}

void html_text_free(html_text_t* text) {
    if (!text) return;
    free(text->utf8);
    free(text->blocks);
    for (size_t i = 0; i < text->image_count; ++i) free(text->images[i]);
    free(text->images);
    free(text->runs);
    *text = (html_text_t){0};
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

// 收一段：把当前 run 从 run_start 补到已写入的块尾。只有书里真的出现过 font-family
// 才记，所以没有内嵌字体的书不花这份内存。
// Close one span from run_start to the bytes already written. Runs are only recorded once a
// font-family actually appeared, so books without embedded faces pay nothing.
static esp_err_t run_open(writer_t* w) {
    if (!w->family_rules) return ESP_OK;
    if (w->text.run_count == w->run_cap) {
        // 到顶就不再切段：run 表仍完整覆盖整块，只是整块用首段的字体。
        // At the cap we stop splitting: the table still covers the block, which then uses the
        // first span's face.
        if (w->run_cap >= HTML_RUN_MAX) return ESP_OK;
        size_t cap = w->run_cap ? w->run_cap * 2 : 64;
        if (cap > HTML_RUN_MAX) cap = HTML_RUN_MAX;
        html_run_t* runs = heap_caps_realloc(w->text.runs, cap * sizeof(*runs), PSRAM_CAPS);
        if (!runs) return ESP_ERR_NO_MEM;
        w->text.runs = runs;
        w->run_cap = cap;
    }
    w->text.runs[w->text.run_count++] = (html_run_t){
        .offset = w->run_start,
        .len = w->text.len - w->start - w->run_start,
        .slot = w->run_slot,
    };
    return ESP_OK;
}

static esp_err_t finish_block(writer_t* w, bool line_break) {
    w->space = false;
    if (!w->active) {
        // 段尾 br 没有后续文字时，结束标签仍需收回整段的段后距。
        // A trailing br with no following text still keeps the paragraph's closing margin.
        if (!line_break && w->text.count) {
            blk_t* last = &w->text.blocks[w->text.count - 1];
            if (last->line_break_after) {
                last->line_break_after = false;
                last->margin_after_percent = w->current_style.after;
            }
        }
        return ESP_OK;
    }
    if (w->text.count == HTML_TEXT_MAX_BLOCKS) return ESP_ERR_INVALID_SIZE;
    esp_err_t run_err = run_open(w);
    if (run_err != ESP_OK) return run_err;
    if (w->text.count == w->block_cap) {
        size_t cap = w->block_cap ? w->block_cap * 2 : 32;
        if (cap > HTML_TEXT_MAX_BLOCKS) cap = HTML_TEXT_MAX_BLOCKS;
        blk_t* blocks = heap_caps_realloc(w->text.blocks, cap * sizeof(*blocks), PSRAM_CAPS);
        if (!blocks) return ESP_ERR_NO_MEM;
        w->text.blocks = blocks;
        w->block_cap = cap;
    }
    w->text.blocks[w->text.count++] = (blk_t){
        .offset = w->start, .len = w->text.len - w->start, .heading = w->block_heading, .image = -1,
        .heading_level = w->block_heading_level, .linked = w->block_linked,
        .auxiliary = w->block_auxiliary,
        .align = w->block_style.align,
        .indent_percent = w->block_style.indent,
        .margin_before_percent = w->block_style.before,
        .margin_after_percent = line_break ? 0 : w->block_style.after,
        .line_break_after = line_break,
        .run_first = w->block_run_first,
        .run_count = w->text.run_count - w->block_run_first,
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
        w->block_heading_level = w->heading_level;
        w->block_linked = true;
        w->block_auxiliary = false;
        w->block_style = w->current_style;
        w->block_run_first = w->text.run_count;
        w->run_start = 0;
        w->run_slot = w->family;
    } else if (w->space) w->text.utf8[w->text.len++] = ' ';
    w->space = false;
    w->block_linked &= w->link_depth != 0;
    w->block_auxiliary |= w->auxiliary_depth != 0;
    if (w->family != w->run_slot) {
        esp_err_t run_err = run_open(w);
        if (run_err != ESP_OK) return run_err;
        w->run_start = w->text.len - w->start;
        w->run_slot = w->family;
    }
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

// 识别常见惰性图片属性及 SVG 引用，并解码路径中的 XML 实体。
// Recognize common lazy image attributes and SVG references, decoding XML entities in paths.
static bool image_attr(const char* at, const char* end, bool object,
                       char path[HTML_IMAGE_PATH_MAX + 1]) {
    static const char* const image_names[] = {
        "data-src", "data-original", "data-lazy-src", "src", "xlink:href", "href", "srcset", "data-srcset"
    };
    const char* value = NULL; size_t length = 0;
    if (object) {
        if (!attr_value(at, end, "data", &value, &length)) return false;
    } else {
        for (size_t i = 0; i < sizeof(image_names) / sizeof(image_names[0]); ++i) {
            if (attr_value(at, end, image_names[i], &value, &length) && length &&
                !(length >= 5 && !strncasecmp(value, "data:", 5))) {
                if (strstr(image_names[i], "srcset")) {
                    while (length && ascii_space((unsigned char)*value)) { ++value; --length; }
                    size_t n = 0;
                    while (n < length && !ascii_space((unsigned char)value[n]) && value[n] != ',') ++n;
                    length = n;
                }
                break;
            }
        }
    }
    if (!value || !length || length > HTML_IMAGE_PATH_MAX) return false;
    size_t used = 0;
    for (size_t i = 0; i < length;) {
        uint32_t cp = 0;
        size_t consumed = value[i] == '&' ? entity(value + i, length - i, &cp) : 0;
        if (consumed) {
            char bytes[4]; size_t count = 0;
            if (cp < 0x80) bytes[count++] = (char)cp;
            else if (cp < 0x800) {
                bytes[count++] = (char)(0xc0 | (cp >> 6));
                bytes[count++] = (char)(0x80 | (cp & 63));
            } else if (cp < 0x10000) {
                bytes[count++] = (char)(0xe0 | (cp >> 12));
                bytes[count++] = (char)(0x80 | ((cp >> 6) & 63));
                bytes[count++] = (char)(0x80 | (cp & 63));
            } else {
                bytes[count++] = (char)(0xf0 | (cp >> 18));
                bytes[count++] = (char)(0x80 | ((cp >> 12) & 63));
                bytes[count++] = (char)(0x80 | ((cp >> 6) & 63));
                bytes[count++] = (char)(0x80 | (cp & 63));
            }
            if (used + count > HTML_IMAGE_PATH_MAX) return false;
            memcpy(path + used, bytes, count); used += count; i += consumed;
        } else {
            if (used == HTML_IMAGE_PATH_MAX) return false;
            path[used++] = value[i++];
        }
    }
    path[used] = 0;
    return used != 0;
}

static esp_err_t add_image(writer_t* w, const char* path) {
    if (w->text.image_count == HTML_IMAGE_MAX) return ESP_OK;
    esp_err_t err = finish_block(w, false);
    if (err != ESP_OK) return err;
    char** images = heap_caps_realloc(w->text.images, (w->text.image_count + 1) * sizeof(char*), PSRAM_CAPS);
    if (!images) return ESP_ERR_NO_MEM;
    w->text.images = images;
    size_t n = strlen(path) + 1;
    char* copy = heap_caps_malloc(n, PSRAM_CAPS);
    if (!copy) return ESP_ERR_NO_MEM;
    memcpy(copy, path, n);
    w->text.images[w->text.image_count] = copy;
    int index = (int)w->text.image_count++;
    err = emit(w, 0xfffc);
    if (err != ESP_OK) return err;
    err = finish_block(w, false);
    if (err == ESP_OK) w->text.blocks[w->text.count - 1].image = index;
    return err;
}

esp_err_t html_to_blocks_with_css_target(const char* html, size_t len,
                                          const char* css, size_t css_len,
                                          const html_font_map_t* fonts,
                                          const char* anchor, size_t source_offset,
                                          size_t* anchor_offset, html_text_t* out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = (html_text_t){0};
    if (anchor_offset) *anchor_offset = 0;
    if ((!html && len) || (!css && css_len)) return ESP_ERR_INVALID_ARG;
    if (len > HTML_TEXT_MAX_BYTES) return ESP_ERR_INVALID_SIZE;
    writer_t w = {0};
    w.fonts = fonts;
    w.rules = heap_caps_malloc(CSS_RULE_MAX * sizeof(*w.rules), PSRAM_CAPS);
    if (w.rules == NULL) return ESP_ERR_NO_MEM;
    if (css_len) css_parse_rules(&w, css, css + css_len);
    if (len) css_parse_styles(&w, html, len);
    esp_err_t err = ESP_OK;
    if (w.scoped_rules) {
        w.ancestors = heap_caps_malloc(CSS_ANCESTOR_MAX * sizeof(*w.ancestors), PSRAM_CAPS);
        if (!w.ancestors) { free(w.rules); return ESP_ERR_NO_MEM; }
    }
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
            size_t tag_offset = pos;
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
                // 在块结束之前保留其来源；关闭容器后不影响后面的正文块。
                // Preserve provenance until the block ends; closing a container must not taint later body text.
                if (closing) {
                    if (w.depth == w.link_depth) w.link_depth = 0;
                    if (w.depth == w.auxiliary_depth) w.auxiliary_depth = 0;
                    if (w.depth) --w.depth;
                    if (w.family_rules && w.depth <= CSS_ANCESTOR_MAX)
                        w.family = w.depth ? w.family_at[w.depth - 1] : 0;
                } else if (!self_closing && !void_tag(name) &&
                           !name_equal(name, "head") && !name_equal(name, "script") && !name_equal(name, "style")) {
                    ++w.depth;
                    // 内联标签也会换字体（<span class="num">），所以每层都算一次；本层没声明
                    // 就继承父层。没有 font-family 规则、标签也不带 style 时整段跳过，
                    // 正文一分钱不花；带 style 的标签只看属性、不跑整套选择器匹配。
                    // Inline tags switch faces too, so every level resolves; a level without a
                    // declaration inherits its parent's. With no font-family rule and no style
                    // attribute the step is skipped and body text pays nothing; a styled tag is
                    // only probed for the attribute instead of running the selector match.
                    bool want_family = w.family_rules;
                    if (!want_family && w.fonts != NULL && w.fonts->resolve != NULL) {
                        const char* probe = NULL;
                        size_t probe_len = 0;
                        want_family = attr_value(html + at, html + end, "style", &probe, &probe_len);
                    }
                    if (want_family && w.depth <= CSS_ANCESTOR_MAX) {
                        uint8_t parent = w.depth >= 2 ? w.family_at[w.depth - 2] : 0;
                        css_style_t level = style_for(&w, name, html + at, html + end);
                        w.family_at[w.depth - 1] =
                            (level.mask & CSS_FAMILY) ? level.family : parent;
                        w.family = w.family_at[w.depth - 1];
                    }
                    if (w.ancestors && w.depth <= CSS_ANCESTOR_MAX) {
                        css_node_t *node = &w.ancestors[w.depth - 1];
                        memcpy(node->tag, name, sizeof(node->tag));
                        node->attrs = html + at; node->end = html + end;
                    }
                    if (!w.auxiliary_depth && auxiliary_tag(name, html + at, html + end))
                        w.auxiliary_depth = w.depth;
                    const char* href = NULL; size_t href_len = 0;
                    if (!w.link_depth && name_equal(name, "a") &&
                        attr_value(html + at, html + end, "href", &href, &href_len) && href_len)
                        w.link_depth = w.depth;
                }
                if (!closing && (name_equal(name, "img") || name_equal(name, "image") ||
                                 name_equal(name, "svg:image") || name_equal(name, "object"))) {
                    char path[HTML_IMAGE_PATH_MAX + 1];
                    if (image_attr(html + at, html + end, name_equal(name, "object"), path)) {
                        err = add_image(&w, path);
                        if (err != ESP_OK) goto fail;
                    }
                    continue;
                }
                if (!closing && !self_closing && (name_equal(name, "head") || name_equal(name, "style") || name_equal(name, "script"))) {
                    memcpy(skip, name, sizeof(skip));
                } else if (block_tag(name)) {
                    bool line_break = name_equal(name, "br");
                    err = finish_block(&w, line_break);
                    if (err != ESP_OK) goto fail;
                    // br 仅换行；整段结束时才加段后距，不重复段前距和首行缩进。
                    // A br continues alignment and defers the trailing margin until paragraph end.
                    if (line_break)
                        w.current_style = (css_style_t){.mask = w.current_style.mask & (CSS_ALIGN | CSS_AFTER),
                                                       .align = w.current_style.align,
                                                       .after = w.current_style.after};
                    else if (!closing && !self_closing) w.current_style = style_for(&w, name, html + at, html + end);
                    else w.current_style = (css_style_t){0};
                    if (name[0] == 'h' && name[1] >= '1' && name[1] <= '3' && !name[2]) w.heading = !closing && !self_closing;
                    if (name[0] == 'h' && name[1] >= '1' && name[1] <= '6' && !name[2])
                        w.heading_level = !closing && !self_closing ? (uint8_t)(name[1] - '0') : 0;
                }
                if (!closing && !skip[0] && anchor_offset) {
                    const char *value = NULL; size_t length = 0;
                    bool matched = tag_offset == source_offset;
                    if (!matched && anchor && *anchor &&
                        (attr_value(html + at, html + end, "id", &value, &length) ||
                         attr_value(html + at, html + end, "xml:id", &value, &length) ||
                         attr_value(html + at, html + end, "name", &value, &length)) &&
                        length == strlen(anchor) && !memcmp(value, anchor, length)) matched = true;
                    if (matched)
                        *anchor_offset = w.text.len + (!w.active && w.text.count ? 1 : 0);
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
    err = finish_block(&w, false);
    if (err == ESP_OK) err = reserve_text(&w, 0);
    if (err != ESP_OK) goto fail;
    w.text.utf8[w.text.len] = 0;
    *out = w.text;
    free(w.ancestors);
    free(w.rules);
    return ESP_OK;
fail:
    free(w.ancestors);
    free(w.rules);
    html_text_free(&w.text);
    return err;
}

esp_err_t html_to_blocks_with_css_anchor(const char* html, size_t len,
                                          const char* css, size_t css_len,
                                          const char* anchor, size_t* anchor_offset,
                                          html_text_t* out) {
    return html_to_blocks_with_css_target(html, len, css, css_len, NULL,
                                           anchor, SIZE_MAX, anchor_offset, out);
}

esp_err_t html_to_blocks_with_css(const char* html, size_t len,
                                   const char* css, size_t css_len, html_text_t* out) {
    return html_to_blocks_with_css_anchor(html, len, css, css_len, NULL, NULL, out);
}

esp_err_t html_to_blocks(const char* html, size_t len, html_text_t* out) {
    return html_to_blocks_with_css(html, len, NULL, 0, out);
}
