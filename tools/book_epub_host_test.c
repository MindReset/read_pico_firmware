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
#include <stdbool.h>
#include <string.h>
#ifdef BOOK_HEAP_TRACK
void book_heap_reset(void);
void book_heap_report(void);
#endif
int main(int argc, char **argv) {
    assert(argc > 1);
    bool inspect = !strcmp(argv[1], "--inspect");
    for (int a = inspect ? 2 : 1; a < argc; ++a) {
#ifdef BOOK_HEAP_TRACK
        book_heap_reset();
#endif
        book_epub_t *book = NULL;
        if (strstr(argv[a], "/bad_")) {
            assert(book_epub_open(argv[a], &book) != ESP_OK && !book);
            printf("epub rejection passed: %s\n", argv[a]); continue;
        }
        assert(book_epub_open(argv[a], &book) == ESP_OK && book);
        size_t count = book_epub_chapter_count(book);
        bool large = strstr(argv[a], "good_large") != NULL;
        bool many = strstr(argv[a], "good_many") != NULL;
        bool limit = strstr(argv[a], "good_spine_limit") != NULL;
        bool previous_limit = strstr(argv[a], "good_spine_2048") != NULL;
        if (!inspect) assert(count == (many ? 30000u : large ? 1486u : limit ? 32768u : previous_limit ? 2048u : 4u));
        assert(count);
        uint32_t previous = 0;
        size_t readable = 0, image_refs = 0, repeated_images = 0, title_images = 0;
        for (size_t i = 0; i < count; ++i) {
            char title[160]; html_text_t text = {0};
            assert(book_epub_chapter_title(book, i, title, sizeof(title)) == ESP_OK);
            assert(title[0]);
            if (strstr(argv[a], "good_many_titled")) {
                char expected[80]; snprintf(expected, sizeof(expected), "Chapter %zu", i);
                assert(!strcmp(title, expected));
            }
            else if (strstr(argv[a], "good_defaults") || large || many) assert(!strncmp(title, "第 ", strlen("第 ")));
            else if (!inspect) assert(strncmp(title, "第 ", strlen("第 ")));
            if (strstr(argv[a], "good_paths") && i < 2) assert(!strcmp(title, i ? "Child Two" : "Parent & One"));
            if (strstr(argv[a], "good_navfallback")) assert(!strncmp(title, "NAV ", 4));
            uint32_t offset = book_epub_chapter_byte_offset(book, i);
            assert(i ? offset > previous : offset == 0); previous = offset;
            assert(book_epub_load(book, i, &text) == ESP_OK);
            for (size_t b = 0; b < text.count; ++b) if (text.blocks[b].image_src) {
                assert(!text.blocks[b].image);
                ++image_refs;
                if (text.blocks[b].image_title) ++title_images;
                if (text.blocks[b].image_repeated) {
                    assert(text.blocks[b].image_first_chapter < i);
                    ++repeated_images;
                }
            }
            if (text.len) {
                assert(text.utf8 && text.blocks && text.count);
                ++readable;
            } else assert(inspect);
            html_text_free(&text);
        }
        assert(book_epub_total_bytes(book) > previous);
        assert(readable);
        book_epub_close(book); printf("epub fixture passed: %s (%zu chapters, %zu with text)\n", argv[a], count, readable);
        if (image_refs) printf("image placeholders=%zu, repeated=%zu, title images=%zu, automatic decodes=0\n", image_refs, repeated_images, title_images);
#ifdef BOOK_HEAP_TRACK
        book_heap_report();
#endif
    }
    puts("epub host tests passed");
}
