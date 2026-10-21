/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：TF 卡上的 RPD1 离线词典查询：常驻稀疏索引 + 卡上键窗口扫描。
 * English: On-card RPD1 dictionary lookup: RAM sparse index plus one key-window read.
 *
 * 词典文件由 tools/build_dict.py 生成（ECDICT 数据，MIT），放 TF 卡 /sdcard/dict/。
 * Dictionary files come from tools/build_dict.py and live in /sdcard/dict on the card.
 *
 * 冻结：不依赖 UI；所有缓冲在 PSRAM；查询为同步阻塞调用（单次约 10-100ms）。
 * Frozen: No UI dependencies; all buffers live in PSRAM; lookups block briefly.
 */
#include "dict_lookup.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#define DICT_DIR "/sdcard/dict"
#define TAG "dict"
#define DICT_MAGIC "RPD1"
#define DICT_HEADER_SIZE 64u
#define DICT_REC_BYTES 6u
#define DICT_HEAD_MAX 64u    /* 生成端已过滤超长词头 / heads over 63 bytes are dropped */
#define DICT_SPARSE_MAX 4096u

typedef struct {
    uint32_t key_off;               /* 键区内块起始偏移 / window start in keys */
    uint16_t key_len;
    char first[DICT_HEAD_MAX];      /* 块首词头全文，内存二分用 / full first headword */
} dict_sparse_t;

typedef struct {
    FILE* file;
    uint32_t entries;
    uint32_t block_log2;
    uint32_t keys_off, keys_len;
    uint32_t rec_off;
    uint32_t defs_off, defs_len;
    dict_sparse_t* sparse;          /* PSRAM */
    uint32_t sparse_count;
    char path[300];             /* 与 dict_open 的路径缓冲同宽 / matches the buffer in dict_open */
} dict_state_t;

static dict_state_t s_dict;
/* 打开失败后的退避时间戳（ms）。目录里若放着坏词典，每次长按都重扫一遍会让界面明显卡顿。
 * Back-off stamp (ms) after a failed open: re-scanning a bad dictionary on every long press
 * makes the UI visibly stutter. */
static uint32_t s_open_failed_ms;
#define DICT_OPEN_BACKOFF_MS 5000
/* 键窗口上限。block_log2<=16 时正常窗口远小于此；损坏文件才会给出离谱的值。
 * Key-window cap. A healthy window is far smaller at block_log2<=16; only a corrupt file
 * produces an absurd one. */
#define DICT_WINDOW_MAX (1u << 20)

bool dict_ready(void) {
    return s_dict.file != NULL && s_dict.sparse != NULL;
}

static bool read_at(uint32_t off, void* buf, size_t len) {
    return s_dict.file && fseek(s_dict.file, (long)off, SEEK_SET) == 0 &&
           fread(buf, 1, len, s_dict.file) == len;
}

static bool try_open(const char* path) {
    // gen17 诊断：把尝试的完整绝对路径与 errno 打出来。此前单词卡 ERR(9) 只能看到
    // 「无词典」，无法区分「卡上没有文件」「路径错」还是「文件不是 RPD1」——
    // 一律被折叠成一个 err=9，用户被误导去查卡。
    // gen17 diagnostics: log the full absolute path attempted and the errno. Word
    // cards previously reported only ERR(9) "no dictionary", which folded
    // "no file on the card", "wrong path" and "not an RPD1 file" into one code
    // and sent users to inspect their card for no reason.
    FILE* f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "open failed: %s errno=%d (%s)", path, errno, strerror(errno));
        return false;
    }
    char header[DICT_HEADER_SIZE];
    if (fread(header, 1, sizeof(header), f) != sizeof(header) ||
        memcmp(header, DICT_MAGIC, 4) != 0) {
        ESP_LOGW(TAG, "not an RPD1 dictionary: %s (magic=%02x %02x %02x %02x)", path,
                 (unsigned char)header[0], (unsigned char)header[1],
                 (unsigned char)header[2], (unsigned char)header[3]);
        fclose(f);
        return false;
    }
    uint32_t version, entries, block_log2, fields[8];
    memcpy(&version, header + 8, 4);
    memcpy(&entries, header + 12, 4);
    memcpy(&block_log2, header + 16, 4);
    memcpy(fields, header + 0x14, 32);
    if (version != 1 || entries == 0 || block_log2 < 4 || block_log2 > 16) {
        fclose(f);
        return false;
    }
    uint32_t keys_off = fields[0], keys_len = fields[1];
    uint32_t rec_off = fields[2], rec_len = fields[3];
    uint32_t defs_off = fields[4], defs_len = fields[5];
    uint32_t sparse_off = fields[6], sparse_len = fields[7];
    /* 头部没有「文件总长度」字段，唯一能发现字段与实际字节不符的办法是拿真实大小比对。
     * 缺了这一步，offset 被改过的文件会被当成合法词典打开，查词时白白读到别的区段去。
     * The header carries no total size, so comparing against the real file size is the only way
     * to catch fields that disagree with the bytes actually on the card. Without it a doctored
     * offset opens cleanly and later reads silently come from the wrong region. */
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return false;
    }
    long end = ftell(f);
    if (end <= 0) {
        fclose(f);
        return false;
    }
    const uint64_t size = (uint64_t)end;
    /* rec_len 是 entries×6 的冗余副本，两者必须自洽；区段顺序也按格式文档固定为
     * keys → rec → defs → sparse。
     * rec_len is a redundant copy of entries×6 and must agree with it; the section order
     * follows the documented layout keys → rec → defs → sparse. */
    const bool layout_ok =
        rec_len == (uint64_t)entries * DICT_REC_BYTES &&
        keys_off >= DICT_HEADER_SIZE && keys_off < rec_off &&
        rec_off < defs_off && defs_off < sparse_off &&
        keys_off + (uint64_t)keys_len <= size &&
        rec_off + (uint64_t)rec_len <= size &&
        defs_off + (uint64_t)defs_len <= size &&
        sparse_off + (uint64_t)sparse_len <= size;
    if (!layout_ok) {
        fclose(f);
        return false;
    }
    // 每条至少 6 字节头和 1 字节词头，据此估槽位；再给上限做保护。
    uint32_t max_slots = sparse_len / 7 + 2;
    if (max_slots > DICT_SPARSE_MAX) max_slots = DICT_SPARSE_MAX;
    dict_sparse_t* sparse = heap_caps_calloc(max_slots, sizeof(*sparse),
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!sparse) {
        fclose(f);
        return false;
    }
    char* blob = heap_caps_malloc(sparse_len ? sparse_len : 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!blob) {
        free(sparse);
        fclose(f);
        return false;
    }
    bool ok = sparse_len == 0 ||
              (fseek(f, (long)sparse_off, SEEK_SET) == 0 &&
               fread(blob, 1, sparse_len, f) == sparse_len);
    uint32_t count = 0;
    uint32_t pos = 0;
    while (ok && pos + 6 <= sparse_len && count < max_slots) {
        uint32_t key_off;
        uint16_t key_len;
        memcpy(&key_off, blob + pos, 4);
        memcpy(&key_len, blob + pos + 4, 2);
        pos += 6;
        if (key_len == 0 || key_len > DICT_HEAD_MAX - 1 || pos + key_len > sparse_len) {
            ok = false;
            break;
        }
        sparse[count].key_off = key_off;
        sparse[count].key_len = key_len;
        memcpy(sparse[count].first, blob + pos, key_len);
        sparse[count].first[key_len] = 0;
        pos += key_len;
        ++count;
    }
    free(blob);
    if (!ok || count == 0) {
        free(sparse);
        fclose(f);
        return false;
    }
    if (s_dict.file) fclose(s_dict.file);
    free(s_dict.sparse);
    memset(&s_dict, 0, sizeof(s_dict));
    s_dict.file = f;
    s_dict.entries = entries;
    s_dict.block_log2 = block_log2;
    s_dict.keys_off = fields[0];
    s_dict.keys_len = fields[1];
    s_dict.rec_off = fields[2];
    s_dict.defs_off = fields[4];
    s_dict.defs_len = fields[5];
    s_dict.sparse = sparse;
    s_dict.sparse_count = count;
    snprintf(s_dict.path, sizeof(s_dict.path), "%s", path);
    ESP_LOGI(TAG, "opened: %s entries=%u block_log2=%u sparse=%u", s_dict.path,
             (unsigned)entries, (unsigned)block_log2, (unsigned)count);
    return true;
}

bool dict_open(void) {
    // 退避：上次失败后 DICT_OPEN_BACKOFF_MS 内不再重扫，避免每次长按都重读坏文件。
    // Back off: skip rescanning for DICT_OPEN_BACKOFF_MS after a failure.
    if (!dict_ready()) {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        if (s_open_failed_ms && now - s_open_failed_ms < DICT_OPEN_BACKOFF_MS) return false;
    }
    DIR* dir = opendir(DICT_DIR);
    if (!dir) {
        // gen17：目录打不开就报出绝对路径与 errno（区分「卡未挂载」「目录不存在」
        // 「路径拼错」三种可能）。/ gen17: report the absolute path and errno so a
        // missing mount, a missing directory and a mistyped path are distinguishable.
        ESP_LOGW(TAG, "opendir failed: %s errno=%d (%s)", DICT_DIR, errno, strerror(errno));
        s_open_failed_ms = (uint32_t)(esp_timer_get_time() / 1000);
        return false;
    }
    bool found = false;
    struct dirent* ent;
    char path[300];
    while (!found && (ent = readdir(dir)) != NULL) {
        size_t len = strlen(ent->d_name);
        if (len < 4 || strcasecmp(ent->d_name + len - 4, ".bin") != 0) continue;
        snprintf(path, sizeof(path), DICT_DIR "/%s", ent->d_name);
        found = try_open(path);
    }
    closedir(dir);
    s_open_failed_ms = found ? 0 : (uint32_t)(esp_timer_get_time() / 1000);
    return found;
}

/* 预热：进阅读页时先把词典打开，把首次长按需付的开销挪出查词路径。
 * Warm-up: open the dictionary when the reader opens so the first long press
 * does not pay for it. A warm-up failure must NOT arm the back-off, or a card
 * that is not ready yet would silence real lookups for DICT_OPEN_BACKOFF_MS. */
bool dict_warm(void) {
    if (dict_ready()) return true;
    bool ok = dict_open();
    if (!ok) s_open_failed_ms = 0;
    return ok;
}

bool dict_rescan(void) {
    dict_close();
    s_open_failed_ms = 0; // 换卡后必须立刻重试，不能被退避挡住 / a new card must retry at once
    return dict_open();
}

void dict_close(void) {
    if (s_dict.file) fclose(s_dict.file);
    free(s_dict.sparse);
    memset(&s_dict, 0, sizeof(s_dict));
}

/* ---- 单词查询：定位块 → 读键窗口 → 线性扫描 → 读释义。 ---- */
static bool lookup_exact(const char* key, char* out, size_t cap, size_t* out_len) {
    if (!dict_ready() || !key || !*key) return false;
    const size_t key_len = strlen(key);
    if (key_len >= DICT_HEAD_MAX) return false;

    /* 内存二分：最后一个 first <= key 的稀疏条目。 */
    uint32_t lo = 0, hi = s_dict.sparse_count;
    while (lo + 1 < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (strcmp(s_dict.sparse[mid].first, key) <= 0) lo = mid;
        else hi = mid;
    }
    if (strcmp(s_dict.sparse[lo].first, key) > 0) return false;

    const uint32_t block = 1u << s_dict.block_log2;
    const uint32_t win_off = s_dict.sparse[lo].key_off;
    const uint32_t win_end = lo + 1 < s_dict.sparse_count
        ? s_dict.sparse[lo + 1].key_off : s_dict.keys_len;
    if (win_end <= win_off || win_end > s_dict.keys_len) return false;
    const uint32_t win_len = win_end - win_off;
    if (win_len == 0 || win_len > DICT_WINDOW_MAX) return false;
    const uint32_t first = lo << s_dict.block_log2;
    uint32_t count = s_dict.entries - first;
    if (count > block) count = block;

    char* win = heap_caps_malloc(win_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    char* recs = heap_caps_malloc((size_t)count * DICT_REC_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    bool hit = false;
    if (win && recs && read_at(s_dict.keys_off + win_off, win, win_len) &&
        read_at(s_dict.rec_off + (uint64_t)first * DICT_REC_BYTES, recs, (size_t)count * DICT_REC_BYTES)) {
        uint32_t pos = 0, rpos = 0;
        while (pos + 2 <= win_len && rpos + DICT_REC_BYTES <= (uint32_t)count * DICT_REC_BYTES) {
            uint16_t klen;
            memcpy(&klen, win + pos, 2);
            if (klen == 0 || pos + 2u + klen > win_len) break;
            const char* kb = win + pos + 2;
            int cmp = memcmp(kb, key, klen < key_len ? klen : key_len);
            if (cmp == 0) cmp = klen < key_len ? -1 : (klen > key_len ? 1 : 0);
            if (cmp == 0) {
                uint32_t def_off;
                uint16_t def_len;
                memcpy(&def_off, recs + rpos, 4);
                memcpy(&def_len, recs + rpos + 4, 2);
                if (def_len && def_off + def_len <= s_dict.defs_len && def_len < cap) {
                    if (read_at(s_dict.defs_off + def_off, out, def_len)) {
                        out[def_len] = 0;
                        if (out_len) *out_len = def_len;
                        hit = true;
                    }
                }
                break;
            }
            if (cmp > 0) break;
            pos += 2u + klen;
            rpos += DICT_REC_BYTES;
        }
    }
    free(win);
    free(recs);
    return hit;
}

/* ---- 轻量词形还原：与 tools/dict_probe.py 的 _candidates 保持一致。 ---- */
static bool ends(const char* w, const char* suf) {
    size_t lw = strlen(w), ls = strlen(suf);
    return lw > ls && strcmp(w + lw - ls, suf) == 0;
}

static size_t make_candidates(const char* w, char out[][DICT_WORD_MAX], size_t max) {
    size_t n = 0;
    char buf[DICT_WORD_MAX];
    size_t len = strlen(w);

    #define ADD(candidate) do { \
        if (n < max && (candidate)[0] && strcmp(candidate, w) != 0) { \
            bool dup = false; \
            for (size_t i = 0; i < n; ++i) if (strcmp(out[i], (candidate)) == 0) { dup = true; break; } \
            if (!dup) snprintf(out[n++], DICT_WORD_MAX, "%s", (candidate)); \
        } \
    } while (0)

    if (ends(w, "'s")) { snprintf(buf, sizeof(buf), "%.*s", (int)(len - 2), w); ADD(buf); }
    if (ends(w, "ies") && len > 4) { snprintf(buf, sizeof(buf), "%.*sy", (int)(len - 3), w); ADD(buf); }
    if (ends(w, "es") && len > 3) { snprintf(buf, sizeof(buf), "%.*s", (int)(len - 2), w); ADD(buf); }
    if (ends(w, "s") && !ends(w, "ss") && len > 2) { snprintf(buf, sizeof(buf), "%.*s", (int)(len - 1), w); ADD(buf); }
    if (ends(w, "ied") && len > 4) { snprintf(buf, sizeof(buf), "%.*sy", (int)(len - 3), w); ADD(buf); }
    if (ends(w, "ed") && len > 3) {
        snprintf(buf, sizeof(buf), "%.*s", (int)(len - 2), w); ADD(buf);          /* walked -> walk */
        snprintf(buf, sizeof(buf), "%.*s", (int)(len - 1), w); ADD(buf);          /* loved  -> love */
        if (len > 5 && w[len - 3] == w[len - 4] && strchr("bcdfgklmnprstz", w[len - 3])) {
            snprintf(buf, sizeof(buf), "%.*s", (int)(len - 3), w); ADD(buf);      /* stopped -> stop */
        }
    }
    if (ends(w, "ing") && len > 4) {
        snprintf(buf, sizeof(buf), "%.*s", (int)(len - 3), w); ADD(buf);          /* doing -> do */
        snprintf(buf, sizeof(buf), "%.*se", (int)(len - 3), w); ADD(buf);         /* writing -> write */
        if (len > 5 && w[len - 4] == w[len - 5] && strchr("bcdfgklmnprstz", w[len - 4])) {
            snprintf(buf, sizeof(buf), "%.*s", (int)(len - 4), w); ADD(buf);      /* running -> run */
        }
    }
    static const char* cmp_suf[] = {"er", "est"};
    for (size_t i = 0; i < 2; ++i) {
        const char* suf = cmp_suf[i];
        size_t ls = strlen(suf);
        if (ends(w, suf) && len > ls + 2) {
            snprintf(buf, sizeof(buf), "%.*s", (int)(len - ls), w); ADD(buf);     /* taller -> tall */
            if (len > ls + 3 && w[len - ls - 1] == w[len - ls - 2] &&
                strchr("bcdfgklmnprstz", w[len - ls - 1])) {
                snprintf(buf, sizeof(buf), "%.*s", (int)(len - ls - 1), w); ADD(buf); /* bigger -> big */
            }
            if ((ends(w, "ier") || ends(w, "iest")) && len > ls + 2) {
                snprintf(buf, sizeof(buf), "%.*sy", (int)(len - ls - 1), w); ADD(buf); /* happier -> happy */
            }
        }
    }
    if (ends(w, "ily") && len > 4) { snprintf(buf, sizeof(buf), "%.*sy", (int)(len - 3), w); ADD(buf); }
    else if (ends(w, "ly") && len > 4) { snprintf(buf, sizeof(buf), "%.*s", (int)(len - 2), w); ADD(buf); }

    #undef ADD
    return n;
}

/* 词字符：小写字母、数字，以及留在词内的撇号与连字符（don't / well-known）。
 * Word characters: lowercase letters, digits, plus apostrophes and hyphens inside a word. */
static bool dict_word_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '\'' || c == '-';
}

bool dict_lookup_word(const char* word, char* out, size_t cap) {
    if (!word || !out || cap < 64) return false;
    out[0] = 0;
    /* 归一：ASCII 小写。 */
    char raw[DICT_WORD_MAX];
    size_t raw_len = 0;
    for (const char* p = word; *p && raw_len + 1 < sizeof(raw); ++p) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        raw[raw_len++] = c;
    }
    raw[raw_len] = 0;
    /* 去首尾非词字符：长按经常把句号、逗号、括号一起圈进来（得到 "country." 而不是
     * "country"），不剥掉就永远查不到。剥完为空说明这个"词"全是标点。
     * Strip leading and trailing non-word characters: a long press routinely swallows the
     * trailing period or comma, and "country." would never match. An empty result means the
     * selection was nothing but punctuation. */
    size_t a = 0, b = raw_len;
    while (a < b && !dict_word_char(raw[a])) ++a;
    while (b > a && !dict_word_char(raw[b - 1])) --b;
    char key[DICT_WORD_MAX];
    size_t n = 0;
    while (a < b && n + 1 < sizeof(key)) key[n++] = raw[a++];
    key[n] = 0;
    if (!key[0]) return false;
    if (lookup_exact(key, out, cap, NULL)) return true;

    char cands[8][DICT_WORD_MAX];
    size_t count = make_candidates(key, cands, 8);
    for (size_t i = 0; i < count; ++i) {
        if (lookup_exact(cands[i], out, cap, NULL)) {
            /* 释义首行前插 [原形] 提示；原地腾挪，避免再开一个大栈缓冲。
             * Prefix the base form hint in place; no second large stack buffer. */
            size_t cl = strlen(cands[i]);
            size_t used = strlen(out);
            if (used + cl + 4 < cap) {
                memmove(out + cl + 3, out, used + 1);
                out[0] = '[';
                memcpy(out + 1, cands[i], cl);
                out[cl + 1] = ']';
                out[cl + 2] = '\n';
            }
            return true;
        }
    }
    return false;
}
