/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 离线书名匹配；不读取文件或修改输入。
 * Offline filename matching without file access or input mutation.
 * 冻结：逐字多音候选，不做词语分词；查询最多64字节，书名最多255字节。
 * Frozen: per-character polyphonic alternatives, no word segmentation; query <=64 bytes, filename <=255 bytes.
 */
#pragma once
#include <stdbool.h>

/// 中文原文、英文不区分大小写子串、全拼或中英首字母匹配；忽略 TXT/EPUB 扩展名。
/// Match Chinese text, ASCII-insensitive substrings, full pinyin or Chinese/English initials; ignore TXT/EPUB extensions.
/// 拼音查询可空格分隔，v表示ü；空查询匹配全部。无效UTF-8、NULL或超长输入返回false。
/// Pinyin queries may contain separating spaces; v represents ü. Empty queries match all; invalid UTF-8, NULL or oversized input returns false.
bool read_pico_search_match(const char* filename, const char* query);
