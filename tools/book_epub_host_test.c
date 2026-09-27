/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：EPUB 容器、spine、目录与正文集成测试。
 * English: EPUB container, spine, navigation and text integration tests.
 * 冻结：仅用于主机测试。/ Frozen: Host tests only.
 */
#include "book_epub.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv) {
    assert(argc > 1);
    for (int a = 1; a < argc; ++a) {
        book_epub_t *book = NULL;
        if (strstr(argv[a], "/bad_")) {
            assert(book_epub_open(argv[a], &book) != ESP_OK && !book);
            printf("epub rejection passed: %s\n", argv[a]); continue;
        }
        assert(book_epub_open(argv[a], &book) == ESP_OK && book);
        assert(book_epub_chapter_count(book) == 4);
        uint32_t previous = 0;
        for (size_t i = 0; i < 4; ++i) {
            char title[160]; html_text_t text = {0};
            assert(book_epub_chapter_title(book, i, title, sizeof(title)) == ESP_OK);
            assert(title[0]);
            if (strstr(argv[a], "good_defaults")) assert(!strncmp(title, "第 ", strlen("第 ")));
            else assert(strncmp(title, "第 ", strlen("第 ")));
            if (strstr(argv[a], "good_paths") && i < 2) assert(!strcmp(title, i ? "Child Two" : "Parent & One"));
            if (strstr(argv[a], "good_navfallback")) assert(!strncmp(title, "NAV ", 4));
            uint32_t offset = book_epub_chapter_byte_offset(book, i);
            assert(i ? offset > previous : offset == 0); previous = offset;
            assert(book_epub_load(book, i, &text) == ESP_OK);
            assert(text.utf8 && text.len && text.blocks && text.count);
            html_text_free(&text);
        }
        assert(book_epub_total_bytes(book) > previous);
        book_epub_close(book); printf("epub fixture passed: %s\n", argv[a]);
    }
    puts("epub host tests passed");
}
