/* 生词本的宿主替身 / Host stand-ins for the vocabulary log.
 *
 * 抽取式 harness 只把 app_book.c 里的单个函数抠出来编译，而 on_enter / book_on_exit /
 * book_on_media_lost 会调用生词本。这些测试关心的是书架、进度和手势，不关心 TF 卡落盘，
 * 所以这里给一套空实现把它们顶掉——既不引入对 real vocab_log.c 的链接依赖，
 * 也不会因为 PSRAM/文件系统的差异让宿主测试失真。
 *
 * The extraction harnesses compile single functions lifted out of app_book.c, and
 * on_enter / book_on_exit / book_on_media_lost call into the vocabulary log. Those
 * tests are about the shelf, progress and gestures — not about writing to the card —
 * so these no-ops stand in. That keeps the harness free of a link dependency on the
 * real vocab_log.c and keeps host results independent of PSRAM / filesystem quirks.
 *
 * 用 static inline 而不是 static：GCC 的 -Wunused-function 不会报未使用的 static inline，
 * 这样即使某个 harness 只用到其中一个符号也不会被 -Werror 拦下。
 * static inline (not plain static): GCC's -Wunused-function exempts static inline,
 * so a harness that references only some of these still builds under -Werror.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TOOLS_VOCAB_LOG_STUBS_H
#define TOOLS_VOCAB_LOG_STUBS_H

#include <stdbool.h>
#include <stddef.h>

#define VOCAB_WORD_MAX 64

static inline void vocab_log_capture(const char* book_path, const char* text,
                                     size_t start, size_t end) {
    (void)book_path; (void)text; (void)start; (void)end;
}
static inline bool vocab_log_contains(const char* word) { (void)word; return false; }
static inline bool vocab_log_save(const char* word, const char* definition) {
    (void)word; (void)definition; return true;
}
static inline bool vocab_log_remove(const char* word) { (void)word; return true; }
static inline bool vocab_log_render(size_t* out_count) {
    if (out_count) *out_count = 0;
    return true;
}
static inline const char* vocab_log_view_path(void) { return "/sdcard/.readpico/vocab_view.txt"; }
static inline bool vocab_log_is_view_path(const char* path) { (void)path; return false; }
static inline void vocab_log_warm(void) {}
static inline void vocab_log_invalidate(void) {}

#endif /* TOOLS_VOCAB_LOG_STUBS_H */
