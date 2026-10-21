/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：手机 BLE 推流的会话状态机。纯 C11：不碰文件、不碰 NVS、不碰 NimBLE，
 * 宿主测试驱动全部转移边。胶水（push_sink.c）按返回的动作位掩码执行副作用。
 * English: the session state machine for phone BLE push. Pure C11: no files, no
 * NVS, no NimBLE. The host test drives every transition; the glue in push_sink.c
 * executes the side effects named by the returned action bits.
 *
 * 冻结：状态集与转移边以设计草案 §4 为准；新增状态先改文档再落码。
 * Frozen: the state set and transitions follow design draft section 4; document first.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "push_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

/// 会话状态。/ Session states.
typedef enum {
    PUSH_STATE_IDLE = 0,    ///< 未武装 / not armed
    PUSH_STATE_ARMED,       ///< 已点接收，等广播起来 / receive pressed, waiting for advertising
    PUSH_STATE_WINDOW,      ///< 窗口期：广播中，等手机连接 / window open: advertising, waiting for a phone
    PUSH_STATE_CONNECTED,   ///< 手机已连接，未开始传输 / phone connected, no transfer yet
    PUSH_STATE_RECEIVING,   ///< BEGIN 已接受，正文流式中 / BEGIN accepted, body streaming
    PUSH_STATE_PROCESSING,  ///< END 已到，等 CRC 对账 / END arrived, awaiting the CRC verdict
    PUSH_STATE_COOLDOWN,    ///< 推送成功后的续推宽限 / post-push grace for a follow-up push
} push_state_t;

/// 状态机事件。/ State machine events.
typedef enum {
    PUSH_EVT_ARM = 0,  ///< 用户点「接收推送」/ the user pressed receive
    PUSH_EVT_ADV_UP,   ///< 广播已启动 / advertising is up
    PUSH_EVT_CONNECT,  ///< 手机连上 / a phone connected
    PUSH_EVT_DISCONNECT, ///< 链路断开 / the link dropped
    PUSH_EVT_BEGIN,    ///< 收到 BEGIN / BEGIN arrived
    PUSH_EVT_DATA,     ///< 收到一包正文 / one body packet arrived
    PUSH_EVT_END,      ///< 收到 END / END arrived
    PUSH_EVT_RESUME_QUERY, ///< 手机问断点 / the phone asks for the resume point
    PUSH_EVT_VERIFY,   ///< 胶水的 CRC 对账结果 / the glue's CRC verdict
    PUSH_EVT_CANCEL,   ///< 用户取消 / the user cancelled
    PUSH_EVT_UNPLUG,   ///< 充电中武装时拔电 / unplugged while armed on charger
    PUSH_EVT_TICK,     ///< 周期事件，arg 为 now_ms / periodic event, arg is now_ms
} push_evt_t;

/// 动作位掩码：胶水执行，状态机不碰外设。/ Action bits: the glue executes them; the machine never touches hardware.
enum {
    PUSH_ACT_ADV_ON = 1u << 0,    ///< 开始广播 / start advertising
    PUSH_ACT_ADV_OFF = 1u << 1,   ///< 停止广播 / stop advertising
    PUSH_ACT_READY = 1u << 2,     ///< 回 ST_READY / reply ST_READY
    PUSH_ACT_RESUME = 1u << 3,    ///< 回 ST_RESUME（offset+next_seq）/ reply ST_RESUME
    PUSH_ACT_APPEND = 1u << 4,    ///< 把刚收的包写进 .part / append the packet to .part
    PUSH_ACT_VERIFY = 1u << 5,    ///< 对 .part 算 CRC32 / CRC32 the .part file
    PUSH_ACT_COMMIT = 1u << 6,    ///< .part → push.txt，清会话，发开书事件 / commit and post the open event
    PUSH_ACT_DROP = 1u << 7,      ///< 丢弃 .part 与会话 / drop the .part file and session
    PUSH_ACT_ERR = 1u << 8,       ///< 回 ST_ERR，码在 last_err / reply ST_ERR, code in last_err
    PUSH_ACT_PROGRESS = 1u << 9, ///< 回 ST_PROGRESS / reply ST_PROGRESS
    PUSH_ACT_OPEN_BOOK = 1u << 10, ///< UI 任务开书（commit 后）/ the UI task opens the book
    PUSH_ACT_WORD_CARD = 1u << 11, ///< type=单词卡：UI 出词典卡 / word card: the UI shows the dictionary card
};

/// 会话簿记。/ Session bookkeeping.
typedef struct {
    push_state_t state;      ///< 当前状态 / current state
    push_begin_t begin;      ///< 当前会话参数 / current session parameters
    uint32_t received;       ///< 已收字节数，等于 .part 长度 / bytes received, equals the .part length
    uint16_t next_seq;       ///< 期望的下一包序号 / expected next packet seq
    uint32_t window_ms;      ///< 武装窗口时长；0 = 充电常开无超时 / window length; 0 means charger-held
    uint32_t armed_at_ms;    ///< 武装时刻 / arm timestamp
    uint32_t cooldown_ms;    ///< 续推宽限 / post-push grace
    uint32_t cooldown_at_ms; ///< 宽限起点 / grace start
    uint16_t token;          ///< 广播名后缀，BEGIN 必须匹配 / adv-name suffix; BEGIN must match
    bool has_part;           ///< .part 是否已建立（有未完成会话）/ a partial file exists
    int last_err;            ///< 最近一次拒绝的错误码 / last rejection code
} push_session_t;

/// 事件参数：BEGIN 与 DATA 带载荷，其余忽略。/ Event payload: BEGIN and DATA carry data; others ignore it.
typedef struct {
    const push_begin_t *begin; ///< PUSH_EVT_BEGIN / PUSH_EVT_BEGIN
    const uint8_t *data;       ///< PUSH_EVT_DATA 的载荷 / PUSH_EVT_DATA payload
    size_t data_len;           ///< PUSH_EVT_DATA 的载荷长度 / payload length
    uint16_t seq;              ///< PUSH_EVT_DATA 的包序号 / packet seq
    uint16_t token;            ///< PUSH_EVT_ARM 的广播名后缀 / adv-name suffix for PUSH_EVT_ARM
    uint32_t window_ms;        ///< PUSH_EVT_ARM 的窗口时长，0 = 充电常开 / window length, 0 = charger-held
    uint32_t cooldown_ms;      ///< PUSH_EVT_ARM 的续推宽限时长 / post-push grace length for PUSH_EVT_ARM
    bool verify_ok;            ///< PUSH_EVT_VERIFY 的结论 / PUSH_EVT_VERIFY verdict
    uint32_t now_ms;           ///< PUSH_EVT_TICK 的当前时刻 / current time for PUSH_EVT_TICK
} push_event_t;

/// 初始化到 IDLE。/ Initialise to IDLE.
void push_session_init(push_session_t *s);

/// 喂一个事件，返回动作位掩码（PUSH_ACT_*）。非法转移不改状态。
/// Feed one event and return the action bits (PUSH_ACT_*); illegal transitions change nothing.
uint32_t push_session_event(push_session_t *s, push_evt_t evt, const push_event_t *arg);

/// 断点续传：把已恢复的会话簿记装回（胶水从 NVS 重建后调用）。
/// Resume: load recovered bookkeeping back in (the glue calls this after rebuilding from NVS).
void push_session_resume(push_session_t *s, const push_begin_t *begin, uint32_t received, uint16_t next_seq);

/// 预装会话簿记但**不动状态**：胶水在用户重新武装时（IDLE 态）从 NVS/.part 恢复
/// 半成品会话用。装完之后照常喂 ARM→ADV_UP→CONNECT，手机一问 RESUME_QUERY 或
/// 重发同名 BEGIN，就能拿到 offset 续传。received 为 0 时等于没装（has_part 假）。
/// Preload recovered bookkeeping without touching the state: the glue uses this
/// when the user re-arms (state IDLE) to restore a partial session from NVS/.part.
/// Feed ARM→ADV_UP→CONNECT as usual afterwards; the phone then learns the offset
/// from RESUME_QUERY or a repeated BEGIN. received == 0 installs nothing.
void push_session_adopt(push_session_t *s, const push_begin_t *begin, uint32_t received, uint16_t next_seq);

/// 窗口剩余毫秒；0 表示无超时或已不在窗口。/ Milliseconds left in the window; 0 means no timeout or not in a window.
uint32_t push_session_window_left_ms(const push_session_t *s, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
