/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：生词本落盘。TSV 一行一条：词 / 时间 / 书名 / 原句 / 释义。
 * English: Vocabulary-book storage. One TSV row per entry:
 * word / timestamp / book / sentence / definition.
 *
 * 用户修订：只由 chip 手动保存，绝不自动记录；索引分配失败退化为扫文件，
 * 绝不因为生词本而卡住界面。
 * User revision: saving is manual only, never automatic; if the index allocation
 * fails we degrade to a file scan rather than ever stalling the UI.
 */
#include "vocab_log.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#include "book_title.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char* TAG = "vocab";

// 宿主测试会用 -D 覆盖这三条，指向临时目录。
// The host test overrides these three with -D to point at a temp directory.
#ifndef VOCAB_DIR
#define VOCAB_DIR "/sdcard/.readpico"
#endif
#ifndef VOCAB_PATH
#define VOCAB_PATH "/sdcard/.readpico/vocab.txt"
#endif
#ifndef VOCAB_TMP
#define VOCAB_TMP "/sdcard/.readpico/.vocab.tmp"
#endif
// 虚拟书：把 TSV 渲染成排版好的 TXT，交给现成阅读器打开。
// The virtual book: the TSV rendered into a laid-out TXT the existing reader opens.
#ifndef VOCAB_VIEW
#define VOCAB_VIEW "/sdcard/.readpico/vocab_view.txt"
#endif
#ifndef VOCAB_VIEW_TMP
#define VOCAB_VIEW_TMP "/sdcard/.readpico/.vocab_view.tmp"
#endif
// 每多少词起一章。标题必须写成「第N章」（中间无空格），book_txt.c 的 heading() 才认。
// Entries per chapter. The title must read "第N章" with no space or book_txt.c's
// heading() will not recognize it.
#ifndef VOCAB_CHUNK
#define VOCAB_CHUNK 50
#endif

#define VOCAB_HEADER "# read-pico vocab.txt — 列：word<TAB>时间<TAB>书名<TAB>原句<TAB>释义"
#define VOCAB_INDEX_MAX 4096u
#ifndef VOCAB_FILE_MAX // 宿主测试会把它缩小，好把「写满」也测到 / the host test shrinks it to exercise the cap
#define VOCAB_FILE_MAX (2u * 1024u * 1024u)
#endif
// 原句向前后各扫多少源字节。有界，所以抓取不会退化成整章扫描。
// How far to scan each way for sentence bounds; bounded so capture never becomes
// a whole-chapter walk.
#define VOCAB_SCAN_MAX 240
#define VOCAB_SENTENCE_MAX 256
#define VOCAB_LINE_MAX 1024

/* 一次长按的上下文：书名路径与抓下来的原句。定长，不做动态分配，
 * 于是「抓上下文」这条路根本没有失败分支。
 * One long press of context: the book path and the captured sentence. Fixed size
 * and never allocated, so capturing has no failure path at all. */
static char s_book[288];
static char s_sentence[VOCAB_SENTENCE_MAX];

/* 刚被移除的那一行原文。紧接着的「保存」把它原样写回，于是误删可撤销——
 * 也顺带避免了「在生词本视图里删了再存，会用视图里抓来的句子污染这一行」。
 * The row just removed. An immediate "save" writes it back verbatim, so an
 * accidental removal is undoable — and a removed-then-re-saved entry can never
 * be polluted by a sentence captured inside the vocabulary view itself. */
static char s_undo[VOCAB_LINE_MAX];

/* 去重索引：PSRAM 里的有序 char* 数组。分配失败就保持 NULL，
 * 由 save() 退化成逐行扫文件；contains() 只读它，绝不读卡。
 * De-duplication index: a sorted char* array in PSRAM. On allocation failure it
 * stays NULL and save() degrades to a file scan; contains() only reads it and
 * never touches the card. */
static char** s_words;
static size_t s_count;

static bool dir_ready(void) {
    struct stat st;
    if (stat(VOCAB_DIR, &st) == 0 && S_ISDIR(st.st_mode)) return true;
    if (mkdir(VOCAB_DIR, 0777) == 0) return true;
    return errno == EEXIST;
}

/* ---------- 纯字符串工具（宿主端单测覆盖） ---------- */

// 字段转义：\t \r \n 与控制字符变空格，连续空格压缩成一个，两端去空格。
// Field escaping: tabs, CR/LF and control characters become spaces, runs of
// spaces collapse to one, and both ends are trimmed.
static void field_escape(char* out, size_t cap, const char* in) {
    size_t o = 0;
    bool pending_space = false;
    for (const char* p = in; p && *p; ++p) {
        unsigned char c = (unsigned char)*p;
        // <= 0x20：普通空格本身就是 0x20，不能被漏掉。
        // <= 0x20: an ordinary space is 0x20 itself and must not slip through.
        bool space = c <= 0x20;
        if (space) { pending_space = o > 0; continue; }
        if (pending_space) {
            if (o + 1 >= cap) break;
            out[o++] = ' ';
            pending_space = false;
        }
        if (o + 1 >= cap) break;
        out[o++] = (char)c;
    }
    out[o] = 0;
}

// 句末判定：返回该标点的字节长度（1 或 3），0 表示不是句末。
// Sentence terminator test: returns the byte length (1 or 3), 0 when not one.
static size_t term_at(const char* t, size_t i, size_t len) {
    if (i >= len) return 0;
    unsigned char c = (unsigned char)t[i];
    if (c < 0x80) {
        if (c == '.' || c == '!' || c == '?' || c == ';' || c == '\n' || c == '\r') return 1;
        return 0;
    }
    if ((c & 0xc0) == 0x80) return 0; // 续字节不是句首 / a continuation byte never starts one
    static const char* terms[] = {"。", "！", "？", "；"};
    for (size_t k = 0; k < sizeof(terms) / sizeof(terms[0]); ++k) {
        size_t n = strlen(terms[k]);
        if (i + n <= len && !strncmp(t + i, terms[k], n)) return n;
    }
    return 0;
}

static size_t prev_cp(const char* t, size_t i) {
    if (!i) return 0;
    size_t p = i - 1;
    while (p && ((unsigned char)t[p] & 0xc0) == 0x80) --p;
    return p;
}

static size_t next_cp(const char* t, size_t i, size_t len) {
    size_t n = 1;
    while (i + n < len && ((unsigned char)t[i + n] & 0xc0) == 0x80) ++n;
    return i + n < len ? i + n : len;
}

// 从原文里切出包含 [start,end) 的整句，转义后放进 s_sentence。
// Slice the sentence containing [start,end) out of the source text and escape it
// into s_sentence.
static void sentence_capture(const char* text, size_t len, size_t start, size_t end) {
    s_sentence[0] = 0;
    if (!text || start >= len) return;
    if (end > len) end = len;
    if (end < start) end = start;

    size_t back = start > VOCAB_SCAN_MAX ? start - VOCAB_SCAN_MAX : 0;
    size_t lo = back;
    for (size_t p = prev_cp(text, start); p > back; p = prev_cp(text, p)) {
        size_t n = term_at(text, p, len);
        if (n) { lo = p + n; break; }
    }

    size_t fwd = end + VOCAB_SCAN_MAX < len ? end + VOCAB_SCAN_MAX : len;
    size_t hi = fwd;
    for (size_t q = end; q < fwd; q = next_cp(text, q, len)) {
        size_t n = term_at(text, q, len);
        if (n) { hi = q + n; break; }
    }
    if (hi <= lo) return;

    // 截断时退到 UTF-8 边界，避免切出半个汉字。
    // Back off to a UTF-8 boundary when truncating so no multibyte char is split.
    size_t take = hi - lo;
    if (take > VOCAB_SCAN_MAX * 2) take = VOCAB_SCAN_MAX * 2;
    while (take > 1 && ((unsigned char)text[lo + take] & 0xc0) == 0x80) --take;
    char raw[VOCAB_SCAN_MAX * 2 + 1];
    memcpy(raw, text + lo, take);
    raw[take] = 0;
    field_escape(s_sentence, sizeof(s_sentence), raw);
}

static void lower_copy(char* out, size_t cap, const char* in) {
    size_t i = 0;
    for (; in && in[i] && i + 1 < cap; ++i)
        out[i] = (char)(in[i] >= 'A' && in[i] <= 'Z' ? in[i] + 32 : in[i]);
    out[i] = 0;
}

static const char* stamp(char* out, size_t cap) {
    // 没有可信时钟就不编造日期（与 book_ticket.c 的 local_day() 同一判据）。
    // Without a trustworthy clock, invent no date (same test as book_ticket.c).
    time_t now = time(NULL);
    struct tm tm;
    if (now < 1704067200 || !localtime_r(&now, &tm) ||
        !strftime(out, cap, "%Y-%m-%d %H:%M", &tm) || !out[0])
        snprintf(out, cap, "-");
    return out;
}

/* ---------- 去重索引 ---------- */

// 索引条目固定放 PSRAM；分配失败由调用方退化处理。
// Index entries always live in PSRAM; callers degrade when this fails.
static char* psram_strdup(const char* s) {
    size_t n = strlen(s) + 1;
    char* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p) memcpy(p, s, n);
    return p;
}

static int cmp_word(const void* a, const void* b) {
    return strcmp(*(const char* const*)a, *(const char* const*)b);
}

static bool index_find(const char* lower, size_t* pos) {
    size_t low = 0, high = s_count;
    while (low < high) {
        size_t mid = (low + high) / 2;
        int c = strcmp(s_words[mid], lower);
        if (c < 0) low = mid + 1;
        else if (c > 0) high = mid;
        else { if (pos) *pos = mid; return true; }
    }
    if (pos) *pos = low;
    return false;
}

static void index_add(const char* lower) {
    if (!s_words || !*lower) return;
    size_t pos = 0;
    if (index_find(lower, &pos)) return;
    if (s_count >= VOCAB_INDEX_MAX) {
        ESP_LOGW(TAG, "index full (%u), dropping de-duplication for new words",
                 (unsigned)VOCAB_INDEX_MAX);
        return;
    }
    char* copy = psram_strdup(lower);
    if (!copy) { ESP_LOGW(TAG, "index entry alloc failed"); return; }
    memmove(&s_words[pos + 1], &s_words[pos], (s_count - pos) * sizeof(s_words[0]));
    s_words[pos] = copy;
    ++s_count;
}

static void index_drop_at(size_t pos) {
    heap_caps_free(s_words[pos]);
    memmove(&s_words[pos], &s_words[pos + 1], (s_count - pos - 1) * sizeof(s_words[0]));
    --s_count;
}

static bool index_load(void) {
    if (s_words) return true;
    // 先分配再读：分配失败时直接退化，不去动文件。
    // Allocate before reading: on failure we degrade immediately without touching
    // the file.
    char** words = heap_caps_calloc(VOCAB_INDEX_MAX, sizeof(char*),
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!words) { ESP_LOGW(TAG, "index alloc failed; saving will scan the file"); return false; }
    FILE* f = fopen(VOCAB_PATH, "r");
    if (!f) { // 还没有生词本：留一个空索引，避免每次都去开文件
        s_words = words;
        s_count = 0;
        return true;
    }
    char line[VOCAB_LINE_MAX];
    char lower[VOCAB_WORD_MAX];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        char* tab = strchr(line, '\t');
        if (tab) *tab = 0;              // 只留第一段：词头 / keep the word field only
        lower_copy(lower, sizeof(lower), line);
        if (!lower[0] || s_count >= VOCAB_INDEX_MAX) continue;
        words[s_count] = psram_strdup(lower);
        if (!words[s_count]) break;
        ++s_count;
    }
    fclose(f);
    for (size_t i = 0; i < s_count; ++i)
        if (!words[i]) { // 中途分配失败：退掉半成品，交回给扫文件模式
            for (size_t k = 0; k < s_count; ++k) heap_caps_free(words[k]);
            heap_caps_free(words);
            ESP_LOGW(TAG, "index build failed; saving will scan the file");
            return false;
        }
    qsort(words, s_count, sizeof(words[0]), cmp_word);
    s_words = words;
    return true;
}

// 索引不可用时的兜底：逐行扫一遍。只在保存/移除（用户点击）时走。
// Fallback when the index is unavailable: scan line by line. Only used from
// save/remove, i.e. on an explicit tap.
static bool file_contains(const char* lower) {
    FILE* f = fopen(VOCAB_PATH, "r");
    if (!f) return false;
    char line[VOCAB_LINE_MAX];
    char word[VOCAB_WORD_MAX];
    bool found = false;
    while (!found && fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        char* tab = strchr(line, '\t');
        if (tab) *tab = 0;
        lower_copy(word, sizeof(word), line);
        found = word[0] && !strcmp(word, lower);
    }
    fclose(f);
    return found;
}

// s_undo 的第一列（词头）是否与 lower 相同。/ Does the stashed row's word field match?
static bool undo_matches(const char* lower) {
    if (!s_undo[0]) return false;
    char head[VOCAB_WORD_MAX];
    size_t n = strcspn(s_undo, "\t");
    if (n >= sizeof(head)) n = sizeof(head) - 1;
    memcpy(head, s_undo, n);
    head[n] = 0;
    lower_copy(head, sizeof(head), head);
    return head[0] && !strcmp(head, lower);
}

// 把释义列拆成音标与正文：build_dict.py 只在音标非空时把 "[音标]" 作为第一行输出，
// 而 summary_text() 用 " / " 把前三行接起来，所以列首是 '[' 就一定能拆出音标。
// Split the definition column into phonetic and body: build_dict.py only emits a
// "[phonetic]" first line when one exists, and summary_text() joined the first
// three lines with " / ", so a leading '[' reliably marks it.
static void split_phonetic(const char* def, char* phon, size_t phon_cap, const char** body) {
    phon[0] = 0;
    *body = def ? def : "";
    if (!def || def[0] != '[') return;
    const char* close = strchr(def, ']');
    if (!close) return;
    size_t n = (size_t)(close - def) + 1;
    if (n >= phon_cap) return;
    memcpy(phon, def, n);
    phon[n] = 0;
    const char* rest = close + 1;
    while (*rest == ' ') ++rest;
    if (!strncmp(rest, "/ ", 2)) rest += 2; // summary_text() 的连接符 / summary_text()'s joiner
    *body = rest;
}

/* ---------- 对外接口 ---------- */

void vocab_log_capture(const char* book_path, const char* text, size_t start, size_t end) {
    // 在生词本视图里长按查词时，抓到的「书名」是视图文件、「原句」是视图里的一行——
    // 那是垃圾上下文。宁可清空也不覆盖：留着上一次真书的上下文同样会写错行。
    // A long press inside the vocabulary view would capture the view file as the
    // book and one of its own rows as the sentence. Clear instead of keeping the
    // previous real book's context: a stale one would write just as wrong a row.
    if (vocab_log_is_view_path(book_path)) {
        s_sentence[0] = 0;
        return;
    }
    s_book[0] = 0;
    if (book_path) snprintf(s_book, sizeof(s_book), "%s", book_path);
    if (text) sentence_capture(text, strlen(text), start, end);
    else s_sentence[0] = 0;
}

bool vocab_log_contains(const char* word) {
    char lower[VOCAB_WORD_MAX];
    if (!word) return false;
    lower_copy(lower, sizeof(lower), word);
    if (!lower[0]) return false;
    if (!s_words) return false; // 不读卡：宁可显示「保存」也不拖慢查词
    size_t pos = 0;
    return index_find(lower, &pos);
}

bool vocab_log_save(const char* word, const char* definition) {
    char lower[VOCAB_WORD_MAX];
    if (!word) return false;
    lower_copy(lower, sizeof(lower), word);
    if (!lower[0]) return false;
    if (!dir_ready()) { ESP_LOGW(TAG, "no vocab dir"); return false; }

    struct stat st;
    bool exists = stat(VOCAB_PATH, &st) == 0;
    if (exists && (uint64_t)st.st_size >= VOCAB_FILE_MAX) {
        ESP_LOGW(TAG, "vocab.txt reached %u bytes, refusing to append", (unsigned)VOCAB_FILE_MAX);
        return false;
    }
    // 刚被删掉的那一行：原样写回，等于撤销。这比重新抓上下文可靠——
    // 在生词本视图里长按查词时抓到的上下文本身就是垃圾。
    // The row that was just removed: write it back verbatim, i.e. undo. More
    // reliable than recapturing, since a long press inside the vocabulary view
    // would only ever capture garbage.
    if (undo_matches(lower)) {
        FILE* undo_file = fopen(VOCAB_PATH, "a");
        bool ok = false;
        if (undo_file) {
            bool head_ok = (exists && st.st_size > 0) ||
                           fprintf(undo_file, "%s\n", VOCAB_HEADER) >= 0;
            ok = head_ok && fprintf(undo_file, "%s\n", s_undo) >= 0;
            if (fclose(undo_file) != 0) ok = false;
        }
        if (!ok) { ESP_LOGW(TAG, "undo write failed"); return false; }
        s_undo[0] = 0;
        index_add(lower);
        return true;
    }
    // 索引没建起来就地扫一遍去重——这是用户点击才走的路，慢点没关系。
    // Without an index, scan for duplicates right here; this only runs on a tap.
    if (!s_words && file_contains(lower)) { index_add(lower); return true; }
    if (s_words) { size_t pos = 0; if (index_find(lower, &pos)) return true; }

    FILE* f = fopen(VOCAB_PATH, "a");
    if (!f) { ESP_LOGW(TAG, "open failed: %s", strerror(errno)); return false; }
    char when[32];
    char title[sizeof(s_book)];
    if (!book_title_get(s_book, title, sizeof(title)) &&
        !book_title_from_path(s_book, title, sizeof(title)))
        snprintf(title, sizeof(title), "%s", s_book[0] ? s_book : "-");
    char book[160], def[288];
    field_escape(book, sizeof(book), title);
    field_escape(def, sizeof(def), definition ? definition : "");
    bool ok = true;
    if (!exists || st.st_size == 0) ok = fprintf(f, "%s\n", VOCAB_HEADER) >= 0;
    if (ok) ok = fprintf(f, "%s\t%s\t%s\t%s\t%s\n", lower, stamp(when, sizeof(when)),
                         book[0] ? book : "-", s_sentence[0] ? s_sentence : "-",
                         def[0] ? def : "-") >= 0;
    if (fclose(f) != 0) ok = false;
    if (!ok) { ESP_LOGW(TAG, "write failed"); return false; }
    index_add(lower);
    return true;
}

bool vocab_log_remove(const char* word) {
    char lower[VOCAB_WORD_MAX];
    if (!word) return false;
    lower_copy(lower, sizeof(lower), word);
    if (!lower[0]) return false;
    if (!dir_ready()) return false;
    struct stat st;
    if (stat(VOCAB_PATH, &st) != 0) return false;
    s_undo[0] = 0;

    FILE* in = fopen(VOCAB_PATH, "r");
    if (!in) return false;
    FILE* out = fopen(VOCAB_TMP, "w");
    if (!out) { fclose(in); return false; }
    char line[VOCAB_LINE_MAX];
    char head[VOCAB_WORD_MAX];
    bool removed = false;
    bool ok = true;
    while (fgets(line, sizeof(line), in)) {
        if (line[0] == '#') { if (fputs(line, out) < 0) { ok = false; break; } continue; }
        char* tab = strchr(line, '\t');
        size_t n = tab ? (size_t)(tab - line) : strlen(line);
        if (n >= sizeof(head)) n = sizeof(head) - 1;
        memcpy(head, line, n);
        head[n] = 0;
        lower_copy(head, sizeof(head), head);
        if (head[0] && !strcmp(head, lower)) {
            removed = true;
            size_t raw = strlen(line);
            while (raw && (line[raw - 1] == '\n' || line[raw - 1] == '\r')) --raw;
            if (raw < sizeof(s_undo)) { memcpy(s_undo, line, raw); s_undo[raw] = 0; }
            continue;
        }
        if (fputs(line, out) < 0) { ok = false; break; }
    }
    fclose(in);
    if (fclose(out) != 0) ok = false;
    // 任何一步失败都清掉临时文件，绝不留下半份生词本。
    // Any failure discards the temp file so no half vocabulary book survives.
    if (!ok) {
        remove(VOCAB_TMP);
        s_undo[0] = 0;
        ESP_LOGW(TAG, "remove failed, original kept");
        return false;
    }
    if (remove(VOCAB_PATH) != 0 || rename(VOCAB_TMP, VOCAB_PATH) != 0) {
        remove(VOCAB_TMP);
        ESP_LOGW(TAG, "replace failed: %s", strerror(errno));
        return false;
    }
    if (s_words) { size_t pos = 0; if (index_find(lower, &pos)) index_drop_at(pos); }
    return removed;
}

// 判据三原则（改这个函数前请先读一遍）：
//  1. 比**内容**不比指针。绝不能写成 `path == VOCAB_VIEW` —— 那比的是地址，
//     只有恰好拿到同一个字符串常量时才成立，换个来源的路径就整个失效，
//     而且编译期不会有任何警告。
//  2. 只比**文件名**不比整条路径：视图被拷到别处（比如拷进 /sdcard/books
//     后再读）照样认得出来，不会被当成普通书去抓上下文。
//  3. 精确匹配，不可以用 strstr 之类的子串包含：`my_vocab_view.txt`、
//     `vocab_view.txt.bak` 都含这个名字但不是视图。误判的代价很实在——
//     app_book.c 的 DICT 分支一旦把真书认成视图，删一个生词就会重开虚拟书，
//     把用户正在读的那本顶掉。
// 大小写不敏感是因为 FAT 本身不区分大小写：`Vocab_View.txt` 打开的就是同一个文件。
// Matching rules:
//  1. Compare contents, never pointers: `path == VOCAB_VIEW` compares addresses and
//     silently breaks for any path that did not come from that one literal.
//  2. Compare the file name, not the whole path: a copy of the view opened from
//     elsewhere (say /sdcard/books) is still recognized.
//  3. Exact match, no substring test like strstr: "my_vocab_view.txt" contains the
//     name but is not the view, and a false positive makes app_book.c's DICT branch
//     reopen the vocabulary book over whatever the user was reading.
// Case-insensitive because FAT is case-insensitive: "Vocab_View.txt" is the same file.
static bool name_is_view(const char* path) {
    if (!path || !path[0]) return false;
    const char* base = strrchr(path, '/');
    base = base ? base + 1 : path;
    const char* view = strrchr(VOCAB_VIEW, '/');
    view = view ? view + 1 : VOCAB_VIEW;
    return !strcasecmp(base, view);
}

bool vocab_log_is_view_path(const char* path) { return name_is_view(path); }
const char* vocab_log_view_path(void) { return VOCAB_VIEW; }

bool vocab_log_render(size_t* out_count) {
    if (out_count) *out_count = 0;
    // 还没有生词本：写一本只带提示的空书，让入口有东西可打开。
    // No book yet: write a hint-only view so the entry still has something to open.
    FILE* in = fopen(VOCAB_PATH, "r");
    if (!in) {
        FILE* empty_out = fopen(VOCAB_VIEW_TMP, "w");
        if (!empty_out) return false;
        bool ok = fprintf(empty_out, "# 我的生词本（共 0 词）\n\n"
                                     "还没有保存任何生词。\n"
                                     "读英文书时长按一个单词，在释义卡右上角点「保存」。\n") >= 0 &&
                  fclose(empty_out) == 0;
        if (!ok) { remove(VOCAB_VIEW_TMP); return false; }
        if (remove(VOCAB_VIEW) != 0 && errno != ENOENT) { remove(VOCAB_VIEW_TMP); return false; }
        return rename(VOCAB_VIEW_TMP, VOCAB_VIEW) == 0;
    }

    // 第一遍只数行，好在标题里写出总数。/ First pass counts rows for the header.
    char line[VOCAB_LINE_MAX];
    size_t total = 0;
    while (fgets(line, sizeof(line), in)) if (line[0] != '#') ++total;
    rewind(in);

    FILE* out = fopen(VOCAB_VIEW_TMP, "w");
    if (!out) { fclose(in); return false; }
    bool ok = fprintf(out, "# 我的生词本（共 %u 词）\n\n", (unsigned)total) >= 0;
    size_t index = 0, chapter = 0;
    while (ok && fgets(line, sizeof(line), in)) {
        if (line[0] == '#') continue;
        char* nl = strpbrk(line, "\r\n");
        if (nl) *nl = 0;
        char* field[5] = {0};
        char* p = line;
        int tabs = 0;
        for (int i = 0; i < 5; ++i) {
            field[i] = p;
            char* tab = strchr(p, '\t');
            if (!tab) break;
            *tab = 0;
            p = tab + 1;
            ++tabs;
        }
        // 没有 TAB 就不是词条（坏行/手工编辑残留），跳过而不是渲染成怪条目。
        // A row with no tab is not an entry (corruption or hand editing); skip it
        // instead of rendering something odd.
        if (!tabs || !field[0] || !field[0][0]) continue;
        ++index;
        // 章节标题必须写成「第N章」（中间无空格），book_txt.c 的 heading() 才认。
        // The heading must read "第N章" with no space or book_txt.c's heading() misses it.
        if ((index - 1) % VOCAB_CHUNK == 0 && fprintf(out, "第%u章\n\n", (unsigned)++chapter) < 0) {
            ok = false;
            break;
        }
        char phon[160];
        const char* body = NULL;
        split_phonetic(field[4] ? field[4] : "-", phon, sizeof(phon), &body);
        const char* when = field[1] && field[1][0] ? field[1] : "-";
        const char* book = field[2] && field[2][0] ? field[2] : "-";
        const char* sentence = field[3] && field[3][0] ? field[3] : NULL;
        if (fprintf(out, "%u. %s\n", (unsigned)index, field[0]) < 0) { ok = false; break; }
        if (phon[0]) {
            if (fprintf(out, "   %s %s\n", phon, body) < 0) { ok = false; break; }
        } else if (fprintf(out, "   %s\n", body) < 0) { ok = false; break; }
        if (sentence && strcmp(sentence, "-") &&
            fprintf(out, "   原句：%s\n", sentence) < 0) { ok = false; break; }
        if (fprintf(out, "   [保存时间：%s · %s]\n\n", when, book) < 0) { ok = false; break; }
    }
    fclose(in);
    if (fclose(out) != 0) ok = false;
    if (!ok) { remove(VOCAB_VIEW_TMP); return false; }
    if (remove(VOCAB_VIEW) != 0 && errno != ENOENT) { remove(VOCAB_VIEW_TMP); return false; }
    if (rename(VOCAB_VIEW_TMP, VOCAB_VIEW) != 0) { remove(VOCAB_VIEW_TMP); return false; }
    if (out_count) *out_count = index;
    return true;
}

void vocab_log_warm(void) {
    if (s_words) return;
    (void)index_load();
}

void vocab_log_invalidate(void) {
    if (!s_words) { s_count = 0; return; }
    for (size_t i = 0; i < s_count; ++i) heap_caps_free(s_words[i]);
    heap_caps_free(s_words);
    s_words = NULL;
    s_count = 0;
    s_book[0] = 0;
    s_sentence[0] = 0;
    s_undo[0] = 0; // 卡都没了，撤销缓冲指向的行也一起没了 / the card is gone, so is the stashed row
}
