/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：push_proto.h 的实现。字节序显式小端，不用 struct  packing——跨编译器与
 * 宿主/目标一致，ASan 宿主测试逐字节对表。
 * English: implementation of push_proto.h. Explicit little-endian fields and no
 * struct packing: identical across compilers and between host and target, so the
 * ASan host test compares byte for byte.
 *
 * 冻结：同 push_proto.h。/ Frozen: same contract as push_proto.h.
 */

#include "push_proto.h"

#include <stdbool.h>
#include <string.h>

/* ---- CRC32 ---- */

static uint32_t s_crc_table[256];
static bool s_crc_ready;

static void crc_table_build(void) {
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        s_crc_table[i] = c;
    }
    s_crc_ready = true;
}

uint32_t push_crc32(const void *data, size_t len) { return push_crc32_continue(0u, data, len); }

uint32_t push_crc32_continue(uint32_t crc, const void *data, size_t len) {
    if (!s_crc_ready) crc_table_build();
    // 传入的是"已出盒"值，先退回内部态再喂字节，出口再封箱。
    // The incoming value is already finalised; undo that, feed, then seal again.
    uint32_t c = crc ^ 0xFFFFFFFFu;
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; ++i) c = s_crc_table[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

/* ---- 小端助手 / little-endian helpers ---- */

static void put_u16(uint8_t *out, uint16_t v) {
    out[0] = (uint8_t)(v & 0xFFu);
    out[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void put_u32(uint8_t *out, uint32_t v) {
    out[0] = (uint8_t)(v & 0xFFu);
    out[1] = (uint8_t)((v >> 8) & 0xFFu);
    out[2] = (uint8_t)((v >> 16) & 0xFFu);
    out[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint16_t get_u16(const uint8_t *in) {
    return (uint16_t)((uint16_t)in[0] | ((uint16_t)in[1] << 8));
}

static uint32_t get_u32(const uint8_t *in) {
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
}

/* ---- BEGIN / BEGIN codec ---- */

size_t push_begin_encode(const push_begin_t *begin, uint8_t *out, size_t cap) {
    if (!begin || !out || cap < PUSH_BEGIN_LEN) return 0;
    memset(out, 0, PUSH_BEGIN_LEN);
    out[0] = PUSH_MAGIC0;
    out[1] = PUSH_MAGIC1;
    out[2] = PUSH_OP_BEGIN;
    out[3] = PUSH_VERSION;
    out[4] = begin->type;
    put_u16(out + 5, begin->token);
    put_u32(out + 7, begin->total_len);
    put_u32(out + 11, begin->crc32);
    put_u32(out + 15, begin->session_id);
    // 标题截断保 0：调用方给的 title 必须已以 0 结尾，这里只防越界。
    // Truncate with NUL: callers pass NUL-terminated titles; this only guards the copy.
    size_t n = strnlen(begin->title, PUSH_TITLE_MAX);
    if (n >= PUSH_TITLE_MAX) n = PUSH_TITLE_MAX - 1;
    memcpy(out + 19, begin->title, n);
    return PUSH_BEGIN_LEN;
}

bool push_begin_decode(const uint8_t *in, size_t len, push_begin_t *begin) {
    if (!in || !begin || len < PUSH_BEGIN_LEN) return false;
    if (in[0] != PUSH_MAGIC0 || in[1] != PUSH_MAGIC1 || in[2] != PUSH_OP_BEGIN) return false;
    if (in[3] != PUSH_VERSION) return false;
    memset(begin, 0, sizeof(*begin));
    begin->type = in[4];
    begin->token = get_u16(in + 5);
    begin->total_len = get_u32(in + 7);
    begin->crc32 = get_u32(in + 11);
    begin->session_id = get_u32(in + 15);
    memcpy(begin->title, in + 19, PUSH_TITLE_MAX);
    begin->title[PUSH_TITLE_MAX - 1] = 0;
    return true;
}

/* ---- DATA / DATA codec ---- */

bool push_data_decode(const uint8_t *in, size_t len, uint16_t *seq, const uint8_t **payload, size_t *payload_len) {
    if (!in || len < PUSH_DATA_HDR_LEN) return false;
    const size_t n = (size_t)in[2];
    if (len < PUSH_DATA_HDR_LEN + n) return false;
    if (seq) *seq = get_u16(in);
    if (payload) *payload = in + PUSH_DATA_HDR_LEN;
    if (payload_len) *payload_len = n;
    return true;
}

/* ---- CTRL 帧头 / CTRL header ---- */

bool push_ctrl_header_decode(const uint8_t *in, size_t len, uint8_t *op) {
    if (!in || len < 4u) return false;
    if (in[0] != PUSH_MAGIC0 || in[1] != PUSH_MAGIC1) return false;
    if (in[3] != PUSH_VERSION) return false;
    if (op) *op = in[2];
    return true;
}

/* ---- STATUS / STATUS codec ---- */

size_t push_status_encode(uint8_t opcode, const uint8_t *body, size_t body_len, uint8_t *out, size_t cap) {
    if (!out || body_len > 12u) return 0;
    if (cap < 1u + body_len) return 0;
    out[0] = opcode;
    if (body_len) memcpy(out + 1, body, body_len);
    return 1u + body_len;
}
