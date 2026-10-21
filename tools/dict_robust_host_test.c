/* SPDX-License-Identifier: Apache-2.0
 * 中文：损坏词典的打开校验，以及长按带来的首尾标点剥离。真构造 RPD1、真 fopen/fseek/ftell。
 * English: open validation for damaged dictionaries plus leading/trailing punctuation stripping,
 * against a real RPD1 file on disk. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../main/dict/dict_lookup.c"

int vocab_test_alloc_fail; /* heap stub 的可注入失败开关 / injectable failure switch from the stub */

#define DIR "/tmp/dict_robust_test"
#define PATH DIR "/t.bin"

/* ---- 一个最小但格式正确的 RPD1（两条：alpha / beta）---- */
static uint8_t g_img[512];
static size_t g_len;

static void put32(uint8_t* p, uint32_t v) { memcpy(p, &v, 4); }
static void put16(uint8_t* p, uint16_t v) { memcpy(p, &v, 2); }

static size_t build_image(void) {
    size_t at = 0;
    memset(g_img, 0, sizeof(g_img));
    memcpy(g_img + at, "RPD1\0\0\0\0", 8);          at = 8;
    put32(g_img + at, 1);                            at += 4;   /* version   */
    put32(g_img + at, 2);                            at += 4;   /* entries   */
    put32(g_img + at, 4);                            at += 4;   /* block_log2: >=4 */
    /* keys */
    uint32_t keys_off = 64;
    uint32_t keys_at = keys_off;
    put16(g_img + keys_at, 5); memcpy(g_img + keys_at + 2, "alpha", 5); keys_at += 7;
    put16(g_img + keys_at, 4); memcpy(g_img + keys_at + 2, "beta", 4);  keys_at += 6;
    uint32_t keys_len = keys_at - keys_off;
    /* recs */
    uint32_t rec_off = keys_at;
    put32(g_img + rec_off, 0);  put16(g_img + rec_off + 4, 6);
    put32(g_img + rec_off + 6, 6); put16(g_img + rec_off + 10, 6);
    uint32_t rec_len = 12;
    at = rec_off + rec_len;
    /* defs */
    uint32_t defs_off = at;
    memcpy(g_img + at, "A-def\n", 6); at += 6;
    memcpy(g_img + at, "B-def\n", 6); at += 6;
    uint32_t defs_len = at - defs_off;
    /* sparse: 一条，块首词头 alpha，key_off = 0 */
    uint32_t sparse_off = at;
    put32(g_img + at, 0); put16(g_img + at + 4, 5); memcpy(g_img + at + 6, "alpha", 5);
    at += 11;
    uint32_t sparse_len = at - sparse_off;
    /* header fields */
    put32(g_img + 0x14, keys_off);   put32(g_img + 0x18, keys_len);
    put32(g_img + 0x1C, rec_off);    put32(g_img + 0x20, rec_len);
    put32(g_img + 0x24, defs_off);   put32(g_img + 0x28, defs_len);
    put32(g_img + 0x2C, sparse_off); put32(g_img + 0x30, sparse_len);
    return at;
}

static void write_image(size_t len) {
    FILE* f = fopen(PATH, "wb");
    assert(f);
    assert(fwrite(g_img, 1, len, f) == len);
    fclose(f);
}

/* 每次换一个变体：清状态 → 落盘 → 试着打开。/ Reset, write, then try to open. */
static bool open_variant(const char* what, size_t len) {
    dict_close();
    write_image(len);
    bool ok = try_open(PATH);
    printf("  %-34s → %s\n", what, ok ? "接受" : "拒绝");
    return ok;
}

static uint32_t field32(size_t off) { uint32_t v; memcpy(&v, g_img + off, 4); return v; }

int main(void) {
    assert(!mkdir(DIR, 0700) || access(DIR, F_OK) == 0);
    g_len = build_image();
    printf("构造的最小词典：%zu 字节（keys=%u rec=%u defs=%u sparse=%u）\n", g_len,
           field32(0x14), field32(0x1C), field32(0x24), field32(0x2C));

    puts("\n[1] 合法文件必须能打开并查到");
    assert(open_variant("原始文件", g_len));
    char out[256];
    assert(dict_lookup_word("alpha", out, sizeof(out)) && strstr(out, "A-def"));
    assert(dict_lookup_word("beta", out, sizeof(out)) && strstr(out, "B-def"));

    puts("\n[2] 首尾标点必须被剥掉（长按常常把句号/引号一起圈进来）");
    assert(dict_lookup_word("alpha.", out, sizeof(out)) && strstr(out, "A-def"));
    assert(dict_lookup_word("(beta)", out, sizeof(out)) && strstr(out, "B-def"));
    assert(dict_lookup_word("\"alpha\"", out, sizeof(out)) && strstr(out, "A-def"));
    assert(dict_lookup_word("beta,", out, sizeof(out)) && strstr(out, "B-def"));
    assert(!dict_lookup_word("...", out, sizeof(out)) && !out[0]);
    assert(!dict_lookup_word("  ", out, sizeof(out)) && !out[0]);
    /* 词内的撇号要留下：alpha's 走 's 规则还原回 alpha */
    assert(dict_lookup_word("alpha's", out, sizeof(out)) && strstr(out, "A-def") && strstr(out, "[alpha]"));

    puts("\n[3] 损坏 / 手改的文件必须被拒绝");
    /* 截断：稀疏索引在文件末尾，任何截断都会让它读不全 */
    assert(!open_variant("尾部截断 30 字节", g_len - 30));
    assert(!open_variant("尾部截断一半", g_len / 2));
    /* 单个字段改坏 */
    g_len = build_image();
    put32(g_img + 0x18, 0x7FFFFFFF);            /* keys_len */
    assert(!open_variant("keys_len 改成 2G", g_len));
    g_len = build_image();
    put32(g_img + 0x28, 0x7FFFFFFF);            /* defs_len */
    assert(!open_variant("defs_len 改成 2G", g_len));
    g_len = build_image();
    put32(g_img + 0x2C, 0x7FFFFFFF);            /* sparse_off */
    assert(!open_variant("sparse_off 改成 2G", g_len));
    g_len = build_image();
    put32(g_img + 0x30, 0x7FFFFFFF);            /* sparse_len */
    assert(!open_variant("sparse_len 改成 2G", g_len));
    /* 冗余字段自洽性：改 entries 而不改 rec_len，或反过来 */
    g_len = build_image();
    put32(g_img + 0x0C, 3);                     /* entries 2 → 3 */
    assert(!open_variant("entries 改 3（rec_len 没跟着改）", g_len));
    g_len = build_image();
    put32(g_img + 0x20, 18);                    /* rec_len 12 → 18 */
    assert(!open_variant("rec_len 改 18（≠entries×6）", g_len));
    /* 区段顺序错乱 */
    g_len = build_image();
    uint32_t a = field32(0x1C), b = field32(0x24);
    put32(g_img + 0x1C, b); put32(g_img + 0x24, a);
    assert(!open_variant("rec_off 与 defs_off 对调", g_len));
    /* 头部落在保留区里 */
    g_len = build_image();
    put32(g_img + 0x14, 0);
    assert(!open_variant("keys_off 改成 0（压到头部）", g_len));
    /* 常规检查不能退化 */
    g_len = build_image();
    memcpy(g_img, "XXXX", 4);
    assert(!open_variant("magic 改坏", g_len));
    g_len = build_image();
    put32(g_img + 8, 2);
    assert(!open_variant("version 改成 2", g_len));

    puts("\n[4] 拒绝之后不能留下半开状态");
    assert(!dict_ready());
    assert(!dict_lookup_word("alpha", out, sizeof(out)) && !out[0]);

    dict_close();
    unlink(PATH);
    rmdir(DIR);
    puts("\nPASS: damaged or doctored dictionaries are refused at open; "
         "long-press punctuation no longer hides a word that is in the dictionary");
    return 0;
}
