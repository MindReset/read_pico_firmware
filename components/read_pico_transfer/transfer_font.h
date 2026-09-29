/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：上传 TTF 的结构预检；提交前检查轮廓、字距和目录边界。
 * English: Preflight uploaded TTF structure, outline, metrics and directory bounds before commit.
 * 冻结：只读有界检查；不解码字形，不接受集合或 CFF 字体。
 * Frozen: Bounded read-only checks; no glyph decoding, collections or CFF fonts.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TRANSFER_FONT_MAX (32U * 1024U * 1024U)

static uint16_t font_u16(const unsigned char *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t font_u32(const unsigned char *p) { return (uint32_t)font_u16(p) << 16 | font_u16(p + 2); }
static bool font_read(FILE *f, uint32_t offset, void *out, size_t size) {
    return fseek(f, (long)offset, SEEK_SET) == 0 && fread(out, 1, size, f) == size;
}

static bool valid_ttf(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    bool valid = false;
    unsigned char header[12];
    if (fseek(f, 0, SEEK_END)) goto done;
    long length = ftell(f);
    if (length < 12 || (unsigned long)length > TRANSFER_FONT_MAX || !font_read(f, 0, header, sizeof(header))) goto done;
    if (font_u32(header) != 0x00010000 && memcmp(header, "true", 4)) goto done;
    unsigned count = font_u16(header + 4);
    if (!count || count > 128 || 12U + count * 16U > (unsigned long)length) goto done;
    const char *required[] = {"cmap", "head", "hhea", "hmtx", "maxp", "loca", "glyf"};
    uint32_t offsets[7] = {0}, sizes[7] = {0};
    for (unsigned i = 0; i < count; ++i) {
        unsigned char record[16];
        if (!font_read(f, 12 + i * 16, record, sizeof(record))) goto done;
        uint32_t offset = font_u32(record + 8), size = font_u32(record + 12);
        if (offset < 12U + count * 16U || offset > (uint32_t)length || size > (uint32_t)length - offset) goto done;
        for (unsigned k = 0; k < 7; ++k) {
            if (memcmp(record, required[k], 4)) continue;
            if (offsets[k]) goto done;
            offsets[k] = offset; sizes[k] = size;
        }
    }
    for (unsigned k = 0; k < 7; ++k) if (!offsets[k] || !sizes[k]) goto done;
    unsigned char head[54], hhea[36], maxp[6], cmap[4];
    if (sizes[1] < sizeof(head) || sizes[2] < sizeof(hhea) || sizes[4] < sizeof(maxp) || sizes[0] < sizeof(cmap) ||
        !font_read(f, offsets[1], head, sizeof(head)) || !font_read(f, offsets[2], hhea, sizeof(hhea)) ||
        !font_read(f, offsets[4], maxp, sizeof(maxp)) || !font_read(f, offsets[0], cmap, sizeof(cmap))) goto done;
    unsigned glyphs = font_u16(maxp + 4), metrics = font_u16(hhea + 34), units = font_u16(head + 18);
    unsigned loca = font_u16(head + 50), maps = font_u16(cmap + 2);
    if (font_u32(head + 12) != 0x5f0f3cf5 || units < 16 || units > 16384 || !glyphs || !metrics || metrics > glyphs ||
        loca > 1 || sizes[5] < (glyphs + 1U) * (loca ? 4U : 2U) || sizes[3] < metrics * 4U + (glyphs - metrics) * 2U ||
        font_u16(cmap) || !maps || 4U + maps * 8U > sizes[0]) goto done;
    uint32_t previous = 0;
    unsigned width = loca ? 4 : 2;
    for (unsigned i = 0; i <= glyphs;) {
        unsigned char locations[512];
        unsigned n = glyphs + 1 - i;
        if (n > sizeof(locations) / width) n = sizeof(locations) / width;
        if (!font_read(f, offsets[5] + i * width, locations, n * width)) goto done;
        for (unsigned k = 0; k < n; ++k) {
            const unsigned char *location = locations + k * width;
            uint32_t offset = loca ? font_u32(location) : font_u16(location) * 2U;
            if (offset < previous || offset > sizes[6]) goto done;
            previous = offset;
        }
        i += n;
    }
    valid = true;
done:
    if (fclose(f)) valid = false;
    return valid;
}
