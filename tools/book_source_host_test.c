/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：以真实文件验证书源解析。
 * English: Exercise source parsing with real files.
 * 冻结：仅供主机验证。/ Frozen: Host validation only.
 */
#include "book_source.h"
#include "gbk.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char *path = "/tmp/book-source-test.txt";
static void write_bytes(const char *s, size_t n) {
    FILE *f = fopen(path, "wb"); assert(f); assert(fwrite(s, 1, n, f) == n); fclose(f);
}
static void roundtrip(const char *s, size_t n) {
    size_t seen = 0; char *out; size_t len;
    assert(book_open(path) == ESP_OK); assert(book_total_bytes() == n);
    for (size_t i = 0; i < book_chapter_count(); ++i) {
        assert(book_chapter_byte_offset(i) == seen);
        assert(book_chapter_load(i, &out, &len) == ESP_OK);
        assert(len <= 96 * 1024); assert(memcmp(out, s + seen, len) == 0);
        assert(out[len] == 0); seen += len; free(out);
    }
    assert(seen == n); book_close();
}
int main(int argc, char **argv) {
    const char *s = "Chapter 1 Start\nhello\nChapter 2 End\nworld\n";
    write_bytes(s, strlen(s)); assert(book_open(path) == ESP_OK);
    assert(book_chapter_count() == 2); char title[41];
    assert(book_chapter_title(1, title, sizeof(title)) == ESP_OK);
    assert(strcmp(title, "Chapter 2 End") == 0); book_close(); roundtrip(s, strlen(s));
    s = "第一章 开始\n你好\n第二章 结束\n再见\n";
    write_bytes(s, strlen(s)); assert(book_open(path) == ESP_OK); assert(book_chapter_count() == 2); book_close();
    char *large = malloc(300000); assert(large);
    for (size_t i = 0; i < 100000; ++i) memcpy(large + i * 3, "中", 3);
    write_bytes(large, 300000); roundtrip(large, 300000); free(large);
    const char gbk[] = { (char)0xd6,(char)0xd0,(char)0xce,(char)0xc4,'\n' };
    write_bytes(gbk, sizeof(gbk)); assert(book_open(path) == ESP_OK);
    char *out; size_t len; assert(book_chapter_load(0, &out, &len) == ESP_OK);
    assert(strcmp(out, "中文\n") == 0); free(out); book_close();
    char tiny[5] = {0};
    assert(gbk_to_utf8(gbk, sizeof(gbk), tiny, 4) == 3); assert(strcmp(tiny, "中") == 0);
    assert(gbk_to_utf8("\x81", 1, tiny, sizeof(tiny)) == 3); assert(strcmp(tiny, "\xef\xbf\xbd") == 0);
    s = "\xef\xbb\xbf" "Chapter 1\r\nx\xff\nChapter 2\r\ny";
    write_bytes(s, strlen(s)); assert(book_open(path) == ESP_OK); assert(book_chapter_count() == 2);
    assert(book_chapter_load(0, &out, &len) == ESP_OK);
    assert(strcmp(out, "Chapter 1\r\nx\xef\xbf\xbd\n") == 0); free(out);
    assert(book_chapter_title(0, tiny, sizeof(tiny)) == ESP_ERR_INVALID_SIZE);
    assert(book_chapter_load(99, &out, &len) == ESP_ERR_INVALID_ARG); assert(!out && !len); book_close();
    large = malloc(70000); assert(large); memset(large, 'a', 65535);
    memcpy(large + 65535, "中\n", 4); write_bytes(large, 65539); roundtrip(large, 65539);
    large[65535] = (char)0xd6; large[65536] = (char)0xd0; large[65537] = '\n';
    write_bytes(large, 65538); assert(book_open(path) == ESP_OK);
    assert(book_chapter_load(0, &out, &len) == ESP_OK); assert(len == 65539);
    assert(!memcmp(out + 65535, "中\n", 4)); free(out); free(large); book_close();
    FILE *f = fopen(path, "wb"); assert(f);
    for (size_t i = 0; i < 2200; ++i) fprintf(f, "Chapter %zu\nbody\n", i);
    fclose(f); assert(book_open(path) == ESP_OK); assert(book_chapter_count() <= 2048); book_close();
    s = "preface\nChapter 1\nbody\nChapter 2\nend";
    write_bytes(s, strlen(s)); roundtrip(s, strlen(s));
    write_bytes("", 0); assert(book_open(path) == ESP_OK); assert(book_chapter_count() == 1); book_close();
    assert(book_open("/tmp/missing.epub") == ESP_ERR_NOT_FOUND);
    assert(book_open("/tmp/missing.unknown") == ESP_ERR_NOT_SUPPORTED);
    assert(book_chapter_count() == 0);
    for (int a = 1; a < argc; ++a) {
        assert(book_open(argv[a]) == ESP_OK); assert(book_chapter_count() > 0);
        bool epub = strstr(argv[a], ".epub") != NULL;
        assert(book_kind() == (epub ? BOOK_KIND_EPUB : BOOK_KIND_TXT));
        size_t sum = 0;
        for (size_t i = 0; i < book_chapter_count(); ++i) {
            assert(book_chapter_load(i, &out, &len) == ESP_OK); assert(out[len] == 0);
            html_text_t blocks = {0}; assert(book_chapter_load_blocks(i, &blocks) == ESP_OK);
            assert(blocks.len == len && !memcmp(blocks.utf8, out, len));
            assert(epub ? blocks.blocks != NULL && blocks.count > 0 : blocks.blocks == NULL && blocks.count == 0);
            html_text_free(&blocks);
            assert(book_chapter_title(i, title, sizeof(title)) == ESP_OK);
            sum += len; free(out);
        }
        printf("fixture %s: %zu chapters, %zu UTF-8 bytes\n", argv[a], book_chapter_count(), sum);
        book_close();
        assert(book_kind() == BOOK_KIND_TXT && book_chapter_count() == 0);
    }
    puts("book source host tests passed"); return 0;
}
