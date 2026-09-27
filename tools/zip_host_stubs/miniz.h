/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 仅为主机测试把单次 raw inflate 适配到 zlib。
 * Adapt one-shot raw inflate to zlib for host tests only.
 * 冻结：不替换 ZIP 解析，不用于验证 ROM 实现。/ Frozen: Do not replace ZIP parsing or claim ROM validation.
 */
#pragma once
#include <stddef.h>
#include <string.h>
#include <zlib.h>
typedef unsigned char mz_uint8;
typedef unsigned int mz_uint32;
typedef struct { int m_state; } tinfl_decompressor;
typedef enum { TINFL_STATUS_FAILED = -1, TINFL_STATUS_DONE = 0 } tinfl_status;
#define TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF 4
#define tinfl_init(r) ((r)->m_state = 0)
static inline tinfl_status tinfl_decompress(tinfl_decompressor* state, const mz_uint8* input,
    size_t* in_size, mz_uint8* start, mz_uint8* output, size_t* out_size, mz_uint32 flags) {
    (void)state; (void)start; (void)flags;
    z_stream stream;
    memset(&stream, 0, sizeof(stream));
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) return TINFL_STATUS_FAILED;
    unsigned char empty;
    stream.next_in = (Bytef*)input;
    stream.avail_in = (uInt)*in_size;
    stream.next_out = *out_size ? output : &empty;
    stream.avail_out = *out_size ? (uInt)*out_size : 1;
    int result = inflate(&stream, Z_FINISH);
    *in_size = stream.total_in; *out_size = stream.total_out;
    inflateEnd(&stream);
    return result == Z_STREAM_END ? TINFL_STATUS_DONE : TINFL_STATUS_FAILED;
}
