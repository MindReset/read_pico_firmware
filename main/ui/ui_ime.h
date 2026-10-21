/* SPDX-License-Identifier: Apache-2.0
 * 中文：离线连续拼音、九宫格与词组候选，借用已有单字字表。
 * English: Offline continuous Pinyin, T9 and phrase candidates using the existing character table.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#define UI_IME_RAW_MAX 48
#define UI_IME_CANDIDATES 24
#define UI_IME_TEXT_MAX 96
typedef struct {
    char text[UI_IME_TEXT_MAX], roman[UI_IME_RAW_MAX + 1];
    size_t consume;
} ui_ime_candidate_t;
typedef struct {
    ui_ime_candidate_t items[UI_IME_CANDIDATES];
    size_t count;
    bool has_more; ///< 后面仍有候选 / Further candidates are available
} ui_ime_result_t;
/// 有界组词，不读取网络；结果必须由调用方放 PSRAM。/ Bounded offline composition; caller keeps results in PSRAM.
bool ui_ime_candidates(const char *raw, bool nine, const char *preferred,
                        const char *preceding, ui_ime_result_t *out);
/// 按候选偏移量继续查找，每批只驻留 24 项；不会截断同音字。/ Fetch bounded batches without truncating homophones.
bool ui_ime_candidates_page(const char *raw, bool nine, const char *preferred,
                             const char *preceding, size_t skip, ui_ime_result_t *out);
/// 九宫格字母转数字，不识别非字母。/ Map letters to T9 digits, rejecting other characters.
bool ui_ime_digits(const char *roman, char *out, size_t cap);
/// 单个输入会话的 PSRAM 缓存由主线程独占；退出键盘即释放，不影响可重入书名搜索。
/// Main-thread-only PSRAM cache for one input session; release on keyboard exit without changing reentrant book search.
void ui_ime_release(void);
