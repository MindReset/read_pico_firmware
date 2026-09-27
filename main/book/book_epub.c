/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：有界 EPUB 元数据解析；ZIP 和正文解析由独立后端负责。
 * English: Bounded EPUB metadata parsing; separate backends own ZIP and body conversion.
 *
 * 冻结：只读；不解析外部实体；最多 512 项；累计原始 HTML 字节用于进度。
 * Frozen: Read-only; no external entity resolution; at most 512 items; progress uses cumulative source HTML bytes.
 */
#include "book_epub.h"
#include "zip_reader.h"
#include "esp_heap_caps.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#define EPUB_ITEMS_MAX 512
#define EPUB_XML_DEPTH 64
#define EPUB_PATH_CAP 512
#define EPUB_ID_CAP 128
#define EPUB_TITLE_CAP 160
#define EPUB_ENTRY_MAX (2u * 1024u * 1024u)

typedef struct {
    int zip_index; ///< ZIP 条目 / ZIP entry
    uint32_t offset; ///< spine 累计原始字节 / Cumulative source bytes in spine
    char title[EPUB_TITLE_CAP]; ///< UTF-8 目录标题 / UTF-8 navigation title
    bool titled; ///< 已从目录命名 / Named from navigation
} chapter_t;
struct book_epub {
    zip_reader_t *zip; ///< ZIP 所有权 / ZIP ownership
    chapter_t chapters[EPUB_ITEMS_MAX]; ///< PSRAM 章节表 / PSRAM chapter table
    size_t count; ///< 章节数 / Chapter count
    uint32_t total; ///< 原始 HTML 字节总量 / Total source HTML bytes
};
typedef struct {
    char id[EPUB_ID_CAP]; ///< manifest 标识 / Manifest identifier
    char path[EPUB_PATH_CAP]; ///< 已规范化 ZIP 路径 / Normalized ZIP path
    bool nav, ncx, html; ///< 媒体用途 / Media roles
} item_t;
typedef struct { const char *p; size_t n; } span_t;
typedef enum { XML_OPEN, XML_CLOSE, XML_TEXT } xml_kind_t;
typedef struct {
    xml_kind_t kind; ///< token 类型 / Token kind
    span_t name, attrs, text; ///< 输入借用片段 / Borrowed input spans
    size_t depth; ///< 当前元素深度 / Current element depth
    bool empty, cdata; ///< 自闭合及原文段 / Self-closing and literal text
} token_t;
typedef struct {
    const char *p, *end; ///< 输入范围 / Input bounds
    span_t stack[EPUB_XML_DEPTH]; ///< 嵌套名称 / Nested names
    size_t depth; ///< 当前嵌套 / Current nesting
    size_t roots; ///< 根节点数 / Root element count
    bool failed; ///< 解析错误 / Parse error
} xml_t;

/* ---- XML 边界与实体 / XML bounds and entities ---- */

static void *psram(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
static bool equal(span_t s, const char *text) { return s.n == strlen(text) && !memcmp(s.p, text, s.n); }
static bool local_name(span_t s, const char *name) {
    for (size_t i = 0; i < s.n; ++i) if (s.p[i] == ':') { s.n -= i + 1; s.p += i + 1; break; }
    return equal(s, name);
}
static bool begins(const char *p, const char *end, const char *s) {
    size_t n = strlen(s); return (size_t)(end - p) >= n && !memcmp(p, s, n);
}
static const char *find_end(const char *p, const char *end, const char *marker) {
    size_t n = strlen(marker);
    while ((size_t)(end - p) >= n) { if (!memcmp(p, marker, n)) return p; ++p; }
    return NULL;
}
static bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
static bool name_char(char c) { return !space(c) && c != '/' && c != '>' && c != '=' && c != '<' && c != '\'' && c != '"'; }
static size_t utf8_encode(uint32_t cp, char out[4]) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) { out[0] = 0xc0 | (cp >> 6); out[1] = 0x80 | (cp & 63); return 2; }
    if (cp < 0x10000) { out[0] = 0xe0 | (cp >> 12); out[1] = 0x80 | ((cp >> 6) & 63); out[2] = 0x80 | (cp & 63); return 3; }
    out[0] = 0xf0 | (cp >> 18); out[1] = 0x80 | ((cp >> 12) & 63); out[2] = 0x80 | ((cp >> 6) & 63); out[3] = 0x80 | (cp & 63); return 4;
}
static size_t utf8_read(const char *p, size_t n, uint32_t *cp) {
    unsigned char a = (unsigned char)p[0];
    if (a < 0x80) { *cp = a; return 1; }
    size_t count = a >= 0xc2 && a <= 0xdf ? 2 : a >= 0xe0 && a <= 0xef ? 3 : a >= 0xf0 && a <= 0xf4 ? 4 : 0;
    if (!count || count > n) return 0;
    uint32_t value = a & (0x7f >> count);
    for (size_t i = 1; i < count; ++i) {
        unsigned char b = (unsigned char)p[i]; if ((b & 0xc0) != 0x80) return 0;
        value = (value << 6) | (b & 63);
    }
    if ((count == 2 && value < 0x80) || (count == 3 && value < 0x800) || (count == 4 && value < 0x10000) || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return 0;
    *cp = value; return count;
}
static int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static bool entity(span_t s, uint32_t *cp) {
    if (equal(s, "amp")) *cp = '&';
    else if (equal(s, "lt")) *cp = '<';
    else if (equal(s, "gt")) *cp = '>';
    else if (equal(s, "quot")) *cp = '"';
    else if (equal(s, "apos")) *cp = '\'';
    else if (equal(s, "nbsp")) *cp = 160;
    else if (s.n > 1 && s.p[0] == '#') {
        size_t i = 1; unsigned base = 10;
        if (s.p[i] == 'x' || s.p[i] == 'X') { base = 16; ++i; }
        if (i == s.n) return false;
        uint32_t value = 0;
        for (; i < s.n; ++i) {
            int digit = hex(s.p[i]); if (digit < 0 || (unsigned)digit >= base || value > (0x10ffffu - (unsigned)digit) / base) return false;
            value = value * base + (unsigned)digit;
        }
        if (!value || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
        *cp = value;
    } else return false;
    return true;
}
// 属性必须完整容纳；目录标题可在标量边界截断。/ Attributes must fit fully; navigation labels may truncate at scalar boundaries.
static bool decode(span_t s, char *out, size_t cap, bool label, bool literal) {
    size_t used = strlen(out); bool full = false;
    for (size_t i = 0; i < s.n;) {
        uint32_t cp; size_t n;
        if (!literal && s.p[i] == '&') {
            size_t end = i + 1; while (end < s.n && s.p[end] != ';' && end - i < 20) ++end;
            if (end == s.n || s.p[end] != ';' || !entity((span_t){s.p + i + 1, end - i - 1}, &cp)) return false;
            n = end - i + 1;
        } else { n = utf8_read(s.p + i, s.n - i, &cp); if (!n) return false; }
        i += n;
        if (!cp || (cp < 32 && cp != '\n' && cp != '\r' && cp != '\t')) return false;
        if (label && (cp == 160 || cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r')) {
            if (!used || out[used - 1] == ' ') continue;
            cp = ' ';
        }
        char bytes[4]; size_t bytes_n = utf8_encode(cp, bytes);
        if (full || used + bytes_n >= cap) { if (!label) return false; full = true; continue; }
        memcpy(out + used, bytes, bytes_n); used += bytes_n; out[used] = 0;
    }
    return true;
}
static bool attr_next(span_t attrs, size_t *offset, span_t *name, span_t *value) {
    size_t i = *offset; while (i < attrs.n && space(attrs.p[i])) ++i;
    if (i == attrs.n) { *offset = i; return false; }
    size_t start = i; while (i < attrs.n && name_char(attrs.p[i])) ++i;
    if (i == start) { *offset = SIZE_MAX; return false; }
    *name = (span_t){attrs.p + start, i - start};
    while (i < attrs.n && space(attrs.p[i])) ++i;
    if (i == attrs.n || attrs.p[i++] != '=') { *offset = SIZE_MAX; return false; }
    while (i < attrs.n && space(attrs.p[i])) ++i;
    if (i == attrs.n || (attrs.p[i] != '\'' && attrs.p[i] != '"')) { *offset = SIZE_MAX; return false; }
    char quote = attrs.p[i++]; start = i;
    while (i < attrs.n && attrs.p[i] != quote) { if (attrs.p[i] == '<') { *offset = SIZE_MAX; return false; } ++i; }
    if (i == attrs.n) { *offset = SIZE_MAX; return false; }
    *value = (span_t){attrs.p + start, i - start}; *offset = i + 1; return true;
}
static bool attribute(token_t t, const char *wanted, char *out, size_t cap) {
    size_t offset = 0; span_t name, value; bool found = false; out[0] = 0;
    while (attr_next(t.attrs, &offset, &name, &value)) if (equal(name, wanted)) {
        if (found || !decode(value, out, cap, false, false)) return false;
        found = true;
    }
    return offset != SIZE_MAX;
}
static bool xml_next(xml_t *xml, token_t *t) {
    memset(t, 0, sizeof(*t));
    while (xml->p < xml->end && !xml->failed) {
        const char *p = xml->p;
        if (*p != '<') {
            const char *start = p; while (p < xml->end && *p != '<') ++p;
            if (!xml->depth) {
                const char *q = start; while (q < p && space(*q)) ++q;
                if (q != p) break;
            }
            t->kind = XML_TEXT; t->text = (span_t){start, (size_t)(p - start)}; t->depth = xml->depth; xml->p = p; return true;
        }
        if (begins(p, xml->end, "<!--") || begins(p, xml->end, "<?")) {
            bool comment = p[1] == '!'; const char *marker = comment ? "-->" : "?>";
            const char *end = find_end(p + (comment ? 4 : 2), xml->end, marker);
            if (!end) break;
            xml->p = end + strlen(marker); continue;
        }
        if (begins(p, xml->end, "<![CDATA[")) {
            if (!xml->depth) break;
            const char *end = find_end(p + 9, xml->end, "]]>"); if (!end) break;
            t->kind = XML_TEXT; t->cdata = true; t->text = (span_t){p + 9, (size_t)(end - p - 9)}; t->depth = xml->depth; xml->p = end + 3; return true;
        }
        if (begins(p, xml->end, "<!DOCTYPE")) {
            // 跳过声明但从不读取外部 DTD；内部实体也不展开。/ Skip declarations without loading external DTDs or expanding internal entities.
            int brackets = 0; char quote = 0; p += 9;
            while (p < xml->end) {
                char c = *p++;
                if (quote) { if (c == quote) quote = 0; continue; }
                if (c == '\'' || c == '"') quote = c;
                else if (c == '[') ++brackets;
                else if (c == ']') { if (!brackets) break; --brackets; }
                else if (c == '>' && !brackets) { xml->p = p; break; }
            }
            if (xml->p != p || quote || brackets) break;
            continue;
        }
        ++p; bool closing = p < xml->end && *p == '/'; if (closing) ++p;
        const char *name = p; while (p < xml->end && name_char(*p)) ++p;
        if (p == name) break;
        t->name = (span_t){name, (size_t)(p - name)};
        const char *attrs = p; char quote = 0;
        while (p < xml->end) {
            if (quote) { if (*p == quote) quote = 0; }
            else if (*p == '\'' || *p == '"') quote = *p;
            else if (*p == '>') break;
            ++p;
        }
        if (p == xml->end || quote) break;
        const char *end = p; while (end > attrs && space(end[-1])) --end;
        if (end > attrs && end[-1] == '/') { t->empty = true; --end; }
        t->attrs = (span_t){attrs, (size_t)(end - attrs)};
        size_t at = 0; span_t an, av; while (attr_next(t->attrs, &at, &an, &av)) {}
        if (at == SIZE_MAX) break;
        if (closing) {
            if (t->empty || !xml->depth || t->attrs.n) {
                bool only_space = !t->empty && xml->depth;
                for (size_t i = 0; i < t->attrs.n; ++i) if (!space(t->attrs.p[i])) only_space = false;
                if (!only_space) break;
            }
            span_t top = xml->stack[xml->depth - 1];
            if (top.n != t->name.n || memcmp(top.p, t->name.p, top.n)) break;
            t->kind = XML_CLOSE; t->depth = xml->depth; --xml->depth;
        } else {
            if (xml->depth == EPUB_XML_DEPTH) break;
            if (!xml->depth && ++xml->roots != 1) break;
            t->kind = XML_OPEN; t->depth = xml->depth + 1;
            if (!t->empty) xml->stack[xml->depth++] = t->name;
        }
        xml->p = p + 1; return true;
    }
    if (xml->p != xml->end || xml->depth || xml->roots != 1) xml->failed = true;
    return false;
}
static void xml_reader(xml_t *xml, const char *text, size_t n) {
    memset(xml, 0, sizeof(*xml)); xml->p = text; xml->end = text + n;
    if (memchr(text, 0, n)) xml->failed = true;
    if (n >= 3 && !memcmp(text, "\xef\xbb\xbf", 3)) xml->p += 3;
}

/* ---- ZIP 路径及内容 / ZIP paths and content ---- */

static bool resolve_path(const char *base, const char *href, char out[EPUB_PATH_CAP]) {
    char decoded[EPUB_PATH_CAP]; size_t used = 0;
    for (size_t i = 0; href[i] && href[i] != '#'; ++i) {
        unsigned char c = (unsigned char)href[i];
        if (c == '%') {
            if (!href[i + 1] || !href[i + 2]) return false;
            int a = hex(href[i + 1]), b = hex(href[i + 2]); if (a < 0 || b < 0) return false;
            c = (unsigned char)((a << 4) | b); i += 2;
        }
        if (c < 32 || c == 127 || c == '\\' || c == ':' || c == '?' || used + 1 >= sizeof(decoded)) return false;
        decoded[used++] = (char)c;
    }
    decoded[used] = 0;
    if (!used) {
        if (href[0] != '#' || !base[0] || strlen(base) >= EPUB_PATH_CAP) return false;
        strcpy(out, base); return true;
    }
    if (decoded[0] == '/') return false;
    size_t length = 0;
    const char *slash = strrchr(base, '/'); if (slash) length = (size_t)(slash - base);
    if (length >= EPUB_PATH_CAP) return false;
    memcpy(out, base, length);
    for (size_t i = 0; decoded[i];) {
        size_t start = i; while (decoded[i] && decoded[i] != '/') ++i;
        size_t n = i - start; if (decoded[i]) ++i;
        if (!n || (n == 1 && decoded[start] == '.')) continue;
        if (n == 2 && decoded[start] == '.' && decoded[start + 1] == '.') {
            if (!length) return false;
            while (length && out[length - 1] != '/') --length;
            if (length) --length;
            continue;
        }
        if (length + (length != 0) + n >= EPUB_PATH_CAP) return false;
        if (length) out[length++] = '/';
        memcpy(out + length, decoded + start, n); length += n;
    }
    out[length] = 0; return length != 0;
}
static esp_err_t load_entry(book_epub_t *book, int index, char **out, size_t *len) {
    *out = NULL; *len = 0;
    if (index < 0) return ESP_ERR_NOT_FOUND;
    size_t n = zip_entry_size(book->zip, index); if (n > EPUB_ENTRY_MAX) return ESP_ERR_INVALID_SIZE;
    char *text = psram(n + 1); if (!text) return ESP_ERR_NO_MEM;
    esp_err_t err = zip_extract(book->zip, index, text, n);
    if (err != ESP_OK) { free(text); return err; }
    text[n] = 0; *out = text; *len = n; return ESP_OK;
}
static bool word(const char *list, const char *needle) {
    size_t n = strlen(needle);
    while (*list) {
        while (space(*list)) ++list;
        const char *start = list; while (*list && !space(*list)) ++list;
        if ((size_t)(list - start) == n && !memcmp(start, needle, n)) return true;
    }
    return false;
}
static esp_err_t container_path(book_epub_t *book, char out[EPUB_PATH_CAP]) {
    char *text; size_t len; esp_err_t err = load_entry(book, zip_find(book->zip, "META-INF/container.xml"), &text, &len);
    if (err != ESP_OK) return err;
    xml_t xml; xml_reader(&xml, text, len); token_t t; bool found = false, root = false;
    while (xml_next(&xml, &t)) if (t.kind == XML_OPEN) {
        if (t.depth == 1 && local_name(t.name, "container")) root = true;
        if (root && local_name(t.name, "rootfile") && !found) {
            char href[EPUB_PATH_CAP], media[80];
            if (!attribute(t, "full-path", href, sizeof(href)) || !attribute(t, "media-type", media, sizeof(media))) { xml.failed = true; break; }
            if ((!media[0] || !strcmp(media, "application/oebps-package+xml")) && href[0]) {
                if (!resolve_path("", href, out)) { xml.failed = true; break; } found = true;
            }
        }
    }
    free(text); return !xml.failed && root && found ? ESP_OK : ESP_ERR_INVALID_ARG;
}

/* ---- manifest 与 spine / Manifest and spine ---- */

static int item_find(const item_t *items, size_t count, const char *id) {
    for (size_t i = 0; i < count; ++i) if (!strcmp(items[i].id, id)) return (int)i;
    return -1;
}
static esp_err_t package_parse(book_epub_t *book, const char *opf, char ncx[EPUB_PATH_CAP], char nav[EPUB_PATH_CAP]) {
    char *text; size_t len; esp_err_t err = load_entry(book, zip_find(book->zip, opf), &text, &len);
    if (err != ESP_OK) return err;
    item_t *items = psram(EPUB_ITEMS_MAX * sizeof(*items));
    if (!items) { free(text); return ESP_ERR_NO_MEM; }
    size_t count = 0, manifest_depth = 0; bool root = false;
    xml_t xml; xml_reader(&xml, text, len); token_t t;
    while (xml_next(&xml, &t)) {
        if (t.kind == XML_OPEN && t.depth == 1 && local_name(t.name, "package")) root = true;
        if (t.kind == XML_OPEN && local_name(t.name, "manifest")) manifest_depth = t.depth;
        if (t.kind == XML_CLOSE && t.depth == manifest_depth) manifest_depth = 0;
        if (t.kind != XML_OPEN || !manifest_depth || t.depth != manifest_depth + 1 || !local_name(t.name, "item")) continue;
        if (count == EPUB_ITEMS_MAX) { err = ESP_ERR_INVALID_SIZE; break; }
        item_t *item = &items[count]; memset(item, 0, sizeof(*item));
        char href[EPUB_PATH_CAP], media[80], properties[256];
        if (!attribute(t, "id", item->id, sizeof(item->id)) || !attribute(t, "href", href, sizeof(href)) || !attribute(t, "media-type", media, sizeof(media)) || !attribute(t, "properties", properties, sizeof(properties)) || !item->id[0] || !href[0] || item_find(items, count, item->id) >= 0 || !resolve_path(opf, href, item->path)) { err = ESP_ERR_INVALID_ARG; break; }
        item->nav = word(properties, "nav"); item->ncx = !strcmp(media, "application/x-dtbncx+xml");
        item->html = !strcmp(media, "application/xhtml+xml") || !strcmp(media, "text/html"); ++count;
    }
    if (xml.failed || !root || !count) err = ESP_ERR_INVALID_ARG;
    if (err == ESP_OK) {
        for (size_t i = 0; i < count; ++i) {
            if (!nav[0] && items[i].nav) strcpy(nav, items[i].path);
            if (!ncx[0] && items[i].ncx) strcpy(ncx, items[i].path);
        }
        xml_reader(&xml, text, len); size_t spine_depth = 0; bool seen_spine = false;
        while (xml_next(&xml, &t)) {
            if (t.kind == XML_OPEN && local_name(t.name, "spine")) {
                if (seen_spine) { err = ESP_ERR_INVALID_ARG; break; }
                seen_spine = true; spine_depth = t.depth; char toc[EPUB_ID_CAP];
                if (!attribute(t, "toc", toc, sizeof(toc))) { err = ESP_ERR_INVALID_ARG; break; }
                int index = item_find(items, count, toc); if (index >= 0 && items[index].ncx) strcpy(ncx, items[index].path);
            }
            if (t.kind == XML_CLOSE && t.depth == spine_depth) spine_depth = 0;
            if (t.kind != XML_OPEN || !spine_depth || t.depth != spine_depth + 1 || !local_name(t.name, "itemref")) continue;
            if (book->count == EPUB_ITEMS_MAX) { err = ESP_ERR_INVALID_SIZE; break; }
            char id[EPUB_ID_CAP]; if (!attribute(t, "idref", id, sizeof(id))) { err = ESP_ERR_INVALID_ARG; break; }
            int item_index = item_find(items, count, id);
            if (item_index < 0 || !items[item_index].html) { err = ESP_ERR_NOT_SUPPORTED; break; }
            int entry = zip_find(book->zip, items[item_index].path);
            if (entry < 0) { err = ESP_ERR_NOT_FOUND; break; }
            size_t bytes = zip_entry_size(book->zip, entry);
            if (bytes > EPUB_ENTRY_MAX || bytes > UINT32_MAX - book->total) { err = ESP_ERR_INVALID_SIZE; break; }
            chapter_t *chapter = &book->chapters[book->count]; chapter->zip_index = entry; chapter->offset = book->total;
            snprintf(chapter->title, sizeof(chapter->title), "第 %u 节", (unsigned)book->count + 1);
            book->total += (uint32_t)bytes; ++book->count;
        }
        if (xml.failed || !book->count) err = ESP_ERR_INVALID_ARG;
    }
    free(items); free(text); return err;
}

/* ---- NCX 与 EPUB 3 导航 / NCX and EPUB 3 navigation ---- */

static void assign_title(book_epub_t *book, const char *base, const char *href, char title[EPUB_TITLE_CAP]) {
    size_t n = strlen(title); while (n && title[n - 1] == ' ') title[--n] = 0;
    if (!n || !href[0]) return;
    char path[EPUB_PATH_CAP]; if (!resolve_path(base, href, path)) return;
    int index = zip_find(book->zip, path); if (index < 0) return;
    for (size_t i = 0; i < book->count; ++i) if (book->chapters[i].zip_index == index && !book->chapters[i].titled) {
        strcpy(book->chapters[i].title, title); book->chapters[i].titled = true;
    }
}
typedef struct {
    size_t depth, label_depth; ///< 节点与标签深度 / Node and label depths
    char href[EPUB_PATH_CAP]; ///< 内容目标 / Content target
    char title[EPUB_TITLE_CAP]; ///< 当前节点标题 / Current node title
} nav_node_t;
static esp_err_t navigation_parse(book_epub_t *book, const char *path, bool ncx) {
    char *text; size_t len; esp_err_t err = load_entry(book, zip_find(book->zip, path), &text, &len);
    if (err != ESP_OK) return err;
    nav_node_t *nodes = psram(sizeof(*nodes) * EPUB_XML_DEPTH);
    if (!nodes) { free(text); return ESP_ERR_NO_MEM; }
    unsigned char previously_titled[EPUB_ITEMS_MAX / 8] = {0};
    for (size_t i = 0; i < book->count; ++i) if (book->chapters[i].titled) previously_titled[i / 8] |= (unsigned char)(1u << (i % 8));
    size_t count = 0, toc_depth = 0; xml_t xml; xml_reader(&xml, text, len); token_t t;
    while (xml_next(&xml, &t)) {
        if (!ncx && t.kind == XML_OPEN && local_name(t.name, "nav")) {
            char type[128]; if (!attribute(t, "epub:type", type, sizeof(type))) { xml.failed = true; break; }
            if (!t.empty && word(type, "toc")) toc_depth = t.depth;
        }
        if (!ncx && t.kind == XML_CLOSE && t.depth == toc_depth) toc_depth = 0;
        bool node_open = t.kind == XML_OPEN && (ncx ? local_name(t.name, "navPoint") : toc_depth && local_name(t.name, "a"));
        if (node_open) {
            if (count == EPUB_XML_DEPTH) { xml.failed = true; break; }
            nav_node_t *node = &nodes[count++]; memset(node, 0, sizeof(*node)); node->depth = t.depth;
            if (!ncx) { node->label_depth = t.depth; if (!attribute(t, "href", node->href, sizeof(node->href))) { xml.failed = true; break; } }
            if (t.empty) --count;
            continue;
        }
        if (!count) continue;
        nav_node_t *node = &nodes[count - 1];
        if (ncx && t.kind == XML_OPEN && local_name(t.name, "navLabel")) node->label_depth = t.depth;
        if (t.kind == XML_TEXT && node->label_depth && !decode(t.text, node->title, sizeof(node->title), true, t.cdata)) { xml.failed = true; break; }
        if (ncx && t.kind == XML_OPEN && local_name(t.name, "content")) {
            if (!attribute(t, "src", node->href, sizeof(node->href))) { xml.failed = true; break; }
            assign_title(book, path, node->href, node->title);
        }
        if (t.kind == XML_CLOSE && t.depth == node->label_depth) node->label_depth = 0;
        if (t.kind == XML_CLOSE && t.depth == node->depth) {
            assign_title(book, path, node->href, node->title); --count;
        }
    }
    if (xml.failed) {
        err = ESP_ERR_INVALID_ARG;
        // 损坏目录不留下部分命名，确保备用目录可完整接管。/ A broken TOC leaves no partial labels so fallback navigation can take over fully.
        for (size_t i = 0; i < book->count; ++i) if (!(previously_titled[i / 8] & (1u << (i % 8)))) {
            book->chapters[i].titled = false;
            snprintf(book->chapters[i].title, sizeof(book->chapters[i].title), "第 %u 节", (unsigned)i + 1);
        }
    }
    free(nodes); free(text); return err;
}

/* ---- 后端公共接口 / Backend public interface ---- */

esp_err_t book_epub_open(const char *path, book_epub_t **out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = NULL; if (!path || !*path) return ESP_ERR_INVALID_ARG;
    book_epub_t *book = psram(sizeof(*book)); if (!book) return ESP_ERR_NO_MEM;
    memset(book, 0, sizeof(*book));
    char (*paths)[EPUB_PATH_CAP] = psram(3 * EPUB_PATH_CAP);
    if (!paths) { free(book); return ESP_ERR_NO_MEM; }
    memset(paths, 0, 3 * EPUB_PATH_CAP);
    char *opf = paths[0], *ncx = paths[1], *nav = paths[2];
    esp_err_t err = zip_open(path, &book->zip);
    if (err == ESP_OK) err = container_path(book, opf);
    if (err == ESP_OK) err = package_parse(book, opf, ncx, nav);
    if (err == ESP_OK) {
        // 导航损坏不阻止阅读；NAV 补全 NCX 未命中的章节。/ Broken navigation never blocks reading; NAV fills chapters not named by NCX.
        if (ncx[0]) (void)navigation_parse(book, ncx, true);
        if (nav[0]) (void)navigation_parse(book, nav, false);
    }
    free(paths);
    if (err != ESP_OK) { book_epub_close(book); return err; }
    *out = book; return ESP_OK;
}
void book_epub_close(book_epub_t *book) {
    if (!book) return;
    zip_close(book->zip); free(book);
}
size_t book_epub_chapter_count(const book_epub_t *book) { return book ? book->count : 0; }
esp_err_t book_epub_chapter_title(const book_epub_t *book, size_t i, char *buf, size_t cap) {
    if (!book || i >= book->count || !buf || !cap) return ESP_ERR_INVALID_ARG;
    buf[0] = 0; size_t n = strlen(book->chapters[i].title);
    if (n >= cap) return ESP_ERR_INVALID_SIZE;
    memcpy(buf, book->chapters[i].title, n + 1); return ESP_OK;
}
esp_err_t book_epub_load(book_epub_t *book, size_t i, html_text_t *out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out)); if (!book || i >= book->count) return ESP_ERR_INVALID_ARG;
    char *text; size_t len; esp_err_t err = load_entry(book, book->chapters[i].zip_index, &text, &len);
    if (err != ESP_OK) return err;
    err = html_to_blocks(text, len, out); free(text); return err;
}
uint32_t book_epub_total_bytes(const book_epub_t *book) { return book ? book->total : 0; }
uint32_t book_epub_chapter_byte_offset(const book_epub_t *book, size_t i) { return book && i < book->count ? book->chapters[i].offset : 0; }
