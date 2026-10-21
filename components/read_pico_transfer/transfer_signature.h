/* SPDX-License-Identifier: Apache-2.0
 * 中文：状态栏签名输入边界；留空可清除，拒绝无效 UTF-8 与控制字符。
 * English: Signature bounds; empty clears it, invalid UTF-8 and control characters are rejected.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
static inline bool transfer_signature_valid(const char *text, size_t size) {
    if (!text || size > 95) return false;
    for (size_t i = 0; i < size;) {
        uint32_t cp = (unsigned char)text[i++], minimum = 0; unsigned next = 0;
        if (cp >= 0xc2 && cp <= 0xdf) { cp &= 31; next = 1; minimum = 128; }
        else if (cp >= 0xe0 && cp <= 0xef) { cp &= 15; next = 2; minimum = 2048; }
        else if (cp >= 0xf0 && cp <= 0xf4) { cp &= 7; next = 3; minimum = 65536; }
        else if (cp >= 128) return false;
        if (size - i < next) return false;
        while (next--) { unsigned char c = (unsigned char)text[i++]; if ((c & 192) != 128) return false; cp = (cp << 6) | (c & 63); }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) || cp < 32 || cp == 127 || cp == 0x2028 || cp == 0x2029) return false;
    }
    return true;
}
