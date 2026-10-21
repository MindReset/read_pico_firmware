/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：push_sink 纯核（帧编解码 + 会话状态机）的宿主回归。断言有牙：坏魔数、
 * 坏 CRC、seq 跳号、total_len 超限、未武装写入、token 不符、拔电退出全部必须红。
 * English: host regression for the push_sink pure core (frame codec + session
 * machine). The assertions bite: bad magic, bad CRC, seq gaps, oversized
 * total_len, unarmed writes, token mismatch and unplug must all be refused.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "push_proto.h"
#include "push_session.h"

static int s_checks;
static int s_red;

#define CHECK(cond)                                                      \
    do {                                                                 \
        ++s_checks;                                                      \
        if (!(cond)) {                                                   \
            ++s_red;                                                     \
            printf("RED line %d: %s\n", __LINE__, #cond);                \
        }                                                                \
    } while (0)

static push_session_t s_sess;
static push_event_t s_evt;

// 喂事件的小助手：清零参数结构只填需要的字段。/ Event helper: clear the struct, fill only what is needed.
static uint32_t feed(push_evt_t evt) {
    memset(&s_evt, 0, sizeof(s_evt));
    return push_session_event(&s_sess, evt, &s_evt);
}

static uint32_t feed_arm(uint16_t token, uint32_t window_ms, uint32_t cooldown_ms, uint32_t now_ms) {
    memset(&s_evt, 0, sizeof(s_evt));
    s_evt.token = token;
    s_evt.window_ms = window_ms;
    s_evt.cooldown_ms = cooldown_ms;
    s_evt.now_ms = now_ms;
    return push_session_event(&s_sess, PUSH_EVT_ARM, &s_evt);
}

static uint32_t feed_tick(uint32_t now_ms) {
    memset(&s_evt, 0, sizeof(s_evt));
    s_evt.now_ms = now_ms;
    return push_session_event(&s_sess, PUSH_EVT_TICK, &s_evt);
}

static uint32_t feed_begin(const push_begin_t *b) {
    memset(&s_evt, 0, sizeof(s_evt));
    s_evt.begin = b;
    return push_session_event(&s_sess, PUSH_EVT_BEGIN, &s_evt);
}

static uint32_t feed_data(uint16_t seq, size_t len) {
    static const uint8_t payload[PUSH_DATA_PAYLOAD_MAX];
    memset(&s_evt, 0, sizeof(s_evt));
    s_evt.data = payload;
    s_evt.data_len = len;
    s_evt.seq = seq;
    return push_session_event(&s_sess, PUSH_EVT_DATA, &s_evt);
}

static uint32_t feed_verify(bool ok, uint32_t now_ms) {
    memset(&s_evt, 0, sizeof(s_evt));
    s_evt.verify_ok = ok;
    s_evt.now_ms = now_ms;
    return push_session_event(&s_sess, PUSH_EVT_VERIFY, &s_evt);
}

static push_begin_t make_begin(uint32_t total, uint16_t token) {
    push_begin_t b;
    memset(&b, 0, sizeof(b));
    b.total_len = total;
    b.crc32 = 0xdeadbeefu;
    b.session_id = 0x12345678u;
    b.token = token;
    b.type = PUSH_TYPE_TEXT;
    snprintf(b.title, sizeof(b.title), "测试长文");
    return b;
}

/* ---- 编解码 / codec ---- */

static void test_crc32(void) {
    // zlib 标准向量。/ The standard zlib vector.
    CHECK(push_crc32("123456789", 9) == 0xCBF43926u);
    CHECK(push_crc32("", 0) == 0u);
    const char *hi = "墨水屏推流";
    CHECK(push_crc32(hi, strlen(hi)) == push_crc32(hi, strlen(hi)));
    CHECK(push_crc32("a", 1) != push_crc32("b", 1));

    // 流式续算：分段喂与整块一次算必须逐位相同（512KB 级正文靠它进 ATT 缓冲）。
    // Streaming: chunked feeding must match the one-shot result bit for bit (a
    // 512 KB body relies on this to stay out of one contiguous buffer).
    static const char chunk_a[] = "ReadPico push stream ";
    static const char chunk_b[] = "serendipity 墨水屏";
    static const char chunk_c[] = "";
    const size_t la = sizeof(chunk_a) - 1, lb = sizeof(chunk_b) - 1;
    uint8_t whole[256];
    memcpy(whole, chunk_a, la);
    memcpy(whole + la, chunk_b, lb);
    const uint32_t one = push_crc32(whole, la + lb);
    const uint32_t chained = push_crc32_continue(push_crc32_continue(push_crc32_continue(0u, chunk_a, la), chunk_b, lb), chunk_c, 0);
    CHECK(chained == one);
    // 空段直通、首段传 0 等价一次性。/ Empty chunks pass through; a leading 0 equals the one-shot.
    CHECK(push_crc32_continue(0u, whole, la + lb) == one);
    CHECK(push_crc32_continue(0u, chunk_c, 0) == 0u);
    CHECK(push_crc32_continue(one, chunk_c, 0) == one);
    printf("crc32: vectors + streaming chain ok\n");
}

static void test_ctrl_header(void) {
    // END / RESUME_QUERY / ABORT 的公共头：魔数+版本+长度四项。
    // The shared header of END / RESUME_QUERY / ABORT: magic, version, length.
    const uint8_t ok_end[4] = {0x50, 0x43, PUSH_OP_END, PUSH_VERSION};
    const uint8_t ok_resume[4] = {0x50, 0x43, PUSH_OP_RESUME_QUERY, PUSH_VERSION};
    const uint8_t ok_abort[4] = {0x50, 0x43, PUSH_OP_ABORT, PUSH_VERSION};
    uint8_t op = 0xFF;
    CHECK(push_ctrl_header_decode(ok_end, sizeof(ok_end), &op) && op == PUSH_OP_END);
    CHECK(push_ctrl_header_decode(ok_resume, sizeof(ok_resume), &op) && op == PUSH_OP_RESUME_QUERY);
    CHECK(push_ctrl_header_decode(ok_abort, sizeof(ok_abort), &op) && op == PUSH_OP_ABORT);
    // 有牙：短帧/坏魔数/坏版本全拒。/ Bite: short, bad magic and bad version all refuse.
    CHECK(!push_ctrl_header_decode(ok_end, 3, &op));
    uint8_t bad[4];
    memcpy(bad, ok_end, 4); bad[0] = 0x51;
    CHECK(!push_ctrl_header_decode(bad, 4, &op));
    memcpy(bad, ok_end, 4); bad[1] = 0x44;
    CHECK(!push_ctrl_header_decode(bad, 4, &op));
    memcpy(bad, ok_end, 4); bad[3] = PUSH_VERSION + 1;
    CHECK(!push_ctrl_header_decode(bad, 4, &op));
    printf("ctrl header: END/RESUME/ABORT decode, bad frames refuse ok\n");
}

static void test_begin_codec(void) {
    push_begin_t b = make_begin(300000, 4821);
    uint8_t frame[PUSH_BEGIN_LEN + 8];
    memset(frame, 0xAA, sizeof(frame));
    const size_t n = push_begin_encode(&b, frame, sizeof(frame));
    CHECK(n == PUSH_BEGIN_LEN);
    // 字节级布局：魔数/op/ver/type/token/total/crc/session/title 起点。
    // Byte-level layout: magic, op, ver, type, token, total, crc, session, title.
    CHECK(frame[0] == 0x50 && frame[1] == 0x43);
    CHECK(frame[2] == PUSH_OP_BEGIN);
    CHECK(frame[3] == PUSH_VERSION);
    CHECK(frame[4] == PUSH_TYPE_TEXT);
    CHECK(frame[5] == 0xD5 && frame[6] == 0x12); // token 4821 = 0x12D5 LE
    CHECK(frame[7] == 0xE0 && frame[8] == 0x93 && frame[9] == 0x04); // 300000 = 0x000493E0 LE
    CHECK(memcmp(frame + 19, "测试长文", strlen("测试长文") + 1) == 0);

    push_begin_t out;
    CHECK(push_begin_decode(frame, n, &out));
    CHECK(out.total_len == 300000 && out.crc32 == 0xdeadbeefu && out.session_id == 0x12345678u);
    CHECK(out.token == 4821 && out.type == PUSH_TYPE_TEXT);
    CHECK(strcmp(out.title, "测试长文") == 0);

    // 有牙：短缓冲/坏魔数/坏 op/坏版本全部拒收。
    // Bite: short buffer, bad magic, bad op and bad version are all refused.
    CHECK(!push_begin_decode(frame, PUSH_BEGIN_LEN - 1, &out));
    uint8_t bad[PUSH_BEGIN_LEN];
    memcpy(bad, frame, PUSH_BEGIN_LEN);
    bad[0] = 0x51;
    CHECK(!push_begin_decode(bad, PUSH_BEGIN_LEN, &out));
    memcpy(bad, frame, PUSH_BEGIN_LEN);
    bad[2] = PUSH_OP_END;
    CHECK(!push_begin_decode(bad, PUSH_BEGIN_LEN, &out));
    memcpy(bad, frame, PUSH_BEGIN_LEN);
    bad[3] = 9;
    CHECK(!push_begin_decode(bad, PUSH_BEGIN_LEN, &out));
    CHECK(!push_begin_encode(&b, frame, PUSH_BEGIN_LEN - 1));
    printf("begin codec: round-trip + byte layout + refusals ok\n");
}

static void test_data_status_codec(void) {
    uint8_t frame[8] = {0x07, 0x00, 0x03, 'a', 'b', 'c', 0, 0};
    uint16_t seq = 0;
    const uint8_t *payload = NULL;
    size_t len = 0;
    CHECK(push_data_decode(frame, sizeof(frame), &seq, &payload, &len));
    CHECK(seq == 7 && len == 3 && payload == frame + 3);
    CHECK(payload[0] == 'a' && payload[2] == 'c');
    // len 字段超过缓冲：拒。/ A len field beyond the buffer is refused.
    CHECK(!push_data_decode(frame, 5, &seq, &payload, &len));
    CHECK(!push_data_decode(frame, 2, &seq, &payload, &len));

    uint8_t out[PUSH_STATUS_MAX];
    CHECK(push_status_encode(PUSH_ST_READY, NULL, 0, out, sizeof(out)) == 1 && out[0] == PUSH_ST_READY);
    const uint8_t body[4] = {1, 2, 3, 4};
    CHECK(push_status_encode(PUSH_ST_ERR, body, 4, out, sizeof(out)) == 5);
    CHECK(out[0] == PUSH_ST_ERR && out[4] == 4);
    CHECK(push_status_encode(PUSH_ST_ERR, body, 13, out, sizeof(out)) == 0); // body 超长 / body too long
    CHECK(push_status_encode(PUSH_ST_ERR, body, 4, out, 4) == 0);            // cap 不够 / cap too small
    printf("data/status codec ok\n");
}

/* ---- 状态机 / state machine ---- */

static void test_happy_path(void) {
    push_session_init(&s_sess);
    CHECK(feed_arm(4821, 1800000, 60000, 1000) == PUSH_ACT_ADV_ON);
    CHECK(s_sess.state == PUSH_STATE_ARMED);
    CHECK(feed(PUSH_EVT_ADV_UP) == 0);
    CHECK(s_sess.state == PUSH_STATE_WINDOW);
    CHECK(feed(PUSH_EVT_CONNECT) == 0);
    CHECK(s_sess.state == PUSH_STATE_CONNECTED);

    push_begin_t b = make_begin(600, 4821);
    CHECK(feed_begin(&b) == PUSH_ACT_READY);
    CHECK(s_sess.state == PUSH_STATE_RECEIVING);
    CHECK(s_sess.received == 0 && s_sess.next_seq == 0);

    CHECK(feed_data(0, 241) == (PUSH_ACT_APPEND | PUSH_ACT_PROGRESS));
    CHECK(s_sess.received == 241 && s_sess.next_seq == 1);
    CHECK(feed_data(1, 241) == (PUSH_ACT_APPEND | PUSH_ACT_PROGRESS));
    CHECK(feed_data(2, 118) == (PUSH_ACT_APPEND | PUSH_ACT_PROGRESS));
    CHECK(s_sess.received == 600);

    CHECK(feed(PUSH_EVT_END) == PUSH_ACT_VERIFY);
    CHECK(s_sess.state == PUSH_STATE_PROCESSING);
    CHECK(feed_verify(true, 5000) == (PUSH_ACT_COMMIT | PUSH_ACT_OPEN_BOOK));
    CHECK(s_sess.state == PUSH_STATE_COOLDOWN);
    // 宽限时长随 ARM 注入（不再由外部手改簿记）。
    // The grace length rides in with ARM (no more poking the bookkeeping by hand).
    CHECK(s_sess.cooldown_ms == 60000 && s_sess.cooldown_at_ms == 5000);

    // 宽限里手机断连：回窗口继续广播，簿记保留可再推。
    // A disconnect during grace: back to the window, bookkeeping survives.
    CHECK(feed(PUSH_EVT_DISCONNECT) == 0);
    CHECK(s_sess.state == PUSH_STATE_WINDOW && s_sess.received == 600 && s_sess.has_part);

    // 宽限到期回窗口；窗口到期收摊。/ Grace expiring returns to the window; an expired window closes.
    CHECK(feed_tick(5000 + 60000) == 0);
    CHECK(s_sess.state == PUSH_STATE_WINDOW);
    CHECK(feed_tick(1000 + 1800000) == PUSH_ACT_ADV_OFF);
    CHECK(s_sess.state == PUSH_STATE_IDLE);
    printf("happy path: IDLE->ARMED->WINDOW->CONNECTED->RECEIVING->PROCESSING->COOLDOWN->WINDOW->IDLE ok\n");
}

static void test_word_card(void) {
    push_session_init(&s_sess);
    feed_arm(777, 1800000, 0, 0);
    feed(PUSH_EVT_ADV_UP);
    feed(PUSH_EVT_CONNECT);
    push_begin_t b;
    memset(&b, 0, sizeof(b));
    b.type = PUSH_TYPE_WORD;
    b.token = 777;
    b.session_id = 55;
    snprintf(b.title, sizeof(b.title), "serendipity");
    CHECK(feed_begin(&b) == PUSH_ACT_READY);
    CHECK(s_sess.state == PUSH_STATE_RECEIVING);
    // 单词卡 total_len 必须为 0：非 0 即拒。/ A word card must carry no body.
    push_begin_t bad = b;
    bad.total_len = 10;
    push_session_init(&s_sess);
    feed_arm(777, 1800000, 0, 0);
    feed(PUSH_EVT_ADV_UP);
    feed(PUSH_EVT_CONNECT);
    CHECK(feed_begin(&bad) == PUSH_ACT_ERR);
    CHECK(s_sess.last_err == PUSH_ERR_BAD_FRAME);

    push_session_init(&s_sess);
    feed_arm(777, 1800000, 0, 0);
    feed(PUSH_EVT_ADV_UP);
    feed(PUSH_EVT_CONNECT);
    CHECK(feed_begin(&b) == PUSH_ACT_READY);
    feed(PUSH_EVT_END);
    CHECK(feed_verify(true, 100) == (PUSH_ACT_COMMIT | PUSH_ACT_WORD_CARD));
    printf("word card: ERR on stray body, COMMIT|WORD_CARD on success ok\n");
}

static void test_resume(void) {
    push_session_init(&s_sess);
    feed_arm(4821, 1800000, 0, 0);
    feed(PUSH_EVT_ADV_UP);
    feed(PUSH_EVT_CONNECT);
    push_begin_t b = make_begin(600, 4821);
    feed_begin(&b);
    feed_data(0, 241);
    feed_data(1, 241);
    CHECK(s_sess.received == 482);

    // 断连回窗口，会话簿记保留。/ Disconnect returns to the window; bookkeeping survives.
    CHECK(feed(PUSH_EVT_DISCONNECT) == 0);
    CHECK(s_sess.state == PUSH_STATE_WINDOW);
    CHECK(s_sess.received == 482 && s_sess.has_part);

    // 重连后问断点：RESUME 且计数不动，同时进 RECEIVING 好让续发的 DATA 进门。
    // Reconnect and ask: RESUME with counters intact, and into RECEIVING so the
    // DATA that follows the offset is accepted.
    CHECK(feed(PUSH_EVT_CONNECT) == 0);
    CHECK(feed(PUSH_EVT_RESUME_QUERY) == PUSH_ACT_RESUME);
    CHECK(s_sess.received == 482);
    CHECK(s_sess.state == PUSH_STATE_RECEIVING);
    CHECK(feed_data(2, 118) == (PUSH_ACT_APPEND | PUSH_ACT_PROGRESS));
    CHECK(s_sess.received == 600 && s_sess.next_seq == 3);

    // 同会话重发 BEGIN 要从 CONNECTED 进：断一次再连，BEGIN 不重置、直接续。
    // The same-session BEGIN path is reached from CONNECTED: drop and reconnect,
    // and the repeated BEGIN resumes without resetting the counters.
    CHECK(feed(PUSH_EVT_DISCONNECT) == 0);
    CHECK(s_sess.state == PUSH_STATE_WINDOW && s_sess.received == 600);
    CHECK(feed(PUSH_EVT_CONNECT) == 0);
    CHECK(feed_begin(&b) == PUSH_ACT_RESUME);
    CHECK(s_sess.state == PUSH_STATE_RECEIVING);
    CHECK(s_sess.received == 600 && s_sess.next_seq == 3);

    // 跨重启恢复：簿记装回后接着收。/ Across a reboot: reload bookkeeping and continue.
    push_session_t fresh;
    push_session_init(&fresh);
    push_session_resume(&fresh, &b, 482, 2);
    CHECK(fresh.received == 482 && fresh.next_seq == 2 && fresh.has_part);
    memset(&s_evt, 0, sizeof(s_evt));
    s_evt.data = (const uint8_t *)"x";
    s_evt.data_len = 118;
    s_evt.seq = 2;
    push_session_t *keep = &s_sess;
    s_sess = fresh;
    CHECK(push_session_event(&s_sess, PUSH_EVT_DATA, &s_evt) == (PUSH_ACT_APPEND | PUSH_ACT_PROGRESS));
    CHECK(s_sess.received == 600);
    s_sess = *keep;
    printf("resume: disconnect/reconnect/RESUME/same-session BEGIN/cross-reboot reload ok\n");
}

static void test_adopt(void) {
    // adopt：预装簿记但不动状态。/ adopt: preloads bookkeeping and leaves the state alone.
    push_session_init(&s_sess);
    push_begin_t b = make_begin(600, 4821);
    push_session_adopt(&s_sess, &b, 482, 2);
    CHECK(s_sess.state == PUSH_STATE_IDLE);   // IDLE 原样不动 / IDLE untouched
    CHECK(s_sess.received == 482 && s_sess.next_seq == 2 && s_sess.has_part);

    // received 为 0 等于没装。/ received == 0 installs nothing.
    push_session_init(&s_sess);
    push_session_adopt(&s_sess, &b, 0, 0);
    CHECK(s_sess.state == PUSH_STATE_IDLE && !s_sess.has_part && s_sess.received == 0);

    // 装完之后走完整武装链，手机问断点 → RECEIVING + RESUME，随后 DATA 必须被收。
    // After adopting, run the full arm chain: the phone asks the resume point, gets
    // RECEIVING + RESUME, and its next DATA packet must be accepted.
    push_session_init(&s_sess);
    push_session_adopt(&s_sess, &b, 482, 2);
    feed_arm(4821, 1800000, 0, 0);
    feed(PUSH_EVT_ADV_UP);
    feed(PUSH_EVT_CONNECT);
    CHECK(feed(PUSH_EVT_RESUME_QUERY) == PUSH_ACT_RESUME);
    CHECK(s_sess.state == PUSH_STATE_RECEIVING);
    CHECK(feed_data(2, 118) == (PUSH_ACT_APPEND | PUSH_ACT_PROGRESS));
    CHECK(s_sess.received == 600 && s_sess.next_seq == 3);
    // 正文收满后 END 直接过校验关。/ With the body complete, END passes verification.
    CHECK(feed(PUSH_EVT_END) == PUSH_ACT_VERIFY);
    CHECK(s_sess.state == PUSH_STATE_PROCESSING);
    CHECK(feed_verify(true, 10) == (PUSH_ACT_COMMIT | PUSH_ACT_OPEN_BOOK));

    // 没有半成品时问断点：RESUME(offset 0) 但留在 CONNECTED 等 BEGIN。
    // Asking without a partial: RESUME(offset 0) but stay in CONNECTED for a BEGIN.
    push_session_init(&s_sess);
    feed_arm(4821, 1800000, 0, 0);
    feed(PUSH_EVT_ADV_UP);
    feed(PUSH_EVT_CONNECT);
    CHECK(feed(PUSH_EVT_RESUME_QUERY) == PUSH_ACT_RESUME);
    CHECK(s_sess.state == PUSH_STATE_CONNECTED && !s_sess.has_part);
    printf("adopt: preload/resume-query->RECEIVING/DATA-after-resume ok\n");
}

static void test_teeth(void) {
    // 有牙 1：seq 跳号拒收且不落盘。/ Tooth 1: a seq gap is refused and never lands.
    push_session_init(&s_sess);
    feed_arm(1, 1800000, 0, 0);
    feed(PUSH_EVT_ADV_UP);
    feed(PUSH_EVT_CONNECT);
    push_begin_t b = make_begin(600, 1);
    feed_begin(&b);
    feed_data(0, 241);
    CHECK(feed_data(5, 241) == PUSH_ACT_ERR);
    CHECK(s_sess.last_err == PUSH_ERR_BAD_FRAME);
    CHECK(s_sess.received == 241 && s_sess.next_seq == 1);
    CHECK(s_sess.state == PUSH_STATE_RECEIVING);

    // 有牙 2：坏 CRC 拒收，回 CONNECTED 保留 .part。/ Tooth 2: a bad CRC refuses and keeps the partial.
    feed(PUSH_EVT_END);
    CHECK(s_sess.state == PUSH_STATE_PROCESSING);
    CHECK(feed_verify(false, 10) == PUSH_ACT_ERR);
    CHECK(s_sess.last_err == PUSH_ERR_CHECKSUM);
    CHECK(s_sess.state == PUSH_STATE_CONNECTED);
    CHECK(s_sess.has_part && s_sess.received == 241);

    // 有牙 3：长度不符同样拒。/ Tooth 3: a length mismatch refuses too.
    feed(PUSH_EVT_END);
    CHECK(feed_verify(true, 11) == PUSH_ACT_ERR);
    CHECK(s_sess.received == 241);

    // 有牙 4：total_len 超硬上限。/ Tooth 4: total_len over the hard cap.
    push_begin_t huge = make_begin(PUSH_TOTAL_MAX + 1, 1);
    CHECK(feed_begin(&huge) == PUSH_ACT_ERR);
    CHECK(s_sess.last_err == PUSH_ERR_TOO_LARGE);
    // total_len 为 0 的正文帧同样拒。/ A zero-length body frame is refused too.
    push_begin_t zero = make_begin(0, 1);
    CHECK(feed_begin(&zero) == PUSH_ACT_ERR);
    CHECK(s_sess.last_err == PUSH_ERR_BAD_FRAME);

    // 有牙 5：token 不符拒收（proximity 模型的闸门）。
    // Tooth 5: a token mismatch is refused (the proximity-model gate).
    push_begin_t wrong = make_begin(600, 9999);
    CHECK(feed_begin(&wrong) == PUSH_ACT_ERR);
    CHECK(s_sess.last_err == PUSH_ERR_BAD_FRAME);

    // 有牙 6：未武装/未连接时写入零动作。/ Tooth 6: writes outside a session do nothing.
    push_session_init(&s_sess);
    CHECK(feed_data(0, 10) == 0);
    CHECK(feed(PUSH_EVT_END) == 0);
    CHECK(feed(PUSH_EVT_RESUME_QUERY) == 0);
    CHECK(s_sess.received == 0 && s_sess.state == PUSH_STATE_IDLE);
    feed_arm(1, 1800000, 0, 0);
    feed(PUSH_EVT_ADV_UP);
    CHECK(feed_data(0, 10) == 0); // WINDOW 态未连接：拒。/ Not connected yet: refused.
    CHECK(s_sess.received == 0);

    // 有牙 7：窗口超时与拔电即退。/ Tooth 7: window timeout and unplug both exit.
    CHECK(feed_tick(1800000) == PUSH_ACT_ADV_OFF);
    CHECK(s_sess.state == PUSH_STATE_IDLE);
    feed_arm(1, 1800000, 0, 0);
    feed(PUSH_EVT_ADV_UP);
    CHECK(feed(PUSH_EVT_UNPLUG) == PUSH_ACT_ADV_OFF);
    CHECK(s_sess.state == PUSH_STATE_IDLE);

    // 有牙 8：取消清场带 DROP。/ Tooth 8: cancel tears down with DROP.
    feed_arm(1, 1800000, 0, 0);
    feed(PUSH_EVT_ADV_UP);
    feed(PUSH_EVT_CONNECT);
    feed_begin(&b);
    feed_data(0, 100);
    CHECK(feed(PUSH_EVT_CANCEL) == (PUSH_ACT_ADV_OFF | PUSH_ACT_DROP));
    CHECK(s_sess.state == PUSH_STATE_IDLE);

    // 充电常开：window_ms=0 永不到期。/ Charger-held: window_ms == 0 never expires.
    push_session_init(&s_sess);
    feed_arm(1, 0, 5000, 0);
    feed(PUSH_EVT_ADV_UP);
    CHECK(feed_tick(0xFFFFFFFFu) == 0);
    CHECK(s_sess.state == PUSH_STATE_WINDOW);
    CHECK(feed(PUSH_EVT_UNPLUG) == PUSH_ACT_ADV_OFF); // 拔电即退，不转窗口。/ Unplug exits outright.

    // 常开 + 传输中拔电：UNPLUG 不打断传输，推完进宽限，宽限到期直接收摊
    // （没有窗口可回）。不然拔了电还挂着广播，用户以为退了其实没退。
    // Held + unplug mid-transfer: UNPLUG never interrupts the transfer; after it
    // the grace expires straight into IDLE (there is no window to return to).
    // Otherwise the radio would keep advertising after the unplug while the user
    // believes it stopped.
    push_session_init(&s_sess);
    feed_arm(9, 0, 5000, 0);
    feed(PUSH_EVT_ADV_UP);
    feed(PUSH_EVT_CONNECT);
    push_begin_t hb = make_begin(600, 9);
    CHECK(feed_begin(&hb) == PUSH_ACT_READY);
    feed_data(0, 241);
    feed_data(1, 241);
    feed_data(2, 118);
    CHECK(feed(PUSH_EVT_END) == PUSH_ACT_VERIFY);
    CHECK(s_sess.state == PUSH_STATE_PROCESSING);
    CHECK(feed_verify(true, 100) == (PUSH_ACT_COMMIT | PUSH_ACT_OPEN_BOOK));
    CHECK(s_sess.state == PUSH_STATE_COOLDOWN);
    // 宽限未到：挂着。/ Before the grace expires: it stays.
    CHECK(feed_tick(100 + 4999) == 0);
    CHECK(s_sess.state == PUSH_STATE_COOLDOWN);
    // 宽限一到：收摊，不回窗口。/ Grace expiring: closes up, no window return.
    CHECK(feed_tick(100 + 5000) == PUSH_ACT_ADV_OFF);
    CHECK(s_sess.state == PUSH_STATE_IDLE);

    // 非法转移保持原状。/ Illegal transitions hold state.
    push_session_init(&s_sess);
    feed_arm(1, 1800000, 0, 0);
    feed(PUSH_EVT_ADV_UP);
    feed(PUSH_EVT_CONNECT);
    feed_begin(&b);
    CHECK(feed(PUSH_EVT_ADV_UP) == 0);
    CHECK(s_sess.state == PUSH_STATE_RECEIVING);
    CHECK(feed(PUSH_EVT_CONNECT) == 0);
    CHECK(s_sess.state == PUSH_STATE_RECEIVING);
    printf("teeth: seq gap/bad CRC/length/total cap/token/unarmed/timeout/unplug/cancel/charger-hold all refuse ok\n");
}

/* ---- py<->C 跨语言 golden vector ----
   下列字节全部由 tools/android/push_pico.py 的 --hex 模式生成（token=0x1A2B,
   type=TEXT, session_id=1, title="跨语言向量✓", 正文 300 字节 = 0xf4ab327b）。
   「协议冻结」若只是人眼读源，那么真机轮就是唯一的跨语言验证；这里把 py 吐的
   帧钉成 C 侧解码断言，py<->C 对表从人眼升级为机器级。最强的那一条在末尾：
   把两包 DATA 的载荷拼回正文，用 C 的 push_crc32 重算，与 py 写进 BEGIN 的
   crc32 字段必须逐位相同——CRC、长度、分包边界、seq 语义四者一次性对死。
   ---- py<->C cross-language golden vector ----
   The bytes below come from push_pico.py --hex (token=0x1A2B, type=TEXT,
   session_id=1, title="跨语言向量✓", 300-byte body, crc 0xf4ab327b). If the
   frozen protocol were only eyeballed against the source, the hardware round
   would be the sole cross-language check; pinning the frames Python emits as C
   decode assertions upgrades py<->C from eyeball to machine. The strongest one
   is last: concatenate both DATA payloads back into the body, recompute with C's
   push_crc32 and require bit-exact equality with the crc32 Python wrote into
   BEGIN - CRC, length, chunk boundaries and seq semantics pinned in one shot. */
static void test_py_cross_vector(void) {
    static const uint8_t begin[83] = {
        0x50, 0x43, 0x01, 0x01, 0x00, 0x2b, 0x1a, 0x2c, 0x01, 0x00, 0x00, 0x7b,
        0x32, 0xab, 0xf4, 0x01, 0x00, 0x00, 0x00, 0xe8, 0xb7, 0xa8, 0xe8, 0xaf,
        0xad, 0xe8, 0xa8, 0x80, 0xe5, 0x90, 0x91, 0xe9, 0x87, 0x8f, 0xe2, 0x9c,
        0x93,
        // 余 46 字节标题区补零 / remaining 46 title bytes are zero-padded
    };
    // 标题区不足 83 的补零由编译器完成，这里显式拼足，避免依赖静默行为。
    // Padding is explicit rather than relying on silent zero-init.
    uint8_t frame[83];
    memcpy(frame, begin, sizeof(begin));
    memset(frame + sizeof(begin), 0, sizeof(frame) - sizeof(begin));

    push_begin_t b;
    CHECK(push_begin_decode(frame, sizeof(frame), &b));
    CHECK(b.token == 0x1A2B);
    CHECK(b.type == PUSH_TYPE_TEXT);
    CHECK(b.total_len == 300u);
    CHECK(b.crc32 == 0xf4ab327bu);
    CHECK(b.session_id == 1u);
    CHECK(strcmp(b.title, "跨语言向量✓") == 0);

    // py 生成的完整 DATA 帧（含载荷）：push_data_decode 会校验载荷确实在
    // 缓冲区里（len >= 3 + n），只喂 3 字节头是解不出来的——所以嵌全帧。
    // Full DATA frames from Python (payload included): push_data_decode
    // checks the payload is really present (len >= 3 + n), so a bare
    // 3-byte header cannot decode - the whole frame is embedded.
    static const uint8_t data0[244] = {
        0x00, 0x00, 0xf1, 0x52, 0x65, 0x61, 0x64, 0x50, 0x69, 0x63, 0x6f, 0x20,
        0x70, 0x79, 0x2d, 0x43, 0x20, 0x76, 0x65, 0x63, 0x74, 0x6f, 0x72, 0x2e,
        0x20, 0x52, 0x65, 0x61, 0x64, 0x50, 0x69, 0x63, 0x6f, 0x20, 0x70, 0x79,
        0x2d, 0x43, 0x20, 0x76, 0x65, 0x63, 0x74, 0x6f, 0x72, 0x2e, 0x20, 0x52,
        0x65, 0x61, 0x64, 0x50, 0x69, 0x63, 0x6f, 0x20, 0x70, 0x79, 0x2d, 0x43,
        0x20, 0x76, 0x65, 0x63, 0x74, 0x6f, 0x72, 0x2e, 0x20, 0x52, 0x65, 0x61,
        0x64, 0x50, 0x69, 0x63, 0x6f, 0x20, 0x70, 0x79, 0x2d, 0x43, 0x20, 0x76,
        0x65, 0x63, 0x74, 0x6f, 0x72, 0x2e, 0x20, 0x52, 0x65, 0x61, 0x64, 0x50,
        0x69, 0x63, 0x6f, 0x20, 0x70, 0x79, 0x2d, 0x43, 0x20, 0x76, 0x65, 0x63,
        0x74, 0x6f, 0x72, 0x2e, 0x20, 0x52, 0x65, 0x61, 0x64, 0x50, 0x69, 0x63,
        0x6f, 0x20, 0x70, 0x79, 0x2d, 0x43, 0x20, 0x76, 0x65, 0x63, 0x74, 0x6f,
        0x72, 0x2e, 0x20, 0x52, 0x65, 0x61, 0x64, 0x50, 0x69, 0x63, 0x6f, 0x20,
        0x70, 0x79, 0x2d, 0x43, 0x20, 0x76, 0x65, 0x63, 0x74, 0x6f, 0x72, 0x2e,
        0x20, 0x52, 0x65, 0x61, 0x64, 0x50, 0x69, 0x63, 0x6f, 0x20, 0x70, 0x79,
        0x2d, 0x43, 0x20, 0x76, 0x65, 0x63, 0x74, 0x6f, 0x72, 0x2e, 0x20, 0x52,
        0x65, 0x61, 0x64, 0x50, 0x69, 0x63, 0x6f, 0x20, 0x70, 0x79, 0x2d, 0x43,
        0x20, 0x76, 0x65, 0x63, 0x74, 0x6f, 0x72, 0x2e, 0x20, 0x52, 0x65, 0x61,
        0x64, 0x50, 0x69, 0x63, 0x6f, 0x20, 0x70, 0x79, 0x2d, 0x43, 0x20, 0x76,
        0x65, 0x63, 0x74, 0x6f, 0x72, 0x2e, 0x20, 0x52, 0x65, 0x61, 0x64, 0x50,
        0x69, 0x63, 0x6f, 0x20, 0x70, 0x79, 0x2d, 0x43, 0x20, 0x76, 0x65, 0x63,
        0x74, 0x6f, 0x72, 0x2e,
    };

    static const uint8_t data1[62] = {
        0x01, 0x00, 0x3b, 0x20, 0x52, 0x65, 0x61, 0x64, 0x50, 0x69, 0x63, 0x6f,
        0x20, 0x70, 0x79, 0x2d, 0x43, 0x20, 0x76, 0x65, 0x63, 0x74, 0x6f, 0x72,
        0x2e, 0x20, 0x52, 0x65, 0x61, 0x64, 0x50, 0x69, 0x63, 0x6f, 0x20, 0x70,
        0x79, 0x2d, 0x43, 0x20, 0x76, 0x65, 0x63, 0x74, 0x6f, 0x72, 0x2e, 0x20,
        0x52, 0x65, 0x61, 0x64, 0x50, 0x69, 0x63, 0x6f, 0x20, 0x70, 0x79, 0x2d,
        0x43, 0x20,
    };
    uint16_t seq; const uint8_t *pl; size_t n;
    CHECK(push_data_decode(data0, sizeof(data0), &seq, &pl, &n));
    CHECK(seq == 0 && n == 241);
    CHECK(pl == data0 + 3);            // 载荷紧跟 3 字节头 / payload follows the 3-byte header
    CHECK(push_data_decode(data1, sizeof(data1), &seq, &pl, &n));
    CHECK(seq == 1 && n == 59);
    CHECK(241 + 59 == 300); // 两包拼回 total_len / the two chunks sum to total_len
    // 拼回的正文用 C 的 CRC32 算，须等于 py BEGIN 里的 crc32 字段。
    // The concatenated body, hashed with C's CRC32, must equal the crc32 field
    // Python wrote into BEGIN.
    uint32_t crc_reassembled = push_crc32(data0 + 3, 241);
    crc_reassembled = push_crc32_continue(crc_reassembled, data1 + 3, 59);
    CHECK(crc_reassembled == b.crc32);

    // CTRL 公共头 / CTRL shared header
    static const uint8_t end_f[4] = {0x50, 0x43, 0x02, 0x01};
    static const uint8_t rq_f[4] = {0x50, 0x43, 0x03, 0x01};
    uint8_t op;
    CHECK(push_ctrl_header_decode(end_f, sizeof(end_f), &op) && op == PUSH_OP_END);
    CHECK(push_ctrl_header_decode(rq_f, sizeof(rq_f), &op) && op == PUSH_OP_RESUME_QUERY);

    // 端到端 CRC：py 的正文原文用 C 的 CRC32 重算，须等于 py 写进 BEGIN 的值。
    // End-to-end CRC: recompute Python's body with C's CRC32; must equal the
    // value Python wrote into BEGIN.
    static const char body[] = "ReadPico py-C vector. ";
    uint32_t c = 0;
    // 正文是上面 22 字节短语的重复后截断到 300 字节，逐段喂验证链式等价。
    // The body is that 22-byte phrase repeated then truncated to 300; feed it in
    // pieces to also exercise the streaming chain.
    size_t fed = 0;
    while (fed < 300) {
        const size_t take = (300 - fed) < sizeof(body) - 1 ? (300 - fed) : sizeof(body) - 1;
        c = push_crc32_continue(c, body, take);
        fed += take;
    }
    CHECK(fed == 300);
    CHECK(c == 0xf4ab327bu);          // C 的 CRC32 == py 的 CRC32
    CHECK(c == b.crc32);              // 且与 py BEGIN 帧里的字段逐位相同

    printf("py<->C golden vector: BEGIN/DATA/CTRL decode + CRC end-to-end ok\n");
}

int main(void) {
    test_crc32();
    test_ctrl_header();
    test_begin_codec();
    test_data_status_codec();
    test_py_cross_vector();
    test_happy_path();
    test_word_card();
    test_resume();
    test_adopt();
    test_teeth();
    printf("push_sink_host_test: %d checks, %d red\n", s_checks, s_red);
    return s_red == 0 ? 0 : 1;
}
