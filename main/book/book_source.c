/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：TXT 与 EPUB 单实例书源分派，不依赖界面。
 * English: Singleton TXT and EPUB source dispatch without UI dependencies.
 * 冻结：TXT 保留源字节偏移，EPUB 采用累计 spine HTML 字节；兼容纯文本加载，不写文件。
 * Frozen: TXT retains source offsets; EPUB uses cumulative spine HTML bytes; retain plain-text loading and never write files.
 */
#include "book_source_internal.h"
#include "book_epub.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
static book_txt_t s_book;
static book_epub_t *s_epub;
esp_err_t book_open(const char *path) {
    book_close();
    if (!path || !*path) return ESP_ERR_INVALID_ARG;
    const char *ext = strrchr(path, '.');
    if (ext && !strcasecmp(ext, ".epub")) return book_epub_open(path, &s_epub);
    if (!ext || strcasecmp(ext, ".txt")) return ESP_ERR_NOT_SUPPORTED;
    esp_err_t err = book_txt_open(&s_book, path);
    if (err != ESP_OK) book_close();
    return err;
}
void book_close(void) {
    book_epub_close(s_epub); s_epub = NULL;
    if (s_book.file) fclose(s_book.file);
    free(s_book.entries); memset(&s_book, 0, sizeof(s_book));
}
size_t book_chapter_count(void) { return s_epub ? book_epub_chapter_count(s_epub) : s_book.count; }
esp_err_t book_chapter_title(size_t i, char *buf, size_t cap) {
    if (s_epub) return book_epub_chapter_title(s_epub, i, buf, cap);
    if (!buf || !cap || i >= s_book.count) return ESP_ERR_INVALID_ARG;
    size_t n = strlen(s_book.entries[i].title);
    if (n >= cap) { buf[0] = 0; return ESP_ERR_INVALID_SIZE; }
    memcpy(buf, s_book.entries[i].title, n + 1); return ESP_OK;
}
esp_err_t book_chapter_load(size_t i, char **utf8, size_t *len) {
    if (!utf8 || !len) return ESP_ERR_INVALID_ARG;
    *utf8 = NULL; *len = 0;
    if (s_epub) {
        html_text_t text = {0}; esp_err_t err = book_epub_load(s_epub, i, &text);
        if (err != ESP_OK) return err;
        *utf8 = text.utf8; *len = text.len; text.utf8 = NULL;
        html_text_free(&text); return ESP_OK;
    }
    if (!s_book.file || i >= s_book.count) return ESP_ERR_INVALID_ARG;
    return book_txt_load(&s_book, i, utf8, len);
}
esp_err_t book_chapter_load_blocks(size_t i, html_text_t *out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    if (s_epub) return book_epub_load(s_epub, i, out);
    return book_chapter_load(i, &out->utf8, &out->len);
}
esp_err_t book_chapter_load_image(size_t i, const char *reference, uint8_t **pixels, uint16_t *width, uint16_t *height) {
    if (!pixels || !width || !height) return ESP_ERR_INVALID_ARG;
    *pixels = NULL; *width = *height = 0;
    if (!s_epub) return ESP_ERR_NOT_SUPPORTED;
    return book_epub_load_image(s_epub, i, reference, pixels, width, height);
}
uint32_t book_total_bytes(void) { return s_epub ? book_epub_total_bytes(s_epub) : s_book.total; }
uint32_t book_chapter_byte_offset(size_t i) {
    return s_epub ? book_epub_chapter_byte_offset(s_epub, i) : i < s_book.count ? s_book.entries[i].offset : 0;
}
book_kind_t book_kind(void) { return s_epub ? BOOK_KIND_EPUB : BOOK_KIND_TXT; }
