/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 离线书名匹配边界与性能测试。/ Offline filename boundary and performance tests.
 */
#include "read_pico_search.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static void match(const char* name, const char* q) { assert(read_pico_search_match(name, q)); }
static void miss(const char* name, const char* q) { assert(!read_pico_search_match(name, q)); }
int main(void) {
    match("三体.txt", "st"); match("三体.TXT", "san ti"); match("三体.txt", "SANTI");
    match("三体.txt", "体"); match("三体.txt", ""); match("三体.epub", "   ");
    miss("三体.txt", "txt"); miss("三体.EPUB", "epub"); miss("三体.txt", "santi2");
    match("重庆.epub", "chongqing"); match("重庆.epub", "zhongqing"); match("重庆.epub", "cq");
    match("长安.txt", "changan"); match("长安.txt", "zhang an");
    match("女儿与绿茶.epub", "nver"); match("女儿与绿茶.epub", "lvcha");
    match("The Art of War.txt", "TAOW"); match("The Art of War.txt", "ART OF");
    match("The Art of War.txt", "theartofwar");
    match("三体 Three Body 2026.epub", "sttb2026");
    match("三体 Three Body 2026.epub", "santithreebody2026");
    match("ABC甲乙.txt", "bcjiayi"); match("ABC甲乙.txt", "ajy");
    match("第2部.txt", "d2b"); match("第2部.txt", "di2bu");
    miss("甲🙂乙.txt", "jiayi"); match("甲🙂乙.txt", "🙂");
    miss("甲\xc0\xaf.txt", "a"); miss("甲.txt", "\xed\xa0\x80");
    miss("甲\xf0\x9f.txt", "a"); miss(NULL, ""); miss("a.txt", NULL);
    char query[66]; memset(query, 'a', 64); query[64] = 0;
    char title[257]; memset(title, 'a', 251); memcpy(title + 251, ".txt", 5);
    match(title, query); query[64] = 'a'; query[65] = 0; miss(title, query);
    memset(title, 'a', 252); memcpy(title + 252, ".txt", 5); miss(title, "a");
    // 长多音书名避免组合爆炸；1000个255字节书名。/ Long polyphonic names avoid exponential combinations; 1000 names of 255 bytes.
    for (int i = 0; i < 83; ++i) memcpy(title + i * 3, "重", 3);
    memcpy(title + 249, "AA.txt", 7);
    clock_t start = clock();
    for (int i = 0; i < 1000; ++i) miss(title, "chongzhongchongzhongchongzhongchongzhongchongzhongchongzhongxyz");
    double elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;
    printf("search: PASS; 1000 x 255-byte polyphonic names: %.3f seconds\n", elapsed);
    return 0;
}
