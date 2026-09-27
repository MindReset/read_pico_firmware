/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：书源编码和章节解析，不依赖界面。
 * English: Source encoding and chapter parsing without UI dependencies.
 * 冻结：保留原文件偏移，无效编码替换，不写文件。
 * Frozen: Preserve original byte offsets, replace invalid encoding, never write files.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
/// 双字节查询，无效返回 U+FFFD。/ Look up a pair; invalid input returns U+FFFD.
uint32_t gbk_codepoint(unsigned char a, unsigned char b);
/// 返回输出字节数；cap 含 NUL，按字符截断。/ Return bytes written; cap includes NUL, truncate at character boundaries.
size_t gbk_to_utf8(const char *src, size_t n, char *dst, size_t cap);
