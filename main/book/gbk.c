/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：书源编码和章节解析，不依赖界面。
 * English: Source encoding and chapter parsing without UI dependencies.
 * 冻结：保留原文件偏移，无效编码替换，不写文件。
 * Frozen: Preserve original byte offsets, replace invalid encoding, never write files.
 */
#include "gbk.h"
#include "gbk_table.h"
uint32_t gbk_codepoint(unsigned char a, unsigned char b) {
    if (a < 0x81 || a > 0xfe || b < 0x40 || b > 0xfe || b == 0x7f) return 0xfffd;
    return s_gbk_table[(a - 0x81) * 190 + b - 0x40 - (b > 0x7f)];
}
size_t gbk_to_utf8(const char *src, size_t n, char *dst, size_t cap) {
    size_t out = 0;
    if (!dst || !cap) return 0;
    for (size_t i = 0; src && i < n;) {
        unsigned char a = (unsigned char)src[i++]; uint32_t cp = a;
        if (a >= 0x80) {
            cp = 0xfffd;
            if (a >= 0x81 && a <= 0xfe && i < n) {
                unsigned char b = (unsigned char)src[i];
                if (b >= 0x40 && b <= 0xfe && b != 0x7f) { cp = gbk_codepoint(a, b); ++i; }
            }
        }
        size_t bytes = cp < 0x80 ? 1 : cp < 0x800 ? 2 : 3;
        if (bytes >= cap - out) break;
        if (bytes == 1) dst[out++] = (char)cp;
        else if (bytes == 2) { dst[out++] = 0xc0 | (cp >> 6); dst[out++] = 0x80 | (cp & 63); }
        else { dst[out++] = 0xe0 | (cp >> 12); dst[out++] = 0x80 | ((cp >> 6) & 63); dst[out++] = 0x80 | (cp & 63); }
    }
    dst[out] = 0; return out;
}
