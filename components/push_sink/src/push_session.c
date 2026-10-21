/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：push_session.h 的实现。所有时间由调用方经 now_ms 注入，所有副作用由动作
 * 位掩码申领——本文件不读时钟、不写文件、不碰射频，宿主测试可全边覆盖。
 * English: implementation of push_session.h. Time is injected via now_ms and side
 * effects are declared through action bits: this file reads no clock, writes no
 * file and touches no radio, so the host test can cover every edge.
 *
 * 冻结：转移边与设计草案 §4 一致；非法转移保持原状并记 last_err。
 * Frozen: transitions match design draft section 4; illegal ones hold state and set last_err.
 */

#include "push_session.h"

#include <string.h>

void push_session_init(push_session_t *s) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->state = PUSH_STATE_IDLE;
}

// 簿记安装的公共体：resume 与 adopt 共用。/ Shared body of resume and adopt.
static void adopt_locked(push_session_t *s, const push_begin_t *begin, uint32_t received, uint16_t next_seq) {
    s->begin = *begin;
    s->received = received;
    s->next_seq = next_seq;
    s->has_part = received > 0;
}

void push_session_resume(push_session_t *s, const push_begin_t *begin, uint32_t received, uint16_t next_seq) {
    if (!s || !begin) return;
    adopt_locked(s, begin, received, next_seq);
    // 恢复出来的半成品会话按续收中对待：手机重连后会直接接着发或重发 END。
    // A recovered partial counts as mid-receive: the phone continues or replays END.
    s->state = PUSH_STATE_RECEIVING;
}

void push_session_adopt(push_session_t *s, const push_begin_t *begin, uint32_t received, uint16_t next_seq) {
    if (!s) return;
    if (!begin || received == 0) return;  // 没东西可装：IDLE 保持原样 / nothing to install
    adopt_locked(s, begin, received, next_seq);
}

uint32_t push_session_window_left_ms(const push_session_t *s, uint32_t now_ms) {
    if (!s || s->window_ms == 0) return 0;
    const uint32_t elapsed = now_ms - s->armed_at_ms;
    return elapsed >= s->window_ms ? 0u : s->window_ms - elapsed;
}

// 进 IDLE 并停广播的公共尾巴。/ Common tail for entering IDLE with advertising off.
static uint32_t goto_idle(push_session_t *s) {
    s->state = PUSH_STATE_IDLE;
    return PUSH_ACT_ADV_OFF;
}

// 窗口是否已到期（window_ms==0 表示充电常开，永不到期）。
// Whether the window has expired (window_ms == 0 means charger-held, never expires).
static bool window_expired(const push_session_t *s, uint32_t now_ms) {
    return s->window_ms != 0 && (now_ms - s->armed_at_ms) >= s->window_ms;
}

// BEGIN 的会话级校验：token、type、total_len 三项。返回 0 表示通过。
// Session-level BEGIN checks: token, type and total_len. Zero means pass.
static int begin_check(const push_session_t *s, const push_begin_t *b) {
    if (b->token != s->token) return PUSH_ERR_BAD_FRAME;
    if (b->type > PUSH_TYPE_WORD) return PUSH_ERR_BAD_FRAME;
    // URL 与单词卡不带正文，total_len 必须为 0；其余必须在上限内。
    // URL and word cards carry no body; the rest must fit the cap.
    if (b->type == PUSH_TYPE_URL || b->type == PUSH_TYPE_WORD) {
        if (b->total_len != 0) return PUSH_ERR_BAD_FRAME;
    } else if (b->total_len == 0 || b->total_len > PUSH_TOTAL_MAX) {
        return b->total_len == 0 ? PUSH_ERR_BAD_FRAME : PUSH_ERR_TOO_LARGE;
    }
    return PUSH_ERR_NONE;
}

uint32_t push_session_event(push_session_t *s, push_evt_t evt, const push_event_t *arg) {
    if (!s) return 0;
    const uint32_t now_ms = arg ? arg->now_ms : 0;

    switch (s->state) {
    case PUSH_STATE_IDLE:
        if (evt == PUSH_EVT_ARM && arg) {
            s->token = arg->token;
            s->window_ms = arg->window_ms;
            s->armed_at_ms = now_ms;
            s->cooldown_ms = arg->cooldown_ms;
            s->cooldown_at_ms = 0;
            s->state = PUSH_STATE_ARMED;
            return PUSH_ACT_ADV_ON;
        }
        break;

    case PUSH_STATE_ARMED:
        // 广播确认即入窗口；起不来时胶水走 CANCEL。
        // Advertising confirmation opens the window; the glue cancels on failure.
        if (evt == PUSH_EVT_ADV_UP) {
            s->state = PUSH_STATE_WINDOW;
            return 0;
        }
        if (evt == PUSH_EVT_CANCEL) return goto_idle(s);
        break;

    case PUSH_STATE_WINDOW:
        if (evt == PUSH_EVT_CONNECT) {
            s->state = PUSH_STATE_CONNECTED;
            return 0;
        }
        if (evt == PUSH_EVT_CANCEL || evt == PUSH_EVT_UNPLUG) return goto_idle(s);
        if (evt == PUSH_EVT_TICK && window_expired(s, now_ms)) return goto_idle(s);
        break;

    case PUSH_STATE_CONNECTED: {
        if (evt == PUSH_EVT_DISCONNECT) {
            // 断连回窗口等重连；有 .part 时会话留在簿记里供续传。
            // Drop back to the window for a reconnect; a partial file stays bookkept.
            s->state = PUSH_STATE_WINDOW;
            return 0;
        }
        if (evt == PUSH_EVT_CANCEL || evt == PUSH_EVT_UNPLUG) {
            return goto_idle(s) | (s->has_part ? PUSH_ACT_DROP : 0u);
        }
        if (evt == PUSH_EVT_TICK && window_expired(s, now_ms)) return goto_idle(s);
        if (evt == PUSH_EVT_RESUME_QUERY && arg) {
            // 无论有没有 .part 都回 RESUME：offset 0 就是让手机重头发。
            // Always answer RESUME: offset 0 simply tells the phone to start over.
            // 有半成品时必须同时进 RECEIVING——手机拿到 offset 后紧接着就发 DATA，
            // 停在 CONNECTED 会把每一包都当成非法转移悄悄吃掉，手机永远等不到进度。
            // With a partial, RECEIVING is mandatory: the phone follows the offset
            // with DATA immediately, and staying in CONNECTED would swallow every
            // packet as an illegal transition while the phone waits forever.
            if (s->has_part) s->state = PUSH_STATE_RECEIVING;
            return PUSH_ACT_RESUME;
        }
        if (evt == PUSH_EVT_END && s->has_part) {
            // 坏 CRC 之后手机补发缺失包再重发 END：这里放行，让它再走一次对账。
            // After a bad CRC the phone resends missing packets then replays END:
            // allow it here so verification runs again.
            s->state = PUSH_STATE_PROCESSING;
            return PUSH_ACT_VERIFY;
        }
        if (evt == PUSH_EVT_BEGIN && arg && arg->begin) {
            const push_begin_t *b = arg->begin;
            const int err = begin_check(s, b);
            if (err != PUSH_ERR_NONE) {
                s->last_err = err;
                return PUSH_ACT_ERR;
            }
            if (s->has_part && b->session_id == s->begin.session_id) {
                // 同会话重发 BEGIN：不重置计数，直接续传。
                // A repeated BEGIN for the same session resumes without resetting counters.
                s->state = PUSH_STATE_RECEIVING;
                return PUSH_ACT_RESUME;
            }
            // 新会话：旧 .part 作废。/ A new session retires any old partial file.
            const uint32_t drop = s->has_part ? PUSH_ACT_DROP : 0u;
            s->begin = *b;
            s->received = 0;
            s->next_seq = 0;
            s->has_part = true;
            s->state = PUSH_STATE_RECEIVING;
            return drop | PUSH_ACT_READY;
        }
        break;
    }

    case PUSH_STATE_RECEIVING:
        if (evt == PUSH_EVT_DISCONNECT) {
            s->state = PUSH_STATE_WINDOW;
            return 0;
        }
        if (evt == PUSH_EVT_CANCEL) {
            return goto_idle(s) | PUSH_ACT_DROP;
        }
        if (evt == PUSH_EVT_DATA && arg) {
            // 跳号即拒：这包不落盘，手机重发该序号。
            // A seq gap is refused: this packet never lands, the phone resends it.
            if (arg->seq != s->next_seq) {
                s->last_err = PUSH_ERR_BAD_FRAME;
                return PUSH_ACT_ERR;
            }
            if ((uint64_t)s->received + arg->data_len > s->begin.total_len) {
                s->last_err = PUSH_ERR_BAD_FRAME;
                return PUSH_ACT_ERR;
            }
            if ((uint64_t)s->received + arg->data_len > PUSH_TOTAL_MAX) {
                s->last_err = PUSH_ERR_TOO_LARGE;
                return PUSH_ACT_ERR;
            }
            // 记账先于落盘：胶水按 ACT_APPEND 写文件，计数在这里推进，
            // 断点续传的 offset 与 next_seq 才与 .part 长度一致。
            // Count before the write: the glue appends on ACT_APPEND while the
            // counters advance here, so resume offsets match the .part length.
            s->received += (uint32_t)arg->data_len;
            ++s->next_seq;
            return PUSH_ACT_APPEND | PUSH_ACT_PROGRESS;
        }
        if (evt == PUSH_EVT_END) {
            // 长度对账交给 VERIFY：不等就当校验失败，保留 .part 续传。
            // Length accounting happens in VERIFY: a mismatch fails the checksum
            // and keeps the partial file for resume.
            s->state = PUSH_STATE_PROCESSING;
            return PUSH_ACT_VERIFY;
        }
        break;

    case PUSH_STATE_PROCESSING:
        if (evt == PUSH_EVT_DISCONNECT) {
            s->state = PUSH_STATE_WINDOW;
            return 0;
        }
        if (evt == PUSH_EVT_VERIFY && arg) {
            if (!arg->verify_ok || s->received != s->begin.total_len) {
                s->last_err = PUSH_ERR_CHECKSUM;
                s->state = PUSH_STATE_CONNECTED;
                return PUSH_ACT_ERR;
            }
            // 成功：进宽限，提交与开书由胶水/UI 执行。
            // Success: enter the grace window; the glue commits and the UI opens.
            s->cooldown_at_ms = now_ms;
            s->state = PUSH_STATE_COOLDOWN;
            uint32_t act = PUSH_ACT_COMMIT | PUSH_ACT_OPEN_BOOK;
            if (s->begin.type == PUSH_TYPE_WORD) act = PUSH_ACT_COMMIT | PUSH_ACT_WORD_CARD;
            return act;
        }
        break;

    case PUSH_STATE_COOLDOWN:
        if (evt == PUSH_EVT_DISCONNECT) {
            // 推完手机就走：回窗口继续广播，下一次推送（含重发 BEGIN）还能来。
            // The phone left right after the push: return to the window so the
            // next push (a replayed BEGIN included) can still arrive.
            s->state = PUSH_STATE_WINDOW;
            return 0;
        }
        if (evt == PUSH_EVT_TICK) {
            // 宽限先到期回窗口，窗口也到期才收摊。
            // Grace expiring returns to the window; only an expired window closes up.
            if (s->cooldown_ms != 0 && (now_ms - s->cooldown_at_ms) >= s->cooldown_ms) {
                if (window_expired(s, now_ms)) return goto_idle(s);
                // 常开会话没有窗口可回：宽限一到就收摊。这正是「传输中拔电」
                // 的收官路径——UNPLUG 在 RECEIVING/PROCESSING 不打断（传输
                // 优先），推完进宽限，这里负责把摊收了。
                // A held session has no window to return to: grace expiring
                // closes it. This is the wrap-up path for an unplug that happened
                // mid-transfer - UNPLUG never interrupts RECEIVING/PROCESSING
                // (the transfer wins), and this settles the score after it.
                if (s->window_ms == 0) return goto_idle(s);
                s->state = PUSH_STATE_WINDOW;
                return 0;
            }
            if (window_expired(s, now_ms)) return goto_idle(s);
            return 0;
        }
        if (evt == PUSH_EVT_CONNECT) {
            s->state = PUSH_STATE_CONNECTED;
            return 0;
        }
        if (evt == PUSH_EVT_BEGIN && arg && arg->begin) {
            const int err = begin_check(s, arg->begin);
            if (err != PUSH_ERR_NONE) {
                s->last_err = err;
                return PUSH_ACT_ERR;
            }
            s->begin = *arg->begin;
            s->received = 0;
            s->next_seq = 0;
            s->has_part = true;
            s->state = PUSH_STATE_RECEIVING;
            return PUSH_ACT_READY;
        }
        if (evt == PUSH_EVT_CANCEL || evt == PUSH_EVT_UNPLUG) return goto_idle(s);
        break;
    }

    // 其余组合：非法转移，保持原状。/ Everything else: illegal, hold state.
    return 0;
}
