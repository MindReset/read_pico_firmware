/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：dict_lookup 的宿主替身。词典可用性与查询结果都由 test_* 旋钮决定，
 * 武装页的单词卡分支据此走「查得到」与「查不到」两条路。
 * English: host shim for dict_lookup. Availability and lookup outcomes come from
 * test_* knobs so the arm page's word-card branch can walk both the hit and the
 * miss paths.
 * 冻结：仅供测试。/ Frozen: tests only.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>

#define DICT_WORD_MAX 64
#define DICT_ENTRY_MAX 1280

static bool test_dict_ready_flag;
static inline bool dict_ready(void) { return test_dict_ready_flag; }
static inline bool dict_lookup_word(const char *word, char *out, size_t cap) {
    (void)word;
    if (out && cap) out[0] = 0;
    return false;
}
