/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：手机 BLE 推流的帧格式与 CRC32。纯 C11，不依赖 ESP-IDF——字节序全部显式
 * 小端序列化，宿主与目标逐字段一致，tools/push_sink_host_test.c 直接对表。
 * English: frame format and CRC32 for phone BLE push. Pure C11 with no ESP-IDF
 * dependency: every field is explicitly little-endian so the host test in
 * tools/push_sink_host_test.c compares byte for byte.
 *
 * 冻结：帧布局一旦发布不得单方面改字段；加字段只能扩尾部保留区。
 * Frozen: never change field layout unilaterally once shipped; extend the tail only.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 常量 / Constants ---- */

#define PUSH_MAGIC0 0x50u ///< 'P' / 'P'
#define PUSH_MAGIC1 0x43u ///< 'C' / 'C'
#define PUSH_VERSION 1u   ///< 帧格式版本 / frame format version

#define PUSH_TITLE_MAX 64u  ///< 标题 UTF-8 上限（含结尾 0）/ title cap including NUL
#define PUSH_DATA_PAYLOAD_MAX 241u ///< MTU 247 时每包载荷：3 写属性头 + 3 帧头 / payload at MTU 247
#define PUSH_TOTAL_MAX (512u * 1024u) ///< 正文硬上限；超了 ERR(3) / hard body cap, ERR(3) beyond

/// CTRL 操作码。/ CTRL opcodes.
///
/// ---- 协议变更记录（每新增/改动 opcode 必须登记；BEGIN 帧布局已冻结） ----
/// Protocol change log (every opcode addition/change lands here; the BEGIN layout is frozen).
///   gen14  0x01..0x04  BEGIN/END/RESUME_QUERY/ABORT（BEGIN 帧 83B 定长，此后冻结）
///   gen22  0x05        SET_TIME：手机下发 UTC 秒（4B LE）校准设备时钟。
///                      · 独立于「必须武装」闸门之外：连上即可校时（有意设计——校时
///                        不携带 token，采用「连上即信任」模型；无 Wi-Fi 场景的唯一
///                        高频校时通道，PMU 无外部晶振、深睡靠内部 RC 走时会漂）。
///                      · sanity：长度 <8B 或 UTC 落在 1704067200..4102444799
///                        （2024-01-01~2100-01-01）之外 → ERR(2)，时钟不动。
///                      · 参考实现：PushProto.encodeSetTime（Java）、
///                        push_pico.py encode_set_time / --set-time（Python）已同步。
///   gen22  0x05        SET_TIME: the phone pushes UTC seconds (4 B LE) to set the clock.
///                      · Deliberately outside the "must be armed" gate - any connection
///                        may set the time (by design; it carries no token, i.e. a
///                        "connected means trusted" model) because it is the only
///                        high-frequency path when there is no WiFi.
///                      · Sanity: len < 8 B or UTC outside 1704067200..4102444799
///                        -> ERR(2) and the clock is untouched.
///                      · Reference implementations kept in sync: PushProto.encodeSetTime
///                        (Java) and push_pico.py encode_set_time / --set-time (Python).
enum {
    PUSH_OP_BEGIN = 0x01,         ///< 开始一次会话 / start a session
    PUSH_OP_END = 0x02,           ///< 正文结束，请求校验 / body done, ask for verification
    PUSH_OP_RESUME_QUERY = 0x03,  ///< 重连后问断点 / ask for the resume point after reconnect
    PUSH_OP_ABORT = 0x04,         ///< 放弃本次会话 / abandon the session
    PUSH_OP_SET_TIME = 0x05,      ///< gen22：手机下发 UTC 秒（4 字节小端）校准设备时钟 /
                                  ///< phone pushes a UTC second count (4 bytes LE) to set the clock
};

/// CTRL 帧头字节数：magic(2) + op(1) + version(1)。/ CTRL header bytes: magic(2)+op(1)+version(1).
#define PUSH_CTRL_HDR_LEN 4u

/// STATUS 操作码（设备→手机）。/ STATUS opcodes (device to phone).
enum {
    PUSH_ST_READY = 0x10,    ///< 会话已接受，可以发数据 / session accepted, send data
    PUSH_ST_RESUME = 0x11,  ///< 断点：从 offset 续、seq 从 next_seq 起 / resume point
    PUSH_ST_DONE = 0x12,    ///< 校验通过，已开书 / verified and opened
    PUSH_ST_ERR = 0x13,     ///< 拒绝，body 是错误码 / rejected, body is the error code
    PUSH_ST_EVT_READ = 0x14, ///< 用户在设备上标记已读 / the user marked it read on the device
    PUSH_ST_BATT = 0x15,    ///< 电量百分比 / battery percent
    PUSH_ST_PROGRESS = 0x16, ///< 接收进度 / receive progress
};

/// 正文类型。/ Body types.
enum {
    PUSH_TYPE_TEXT = 0,  ///< 纯文本 / plain text
    PUSH_TYPE_MARKDOWN = 1, ///< Markdown / Markdown
    PUSH_TYPE_HTML = 2,   ///< HTML / HTML
    PUSH_TYPE_URL = 3,    ///< 单个 URL，M1.5 抓取 / one URL, fetch in M1.5
    PUSH_TYPE_WORD = 4,   ///< 单词卡：title 即单词 / word card: title is the word
};

/// 错误码（STATUS ERR 的 body）。/ Error codes carried in STATUS ERR bodies.
enum {
    PUSH_ERR_NONE = 0,
    PUSH_ERR_NOT_ARMED = 1,     ///< 不在武装窗口 / not inside an armed window
    PUSH_ERR_BAD_FRAME = 2,     ///< 魔数/版本/长度非法 / bad magic, version or length
    PUSH_ERR_TOO_LARGE = 3,     ///< total_len 超上限 / total_len over the cap
    PUSH_ERR_CHECKSUM = 4,      ///< CRC32 不符 / CRC32 mismatch
    PUSH_ERR_NO_CARD = 5,       ///< TF 卡不可用 / card unavailable
    PUSH_ERR_LOW_BATTERY = 6,   ///< 电量不足且未充电 / low battery, not charging
    PUSH_ERR_BUSY = 7,          ///< 已有一次推送在处理 / a push is already in flight
    PUSH_ERR_FETCH = 8,         ///< URL 抓取失败（M1.5）/ URL fetch failed (M1.5)
    PUSH_ERR_NO_DICT = 9,       ///< 单词卡但设备无词典 / word card but no dictionary
};

/// BEGIN 帧（83 字节，全部小端）。/ BEGIN frame (83 bytes, all little-endian).
typedef struct {
    uint32_t total_len; ///< 正文字节数；URL/单词卡为 0 / body bytes; 0 for URL and word
    uint32_t crc32;     ///< 正文 CRC32；URL/单词卡为 0 / body CRC32; 0 for URL and word
    uint32_t session_id; ///< 会话标识，断点续传对账 / session id for resume matching
    uint16_t token;     ///< 广播名后缀，须与窗口期广播名一致 / adv-name suffix, must match the window
    uint8_t type;       ///< PUSH_TYPE_* / PUSH_TYPE_*
    char title[PUSH_TITLE_MAX]; ///< UTF-8，截断保 0 / UTF-8, truncated with NUL
} push_begin_t;

#define PUSH_BEGIN_LEN 83u ///< BEGIN 帧字节数 / BEGIN frame size
#define PUSH_DATA_HDR_LEN 3u ///< DATA 帧头：seq(2)+len(1) / DATA frame header

/* ---- CRC32 ---- */

/// IEEE 反射式 CRC32（多项式 0xEDB88320），与 zlib 同款。/ Reflected IEEE CRC32 (poly 0xEDB88320), same as zlib.
uint32_t push_crc32(const void *data, size_t len);

/// 流式 CRC32：crc 传上一段的返回值（首段传 0），可分段喂 512KB 级正文而不必
/// 整块驻留内存。链式调用等价于对拼接后的一次运算：continue(continue(0,a),b)
/// 等于 continue(0, a||b)，也等于 push_crc32(a||b)。空段是合法的直通。
/// Streaming CRC32: pass the previous return value (0 for the first chunk) so a
/// 512 KB body can be fed in pieces without holding it all in RAM. Chaining is
/// associative: continue(continue(0,a),b) == continue(0, a||b) == push_crc32(a||b);
/// an empty chunk is a legal pass-through.
uint32_t push_crc32_continue(uint32_t crc, const void *data, size_t len);

/* ---- BEGIN 编解码 / BEGIN codec ---- */

/// 序列化 BEGIN 到 out（至少 PUSH_BEGIN_LEN 字节），返回写入字节数。
/// Serialise a BEGIN into out (PUSH_BEGIN_LEN bytes); returns bytes written.
size_t push_begin_encode(const push_begin_t *begin, uint8_t *out, size_t cap);

/// 反序列化 len 字节到 begin。坏魔数/坏版本/长度不足返回 false。
/// Deserialise len bytes into begin; false on bad magic, bad version or short buffer.
bool push_begin_decode(const uint8_t *in, size_t len, push_begin_t *begin);

/* ---- DATA 帧头 / DATA frame header ---- */

/// 取出 DATA 帧的 seq 与载荷长度；帧非法返回 false。
/// Read seq and payload length from a DATA frame; false when the frame is malformed.
bool push_data_decode(const uint8_t *in, size_t len, uint16_t *seq, const uint8_t **payload, size_t *payload_len);

/* ---- CTRL 帧头 / CTRL frame header ---- */

/// 校验 CTRL 帧公共头（魔数 + 版本 + 长度 ≥4）并取出 opcode。END / RESUME_QUERY /
/// ABORT 无载荷，走这一路；BEGIN 另有 push_begin_decode 解析全文。
/// Validate the shared CTRL header (magic + version + length >= 4) and extract the
/// opcode. END / RESUME_QUERY / ABORT carry no payload and take this path; BEGIN
/// is parsed in full by push_begin_decode instead.
bool push_ctrl_header_decode(const uint8_t *in, size_t len, uint8_t *op);

/* ---- STATUS 编解码 / STATUS codec ---- */

#define PUSH_STATUS_MAX 16u ///< STATUS 帧上限 / STATUS frame cap

/// 编码一条 STATUS：opcode + 至多 12 字节 body，返回帧长（0 表示 cap 不够）。
/// Encode one STATUS: opcode plus up to 12 body bytes; returns frame length (0 when out of cap).
size_t push_status_encode(uint8_t opcode, const uint8_t *body, size_t body_len, uint8_t *out, size_t cap);

#ifdef __cplusplus
}
#endif
