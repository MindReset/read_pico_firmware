/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：TF 卡 RPD1 离线词典的极简查询接口；无 UI 依赖，可在任意任务调用。
 * English: Minimal query API for the on-card RPD1 dictionary; no UI dependencies.
 *
 * 冻结：查询为同步阻塞调用，调用方负责缓冲容量。
 * Frozen: Lookups block; callers own the output buffer.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/// 单词缓冲与释义缓冲建议大小。/ Suggested word and definition buffer sizes.
#define DICT_WORD_MAX 64
#define DICT_ENTRY_MAX 1280

/// 扫描 /sdcard/dict/*.bin 并打开第一个合法词典；成功后 dict_ready() 为真。
/// Scan /sdcard/dict/*.bin and open the first valid dictionary.
bool dict_open(void);
/// 关闭后重新扫描（TF 重新挂载时调用）。/ Close and rescan after a remount.
bool dict_rescan(void);
/// 预热：未打开时尝试打开；失败不设置退避，不影响后续真正查词。
/// Warm-up: open if not already open; a failure does not arm the back-off.
bool dict_warm(void);
/// 词典是否可用。/ Whether a dictionary is open.
bool dict_ready(void);
/// 释放句柄与稀疏索引。/ Release the file handle and sparse index.
void dict_close(void);

/// 查询单词（大小写不敏感）；命中时把 UTF-8 释义（可含 '\n' 行）写入 out 并返回 true。
/// 词形未命中时按还原规则重查，命中时释义首行为 "[原形]"。
/// Look up a word; on hit writes UTF-8 definition lines into out and returns true.
/// Misses retry reduced word forms; a reduced hit starts with a "[base]" line.
bool dict_lookup_word(const char* word, char* out, size_t cap);

#ifdef __cplusplus
}
#endif
