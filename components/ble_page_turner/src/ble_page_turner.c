/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：BLE HID 主机的 IDF NimBLE 实现。扫描、连接、配对（Just Works）与 HID 输入报告
 * 解析。报告解析、原始边沿、自动重复与 bond 持久化的逻辑移植自 CrossMux 的
 * BleKeyboardHost；BLE 胶水层按 ESP-IDF 的 NimBLE 回调模型重写（IDF 把 GATT client
 * 的 API 放在 host/ble_gatt.h，没有单独的 ble_gattc.h）。
 *
 * English: IDF NimBLE implementation of the BLE HID host: scan, connect, Just Works pairing
 * and HID input-report parsing. Report parsing, raw-button edges, auto-repeat and bond
 * persistence are ported from CrossMux's BleKeyboardHost; the BLE plumbing is rewritten for
 * ESP-IDF's NimBLE (which keeps the GATT client API in host/ble_gatt.h, with no ble_gattc.h).
 *
 * 冻结：不做刷屏、不决定翻页语义、不碰本模块 NVS 命名空间以外的存储。
 * Frozen: no drawing, no page-turn policy, no storage outside this module's NVS namespace.
 */

#include "ble_page_turner.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "push_sink.h"
#include "read_pico_crashlog.h"

#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_sm.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"
#include "store/config/ble_store_config.h"

static const char *TAG = "ble_pt";

// 网络占用先登记，再串行拆栈，防止主循环同时重建蓝牙。/ Reserve network memory before serial teardown to prevent a concurrent BLE restart.
static atomic_flag s_lifecycle = ATOMIC_FLAG_INIT;
static atomic_uint s_network_users;
static TaskHandle_t s_host_task;
static bool lifecycle_take(uint32_t timeout_ms) {
    const int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (atomic_flag_test_and_set(&s_lifecycle)) {
        if (esp_timer_get_time() >= deadline) return false;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return true;
}
static void lifecycle_give(void) { atomic_flag_clear(&s_lifecycle); }

/* ---- HID 常量 / HID constants ---- */

#define HID_SVC_UUID 0x1812
#define HID_CHR_REPORT_MAP 0x2A4B
#define HID_CHR_REPORT 0x2A4D
#define HID_CHR_PROTOCOL_MODE 0x2A4E
#define HID_CHR_BOOT_KBD_INPUT 0x2A22

// 静态存储期的 UUID：这些调用是异步的，且不拷贝 UUID，只留指针。用 BLE_UUID16_DECLARE
// 的复合字面量会在块结束时失效，procedure 真正执行时就是野指针——表现为调用返回 0、
// 回调永远不来。
// UUIDs with static storage duration. These calls are asynchronous and do NOT copy the UUID,
// only the pointer. A BLE_UUID16_DECLARE compound literal dies at the end of its block, so the
// procedure later runs against a dangling pointer: the call returns 0 and the callback never
// arrives.
#define HID_CHR_CCCD 0x2902

// 启动前的内存下限。内部 RAM 才是瓶颈：控制器与协议栈任务只能放这里（硬件路径读不到
// PSRAM）。阈值取 CrossMux 在同一块板子上验证过的值；本仓库把文件列表、图片列表、壁纸
// 列表、字形缓存和字体目录搬进 PSRAM 之后，DIRAM 空闲约 148KB，这里过得绰绰有余。
// 定得太低会半途失败，失败由退避和自锁兜住。
// Memory floors checked before starting. Internal RAM is the constraint, since the controller and
// the stack task can only live there (the hardware path cannot read PSRAM). The values are the
// ones CrossMux verified on this same board; after this tree moved its file, image, wallpaper,
// glyph-cache and font-catalogue arrays into PSRAM, DIRAM has about 148 KB free, so this passes
// with room to spare. Too low and the start fails halfway, which the back-off and self-lock
// contain.
#define BLE_PT_MIN_FREE_INTERNAL (56 * 1024)
#define BLE_PT_MIN_LARGEST_INTERNAL (20 * 1024)
#define BLE_PT_MIN_FREE_PSRAM (256 * 1024)

// 启动失败后的退避。/ Back-off after a failed start.
#define BLE_PT_RETRY_BACKOFF_MS 30000
// 连续失败这么多次就自锁，避免「开机即崩 → 重启 → 再崩」的死循环。
// Self-lock after this many consecutive failures, so "crash on boot, reboot, crash again" cannot
// loop forever.
#define BLE_PT_MAX_BOOT_TRIES 3
// 栈稳定跑这么久才把尝试计数清零。/ Tries are only cleared once the stack has run this long.
#define BLE_PT_STABLE_MS 10000
// 自动重连间隔。取值同 CrossMux 的 kReconnectBackoffMs。
// Auto-reconnect interval, matching CrossMux's kReconnectBackoffMs.
#define BLE_PT_RECONNECT_MS 4000
#define NVS_NS_BLE_STATE "rp_blestate"

// 报告帧与键环的容量。报告一般 8-9 字节，留 16 足够覆盖带 report id 的复合设备。
// Report frames are typically 8-9 bytes; 16 covers composite devices that prefix a report id.
#define FRAME_MAX 16
#define KEY_RING_LEN 16
// 八次按下连同各自的释放，再加一格用来区分满与空。/ Eight presses with releases, plus one to tell full from empty.
#define RAW_RING_LEN 17
#define MAX_INPUT_CHRS 4

/* ---- 环形缓冲 / Rings ---- */

typedef struct {
    ble_pt_event_t items[KEY_RING_LEN];
    volatile uint8_t head, tail;
} key_ring_t;

typedef struct {
    ble_pt_raw_t items[RAW_RING_LEN];
    volatile uint8_t head, tail;
} raw_ring_t;

static key_ring_t s_keys;
static raw_ring_t s_raws;
static portMUX_TYPE s_ring_lock = portMUX_INITIALIZER_UNLOCKED;

/* ---- 模块状态 / Module state ---- */

static ble_pt_device_t s_devices[BLE_PT_MAX_DEVICES];
static uint8_t s_device_count;
static ble_pt_bond_t s_bonds[BLE_PT_MAX_BONDS];
static uint8_t s_bond_count;
static char s_conn_addr[18];
static char s_conn_name[32];
// 本次链路是否已经把对端记进配对表：记一次就够，见 remember_live_peer()。
// Whether this link's peer has already been recorded; once is enough, see remember_live_peer().
static bool s_peer_remembered;
// 最近一次收到报告的时刻，以及按下状态的兜底超时。翻页器常常不发「松开」那条全零报告，
// 没有这道兜底，按一下就会自己一直翻页，每秒八页地翻到内存耗尽重启。
// When a report last arrived, and the fallback timeout for the held state. Remotes often never
// send the all-zero release report, and without this timeout one press keeps turning pages by
// itself, eight a second, until memory runs out and the board restarts.
#define HELD_TIMEOUT_MS 5000
static uint32_t s_last_rx_ms;
// 报告路径的去抖：同一个键这么久之内只投递一次，用来挡住"按住期间帧帧都算新按下"。
// Report-path debounce: one delivery per key within this window, which stops frames that all look
// like a fresh press while a key is held.
#define SAME_KEY_DEBOUNCE_MS 500
static uint8_t s_emit_usage;
static uint32_t s_emit_ms;
// 一次按住最多重复多少次：约 7.7 秒，够连着翻页，又让重复无论如何都有终点。
// How many repeats one hold may produce: about 7.7 s, enough for a run of page turns while
// guaranteeing the repeat loop ends.
#define REPEAT_MAX 60
static uint8_t s_repeat_count;
// 本次链路用的是哪种对端地址：连接时用它，记住设备时也存它。
// Which peer address type this link used: it is needed to connect and is stored when the peer is
// remembered.
static uint8_t s_conn_addr_type = BLE_ADDR_PUBLIC;
static char s_failure[128];
// 启动策略状态：退避截止、稳定计时起点、上一次同步是否已发生过、启动失败原因。
// Start-policy state: back-off deadline, stability timer, whether a sync has happened yet, and
// the start-failure text.
static uint32_t s_retry_after_ms;
static uint32_t s_stable_since;
static char s_start_failure[160];
// 自动重连：链路曾经建立过才记这里；用户主动断开时清空，免得"关掉"之后又被自己连回来。
// Auto-reconnect: recorded once a link has been up. Cleared on an explicit disconnect, so
// switching it off is not immediately undone by the retry.
static char s_reconnect_addr[18];
static uint32_t s_reconnect_at;
// 待连目标：已有链路在建立或已建立时点了另一台，就记在这里，等断开事件落地再由 poll() 发起。
// Pending target: picking another peer while a link exists or is coming up records it here, and
// poll() starts it once the disconnect has landed.
static char s_pending_addr[18];
static volatile bool s_connected, s_connecting, s_scanning;
// 建立链路的进度标志：服务发现是否走完（成功失败都算）、加密阶段是否结束。
// 回调写、状态机读，定义在这里是因为服务发现回调比状态机更早用到它们。
// Bring-up progress flags: whether service discovery finished (either way) and whether the
// security step is over. Callbacks write them and the state machine reads them; they live here
// because the discovery callback needs them before the state machine is defined.
static volatile bool s_discovery_done;
static volatile bool s_enc_done;
// 请求取消扫描的时刻：DISC_COMPLETE 迟迟不来时用它兜底清标志。
// When the scan cancel was requested, so a missing DISC_COMPLETE can be caught by a timeout.
static uint32_t s_scan_cancel_ms;
// 排队连接的下一次尝试时刻：控制器忙（扫描取消未落地）时按 300ms 重试，别每轮都撞一次。
// When a queued connect may be retried: 300 ms apart while the controller is busy with a scan
// cancel, rather than hammering it every tick.
static uint32_t s_pending_at;
static atomic_bool s_running;
// 链路是否已经建立（GAP 层）。与 s_connected 分开：后者还要求服务发现做完。重连必须以
// 这个为准——否则发现没走完时会把一条好链路反复拆掉重建，表现就是"连上一会儿又掉"。
// Whether the GAP link exists. Separate from s_connected, which also requires discovery to have
// finished. Reconnect must key off this: keyed off s_connected it tears down a perfectly good
// link whenever discovery has not completed, which reads as a connection that keeps dropping.
static volatile bool s_link_up;
static uint32_t s_connect_ms;
static volatile uint32_t s_passkey;
static volatile bool s_passkey_ready;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint8_t s_own_addr_type;


// 定义在扫描那一节，但 note_peer() 也要用。/ Defined in the discovery section, also used by note_peer().
static const ble_pt_device_t *device_find(const char *addr);

// 已发现的可订阅输入报告。/ Subscribable input reports found on the peer.
static uint16_t s_input_chr_vals[MAX_INPUT_CHRS];
static uint16_t s_input_chr_cccds[MAX_INPUT_CHRS];
static uint8_t s_input_chr_count;
static uint8_t s_subscribed_count;
// HID 服务的结束句柄。ble_gatt_chr 只有 def_handle/val_handle，没有 end_handle，
// 所以发现描述符时用服务自己的范围。
// The HID service's end handle. ble_gatt_chr carries def_handle/val_handle but no end_handle,
// so descriptor discovery uses the service's own range.
static uint16_t s_hid_svc_end = 0xFFFF;
// 发现是否已经发起过。/ Whether discovery has been kicked off for this link.
static bool s_discovery_started;

// 报告映射给出的提示：哪个字节更可能是键码。/ Report-map hint for the byte that holds the code.
static bool s_has_keyboard_page, s_has_consumer_page;
static uint8_t s_preferred_byte = 0xFF;

// 静止帧：外设什么都没按时发的报告。它唯一的用途是判断一帧算不算「按下了」。学习界面会把
// 点「学习」那一刻的帧定为基准——有些遥控器的静止帧带着非零常量（协议字节、状态字节），只按
// 「全零」判断会把静止帧也当成一次按下，学到的就成了它。第一帧「什么都没按」的报告照旧算基准。
// The rest frame: the report the remote sends with nothing pressed. Its only job is deciding
// whether a frame counts as a press. A learning screen pins the frame present when 学习 is tapped
// as the baseline, because some remotes keep non-zero constants in their resting frame and an
// all-zero test alone would read that frame as a press and learn it instead. The first report with
// nothing pressed still becomes one.
static ble_pt_frame_t s_rest;
static bool s_rest_known;

// 原始边沿的判定状态：正在按着的那一帧、上一帧、按下之前那一帧，以及最近一帧的到达时刻。
// Press state for raw edges: the frame the open press was recorded from, the previous frame, the
// frame before the press, and when the last frame arrived.
static ble_pt_frame_t s_raw_held, s_raw_prev, s_raw_base;
static bool s_raw_down;
static uint32_t s_raw_last_ms;

// 静默多久算这一次按下结束。不少遥控器松开时不发报告，只能靠静默收尾；收尾之后同一个帧再发来
// 才算新的一次按下，否则第二次按同一个键会被当成「还按着」而丢掉。
// How long silence ends a press. Many remotes send nothing on release, so silence closes it; once
// closed, the same frame counts as a fresh press -- otherwise the second press of a key reads as
// "still held" and is dropped.
#define RAW_RELEASE_GAP_MS 150

// 自动重复：HID 只在状态变化时发报告，按住不放只会来一次，所以自己合成重复。
// Auto-repeat: HID only reports on state change, so a held key arrives once and repeats are
// synthesized here.
static volatile uint8_t s_held_usage, s_held_mods;
static volatile uint32_t s_held_since, s_last_repeat;

/* ---- 手动映射 / Manual bindings ---- */

#define NVS_NS_BINDINGS "rp_blebind"
#define NVS_NS_BONDS "rp_blebond"

typedef struct {
    char addr[18];
    ble_pt_frame_t frames[BLE_PT_MAX_BINDINGS];  // 槽位 0 = PREV，1 = NEXT / slot 0 = PREV, 1 = NEXT
} binding_set_t;

static binding_set_t s_bindings;  // 当前连接外设的绑定 / bindings of the connected peer

/* ---- NVS ---- */

// 地址不能直接当 NVS 键名（含冒号且可能超长），所以用 FNV-1a 折算。
// An address cannot be an NVS key (colons, length), so it is folded with FNV-1a.
static uint32_t addr_hash(const char *addr) {
    uint32_t h = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)addr; p && *p; ++p) h = (h ^ *p) * 16777619u;
    return h;
}

static const char *bond_key(char *buf, size_t cap, const char *addr) {
    snprintf(buf, cap, "b_%08x", (unsigned)addr_hash(addr));
    return buf;
}

static void bonds_load(void) {
    s_bond_count = 0;
    nvs_handle_t h;
    if (nvs_open(NVS_NS_BONDS, NVS_READONLY, &h) != ESP_OK) return;
    for (uint8_t i = 0; i < BLE_PT_MAX_BONDS; ++i) {
        char key[16];
        snprintf(key, sizeof(key), "d_%u", (unsigned)i);
        ble_pt_bond_t bond = {0};
        size_t len = sizeof(bond);
        if (nvs_get_blob(h, key, &bond, &len) == ESP_OK && bond.addr[0]) s_bonds[s_bond_count++] = bond;
    }
    nvs_close(h);
}

static void bonds_save(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS_BONDS, NVS_READWRITE, &h) != ESP_OK) return;
    for (uint8_t i = 0; i < BLE_PT_MAX_BONDS; ++i) {
        char key[16];
        snprintf(key, sizeof(key), "d_%u", (unsigned)i);
        if (i < s_bond_count) nvs_set_blob(h, key, &s_bonds[i], sizeof(s_bonds[i]));
        else nvs_erase_key(h, key);
    }
    nvs_commit(h);
    nvs_close(h);
}

static ble_pt_bond_t *bond_find(const char *addr) {
    for (uint8_t i = 0; i < s_bond_count; ++i)
        if (!strcmp(s_bonds[i].addr, addr)) return &s_bonds[i];
    return NULL;
}

static void bond_add(const char *addr, const char *name, uint8_t type) {
    if (!addr || !addr[0]) return;
    ble_pt_bond_t *existing = bond_find(addr);
    if (existing) {
        // 名字没变就不写 NVS：这条路径每次链路可用都会被走到，而 NVS 写要几毫秒和可观的栈，
        // 放在通知回调的上下文里做，按键一密集就会撞上 NimBLE host 任务的栈上限。
        // Nothing is stored when the name is unchanged: this path runs on every usable link, and an
        // NVS write costs milliseconds and a lot of stack -- done inside the notification callback
        // it walks into the NimBLE host task's stack limit as soon as keys come fast.
        if (!name || !name[0] || strcmp(existing->name, name) == 0) return;
        snprintf(existing->name, sizeof(existing->name), "%s", name);
        bonds_save();
        return;
    }
    if (s_bond_count >= BLE_PT_MAX_BONDS) {
        // 满了就顶掉最老的一个，否则新外设永远配不上。
        // Evict the oldest, or a new peripheral could never be paired.
        memmove(&s_bonds[0], &s_bonds[1], sizeof(s_bonds[0]) * (BLE_PT_MAX_BONDS - 1));
        s_bond_count = BLE_PT_MAX_BONDS - 1;
    }
    ble_pt_bond_t *slot = &s_bonds[s_bond_count++];
    memset(slot, 0, sizeof(*slot));
    snprintf(slot->addr, sizeof(slot->addr), "%s", addr);
    if (name) snprintf(slot->name, sizeof(slot->name), "%s", name);
    slot->addr_type = type;
    bonds_save();
}

static void bindings_load(const char *addr) {
    memset(&s_bindings, 0, sizeof(s_bindings));
    if (!addr || !addr[0]) return;
    snprintf(s_bindings.addr, sizeof(s_bindings.addr), "%s", addr);
    nvs_handle_t h;
    if (nvs_open(NVS_NS_BINDINGS, NVS_READONLY, &h) != ESP_OK) return;
    char key[16];
    size_t len = sizeof(s_bindings.frames);
    // 旧固件在这里存的是 4 个 32 位码值，长度对不上就当作没绑过：让用户重新学一次，比按一个
    // 错误的身份翻页好。
    // Older firmware stored four 32-bit codes here; a length mismatch counts as nothing bound,
    // which beats turning pages on a wrong identity.
    if (nvs_get_blob(h, bond_key(key, sizeof(key), addr), s_bindings.frames, &len) != ESP_OK ||
        len != sizeof(s_bindings.frames))
        memset(s_bindings.frames, 0, sizeof(s_bindings.frames));
    nvs_close(h);
}

/* ---- 环形缓冲操作 / Ring operations ---- */

static uint32_t s_input_serial;
uint32_t ble_pt_input_serial(void) {
    portENTER_CRITICAL(&s_ring_lock);
    uint32_t serial = s_input_serial;
    portEXIT_CRITICAL(&s_ring_lock);
    return serial;
}
static void key_push(const ble_pt_event_t *ev) {
    ble_pt_crumb(BLE_PT_CRUMB_KEY_PUSH);
    portENTER_CRITICAL(&s_ring_lock);
    ++s_input_serial;
    const uint8_t next = (uint8_t)((s_keys.head + 1) % KEY_RING_LEN);
    if (next != s_keys.tail) {
        s_keys.items[s_keys.head] = *ev;
        s_keys.head = next;
    }
    portEXIT_CRITICAL(&s_ring_lock);
}

// 一次按下只有在它的释放还有位置时才入环，环满就整对丢弃，绝不只丢释放。
// A press only enters with room left for its release; a full ring drops the whole pair, never a
// release alone.
static void raw_push(bool pressed, const ble_pt_frame_t *frame) {
    portENTER_CRITICAL(&s_ring_lock);
    if (pressed) ++s_input_serial;
    const uint8_t used = (uint8_t)((s_raws.head - s_raws.tail + RAW_RING_LEN) % RAW_RING_LEN);
    const uint8_t keep = pressed ? 1 : 0;  // 按下要给自己的释放留位 / a press reserves room for its release
    if ((uint8_t)(RAW_RING_LEN - 1 - used) > keep) {
        ble_pt_raw_t *slot = &s_raws.items[s_raws.head];
        slot->frame = *frame;
        slot->pressed = pressed;
        slot->at_ms = (uint32_t)(esp_timer_get_time() / 1000);
        s_raws.head = (uint8_t)((s_raws.head + 1) % RAW_RING_LEN);
    }
    portEXIT_CRITICAL(&s_ring_lock);
}

/* ---- 整帧身份 / Whole-frame identity ---- */

// 整份清零再拷内容：比较只看 len 个字节，但入环、进 NVS 的结构体里不该有未初始化的字节。
// Zero the whole struct before copying: comparisons look at len bytes only, but neither the ring
// nor the struct written to NVS should carry uninitialised bytes.
static ble_pt_frame_t frame_of(const uint8_t *data, size_t len) {
    ble_pt_frame_t frame = {0};
    if (len > BLE_PT_FRAME_MAX) len = BLE_PT_FRAME_MAX;
    frame.len = (uint8_t)len;
    if (len) memcpy(frame.bytes, data, len);
    return frame;
}

// 逐字节比两帧，长度也要一样；空帧不等于任何帧。
// Compare two frames byte for byte, lengths included; an empty frame equals nothing.
static bool frame_same(const ble_pt_frame_t *a, const ble_pt_frame_t *b) {
    return a->len && a->len == b->len && !memcmp(a->bytes, b->bytes, a->len);
}

// 一帧是不是「什么都没按」：全零，或就是基准帧。9 字节帧的首字节是报告号，不是键码。
// Whether a frame means "nothing pressed": all zero, or exactly the baseline. Byte 0 of a 9-byte
// frame is the report id, not a key code.
static bool frame_idle(const ble_pt_frame_t *frame) {
    if (!frame->len) return true;
    const size_t first = frame->len == 9 ? 1 : 0;
    bool all_zero = true;
    for (size_t i = first; i < frame->len && all_zero; ++i) all_zero = frame->bytes[i] == 0;
    if (all_zero) return true;
    return s_rest_known && frame_same(frame, &s_rest);
}

// 收尾一次按下：松开边沿带的是按下那一刻的整帧，绑在它上面的动作才认得出是同一个键。
// Close an open press: the release edge carries the frame the press was recorded from, so an action
// bound to that frame can tell it is the same key.
static void raw_release(void) {
    s_raw_down = false;
    raw_push(false, &s_raw_held);
}

// 断链或换台：清掉原始边沿的状态，免得新外设的第一帧接着上一台没结束的按下。
// A dropped or switched link clears the raw state, so the next peer's first frame does not
// continue a press the previous peer left open.
static void raw_reset(void) {
    s_raw_down = false;
    s_raw_held.len = s_raw_prev.len = s_raw_base.len = 0;
    s_raw_last_ms = 0;
}

// 一帧 → 原始边沿。整帧就是身份：同一个键每次按下的字节一模一样，记下这一串就够。
// One frame to raw edges. The whole frame is the identity: the same key sends the same bytes on
// every press, so recording that string is all it takes.
static void raw_frame(const uint8_t *data, size_t len, uint32_t now) {
    // 静默够久 = 上一次按下已经结束（遥控器多半不发松开报告）。先收尾，再判这一帧。
    // Enough silence means the previous press is over (most remotes send no release report); close
    // it before judging this frame.
    if (s_raw_down && now - s_raw_last_ms > RAW_RELEASE_GAP_MS) raw_release();

    const ble_pt_frame_t frame = frame_of(data, len);
    if (frame_idle(&frame)) {
        if (s_raw_down) raw_release();
    } else if (!s_raw_down) {
        s_raw_base = s_raw_prev;  // 按下之前那一帧，多半是静止帧 / the frame before the press, usually the rest frame
        s_raw_held = frame;
        s_raw_down = true;
        raw_push(true, &s_raw_held);
    } else if (!frame_same(&frame, &s_raw_held)) {
        // 帧变了，上一次按下到此为止。回到按下之前那一帧只是松开（有些遥控器的松开帧带着非零
        // 常量）；换成别的帧才算又一次按下，这样连按两个不同的键不会丢掉后一个。
        // The frame changed, so the previous press is over. Back at the pre-press frame it is only a
        // release -- some remotes send non-zero constants on release; any other frame is another
        // press, so two different keys in quick succession do not lose the second one.
        raw_release();
        if (!frame_same(&frame, &s_raw_base)) {
            s_raw_base = s_raw_prev;
            s_raw_held = frame;
            s_raw_down = true;
            raw_push(true, &s_raw_held);
        }
    }
    s_raw_prev = frame;
    s_raw_last_ms = now;
}

/* ---- HID 报告解析（移植自 CrossMux）/ Report parsing (ported) ---- */

// 报告页提示：报告映射里 0x05 0x07 是键盘页，0x05 0x0C 是消费者页。键盘页的键码在
// 第 2 字节（修饰键之后），消费者页的用法码在第 1 字节。
// 阅星瞳协议：一字节报告里 0x01 上一页、0x02 下一页。
// YueXingTong page turners: a one-byte report where 0x01 is previous and 0x02 is next.
#define YXT_CODE_PREV 0x01u
#define YXT_CODE_NEXT 0x02u

// Report-map hints: 0x05 0x07 marks the keyboard page, 0x05 0x0C the consumer page. On the
// keyboard page the code sits in byte 2 (after the modifier byte), on the consumer page in byte 1.
static void report_map_hints(const uint8_t *map, size_t len) {
    if (!map || len < 2) return;
    for (size_t i = 0; i + 1 < len; ++i) {
        if (map[i] != 0x05) continue;  // Usage Page
        if (map[i + 1] == 0x07) s_has_keyboard_page = true;
        else if (map[i + 1] == 0x0C) s_has_consumer_page = true;
    }
    if (s_has_keyboard_page) s_preferred_byte = 2;
    else if (s_has_consumer_page) s_preferred_byte = 1;
}

// 取报告里的主码值：优先用报告映射指出的字节，否则扫前几字节取第一个非零。
// Extract the primary code: prefer the byte the report map pointed at, else the first non-zero
// byte within the first few.
static uint8_t extract_primary_code(const uint8_t *p, size_t n) {
    const size_t preferred = s_preferred_byte == 0xFF ? n : s_preferred_byte + (n == 9 ? 1u : 0u);
    // 已知布局的零键码是释放，不能把报告 ID 或修饰键误当按键。/ A known zero usage is release, not a report ID or modifier key.
    if (preferred < n) return p[preferred];
    const size_t limit = n < 8 ? n : 8;
    for (size_t i = 0; i < limit; ++i)
        if (p[i] != 0) return p[i];
    return 0;
}

// HID usage → 特殊键（US QWERTY）。只保留翻页用得到的部分。
// HID usage to special key (US QWERTY), trimmed to what page turning needs.
static ble_pt_key_t usage_to_special(uint8_t usage) {
    switch (usage) {
        case 0x28: case 0x58: return BLE_PT_KEY_ENTER;
        case 0x29: return BLE_PT_KEY_ESCAPE;
        case 0x2A: return BLE_PT_KEY_BACKSPACE;
        case 0x2B: return BLE_PT_KEY_TAB;
        case 0x4C: return BLE_PT_KEY_DELETE;
        case 0x4A: return BLE_PT_KEY_HOME;
        case 0x4B: return BLE_PT_KEY_PAGE_UP;
        case 0x4D: return BLE_PT_KEY_END;
        case 0x4E: return BLE_PT_KEY_PAGE_DOWN;
        case 0x4F: return BLE_PT_KEY_RIGHT;
        case 0x50: return BLE_PT_KEY_LEFT;
        case 0x51: return BLE_PT_KEY_DOWN;
        case 0x52: return BLE_PT_KEY_UP;
        default: return BLE_PT_KEY_NONE;
    }
}

static void emit_usage(uint8_t usage, uint8_t mods) {
    if (!usage) return;
    ble_pt_crumb(BLE_PT_CRUMB_EMIT);
    // 解出来的按键也上屏：和上面的 rx 行一起，就能分清"没收到数据"和"收到了但没解出来"。
    // A decoded key also reaches the screen: together with the rx line above it separates "no data
    // arrived" from "data arrived but nothing was decoded".
    const ble_pt_event_t ev = {.usage = usage, .mods = mods, .special = usage_to_special(usage)};
    s_held_usage = usage;
    s_held_mods = mods;
    s_held_since = (uint32_t)(esp_timer_get_time() / 1000);
    s_last_repeat = 0;
    s_repeat_count = 0;  // 新的一次按下重新计数 / a fresh press starts its own count
    key_push(&ev);
}

// 一帧报告 → 键事件 + 原始边沿。/ One report frame to key events plus raw edges.
static void ingest_report(const uint8_t *data, size_t len) {
    if (!data || !len || len > FRAME_MAX) return;
    ble_pt_crumb(BLE_PT_CRUMB_INGEST);
    const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    // 记下最近一次收到数据的时刻：按下状态靠它兜底超时（见 poll_locked）——
    // 对端不发「松开」那条全零报告时，s_held_usage 否则永远不会清。
    // Remember when data last arrived: the held state times out on it (see poll_locked), because
    // s_held_usage otherwise never clears when the peer never sends the all-zero release report.
    s_last_rx_ms = now;

    const uint8_t code = extract_primary_code(data, len);
    const uint8_t mods = s_has_keyboard_page ? data[len == 9 ? 1 : 0] : 0;

    // 静止帧取第一帧「什么都没按」的报告。/ The rest frame is the first report with nothing pressed.
    if (!s_rest_known && (!code || s_has_keyboard_page)) {
        s_rest = frame_of(data, len);
        if (code) {
            // 标准键盘无需等待先松键，首个按下也可用于学习。/ Standard keyboards can learn their first press without a prior release.
            // 只清按键区间，报告 ID、修饰键与保留字节都留着：它们是状态，不是按键。
            // 整帧清零的话，报告里每个非零常量都会被当成静止帧的一部分，反倒是那些常量成了
            // 静止帧——学习时按下的键与它一比就分出真假。
            // Only the key bytes are zeroed; the report id, the modifiers and the reserved byte
            // stay. They are state, not keys, and a fully zeroed frame would make every non-zero
            // constant part of the baseline, which is exactly what the resting frame must not do.
            const size_t first_key = s_has_keyboard_page ? (len == 9 ? 3 : 2) : 0;
            for (size_t i = first_key; i < len; ++i) s_rest.bytes[i] = 0;
            if (len == 9) s_rest.bytes[0] = data[0];
        }
        s_rest_known = true;
    }

    // 原始边沿先记：学习按键不该等一条「松开」报告，第一次按下就得算数。首帧就是按下时，
    // 键路径照旧在下面投递（那里的去抖会处理重复帧）。
    // Raw edges come first: learning a key must not wait for a release report, so the very first
    // press counts. When the first frame is already a press, the key path below still delivers it
    // (its debounce takes care of duplicate frames).
    raw_frame(data, len, now);

    if (code) {
        // 报告路径去抖：同一个键 500ms 内只投递一次。
        // 对端的报告帧在被按住期间可能持续变化（首字节在零与码值之间来回，或者报表里带着
        // 会变的字节），逐帧投递就成了每秒几十次翻页，一路把内存和看门狗逼到重启——
        // 现象就是"按一下就重启"，而且串口里能看到同一个键被反复上报。
        // 自动重复不走这里，所以按住连翻不受影响。
        // Debounce on the report path: one delivery per key per 500 ms. A peer's frames can keep
        // changing while the key is held (the first byte alternating between zero and the code, or
        // a field that keeps moving), and forwarding each frame becomes dozens of turns a second
        // until memory or the watchdog restarts the board -- the "one press and it restarts"
        // symptom, with the same key reported over and over. Auto-repeat does not come through
        // here, so holding to turn pages is unaffected.
        const bool changed = code != s_held_usage || mods != s_held_mods;
        const bool debounced = code == s_emit_usage && now - s_emit_ms < SAME_KEY_DEBOUNCE_MS;
        if (changed && !debounced) {
            s_emit_usage = code;
            s_emit_ms = now;
            emit_usage(code, mods);
        }
    } else s_held_usage = 0;  // 全零报告 = 所有键都松开了 / an all-released report
}

/* ---- 扫描 / Discovery ---- */

static int scan_cb(struct ble_gap_event *event, void *arg) {
    (void)arg;
    if (event->type != BLE_GAP_EVENT_DISC) return 0;
    const struct ble_gap_disc_desc *d = &event->disc;

    char addr[18];
    snprintf(addr, sizeof(addr), "%02x:%02x:%02x:%02x:%02x:%02x", d->addr.val[5], d->addr.val[4],
             d->addr.val[3], d->addr.val[2], d->addr.val[1], d->addr.val[0]);

    // 地址去重，但后续扫描响应仍可补齐名称，避免把有名设备永远隐藏。
    // Deduplicate addresses while allowing later scan responses to supply names for hidden entries.
    ble_pt_device_t *slot = NULL;
    for (uint8_t i = 0; i < s_device_count; ++i)
        if (!strcasecmp(s_devices[i].addr, addr)) { slot = &s_devices[i]; break; }
    if (!slot) {
        if (s_device_count >= BLE_PT_MAX_DEVICES) return 0;
        slot = &s_devices[s_device_count++];
        memset(slot, 0, sizeof(*slot));
        snprintf(slot->addr, sizeof(slot->addr), "%s", addr);
    }
    slot->rssi = d->rssi;
    slot->addr_type = d->addr.type;

    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, d->data, d->length_data) == 0) {
        if (fields.name && fields.name_len) {
            const size_t copy = fields.name_len < sizeof(slot->name) - 1 ? fields.name_len
                                                                        : sizeof(slot->name) - 1;
            memcpy(slot->name, fields.name, copy);
            slot->name[copy] = '\0';
            slot->has_name = true;
        }
        for (int i = 0; i < fields.num_uuids16; ++i)
            if (ble_uuid_u16(&fields.uuids16[i].u) == HID_SVC_UUID) slot->hid = true;
    }
    if (!slot->name[0]) snprintf(slot->name, sizeof(slot->name), "%s", addr);
    slot->bonded = bond_find(slot->addr) != NULL;
    return 0;
}

void ble_pt_scan_start(uint32_t ms) {
    if (!s_running || s_scanning || s_connected || s_connecting) return;
    s_device_count = 0;  // 重扫时清空，免得新旧混在一起 / clear on rescan so old and new do not mix
    // 扫描参数用协议栈默认值：曾经照 CrossMux 设成 itvl=window=160（100% 占空比），
    // 那也是可能被控制器以 HCI 0x12 拒掉的一条命令。先把连接能力拿回来。
    // Scan parameters stay at the stack defaults: setting itvl=window=160 for a full duty cycle,
    // as CrossMux does, is another command the controller can refuse with HCI 0x12. Restoring the
    // ability to connect comes first.
    const struct ble_gap_disc_params params = {
        .itvl = 0, .window = 0, .filter_duplicates = 0, .passive = 0,
    };
    if (ble_gap_disc(s_own_addr_type, (int32_t)ms, &params, scan_cb, NULL) == 0) {
        s_scanning = true;
    } else {
    }
}

void ble_pt_scan_stop(void) {
    if (!s_scanning) return;
    ble_gap_disc_cancel();
    // 这里**不**清 s_scanning：取消扫描是异步的，控制器还在扫的时候发起连接会直接被拒
    // （BLE_HS_EBUSY），界面只弹一句"连接失败请重试"，日志里什么都没有。
    // 扫描真正结束由 DISC_COMPLETE 落地，poll() 另有超时兜底。
    // Do NOT clear s_scanning here: the cancel is asynchronous, and starting a connection while
    // the controller still scans is refused outright (BLE_HS_EBUSY), which shows up as a bare
    // "connection failed, try again" with nothing at all in the log. The real end lands with
    // DISC_COMPLETE, with a timeout fallback in poll().
    s_scan_cancel_ms = (uint32_t)(esp_timer_get_time() / 1000);
}

bool ble_pt_scanning(void) { return s_scanning; }
uint8_t ble_pt_device_count(void) { return s_device_count; }

const ble_pt_device_t *ble_pt_device(uint8_t index) {
    return index < s_device_count ? &s_devices[index] : NULL;
}

// 按地址找扫描结果。地址比较不区分大小写：扫描回调写的是小写，已配对列表里的可能来自
// 别处。/ Look up a scan result by address, case-insensitively: the scan callback writes lower
// case while the bond list may hold another spelling.
static const ble_pt_device_t *device_find(const char *addr) {
    if (!addr) return NULL;
    for (uint8_t i = 0; i < s_device_count; ++i)
        if (!strcasecmp(s_devices[i].addr, addr)) return &s_devices[i];
    return NULL;
}

/* ---- 连接与发现 / Connection and discovery ---- */

static void note_peer(const char *addr, const char *name) {
    if (addr && addr[0]) snprintf(s_conn_addr, sizeof(s_conn_addr), "%s", addr);
    if (name && name[0]) {
        snprintf(s_conn_name, sizeof(s_conn_name), "%s", name);
    } else if (addr) {
        // 名字按可信度依次回退：扫描到的广播名 → 已配对时存下的名字 → 地址。
        // 扫描结果里本来就有广播名，之前没接上，所以连上以后只显示 MAC。
        // Fall back by confidence: the name seen while scanning, then the one stored when it was
        // bonded, then the address. The scan result already carries the advertised name; not
        // wiring it through is why a connected peer showed only its MAC.
        const ble_pt_device_t *dev = device_find(addr);
        const ble_pt_bond_t *bond = bond_find(addr);
        if (dev && dev->has_name) snprintf(s_conn_name, sizeof(s_conn_name), "%s", dev->name);
        else if (bond && bond->name[0]) snprintf(s_conn_name, sizeof(s_conn_name), "%s", bond->name);
        else snprintf(s_conn_name, sizeof(s_conn_name), "%s", addr);
    }
    bindings_load(s_conn_addr);
}

static void peer_addr_from_desc(const struct ble_gap_conn_desc *desc, char *out, size_t cap) {
    snprintf(out, cap, "%02x:%02x:%02x:%02x:%02x:%02x", desc->peer_id_addr.val[5],
             desc->peer_id_addr.val[4], desc->peer_id_addr.val[3], desc->peer_id_addr.val[2],
             desc->peer_id_addr.val[1], desc->peer_id_addr.val[0]);
}

// 链路一可用就记住这台设备。很多翻页器是明文 HID，从不配对，也就永远等不到加密事件，
// 于是界面显示「已连接」却不出现在已配对列表里，用户根本找不到它、也没法回连。
// 真正配对成功的设备走 ENC_CHANGE 那条路，地址类型同样会存下来。
// Remember a peer as soon as its link is usable. Plenty of remotes are plaintext HID that never
// pair, so the encryption event never arrives and the device stayed "connected" yet absent from
// the paired list, where the user could neither find nor reconnect it. A peer that does pair is
// recorded through ENC_CHANGE instead, with the same address type.
static void remember_live_peer(void) {
    // 每次链路只记一次：这个函数会被每条通知调用，而往下是 NVS 写。
    // Once per link: this is called from every notification, and an NVS write waits below.
    if (s_peer_remembered || !s_conn_addr[0]) return;
    s_peer_remembered = true;
    bond_add(s_conn_addr, s_conn_name, s_conn_addr_type);
}

// 订阅一个 HID 输入报告：在 CCCD 上写 0x0001（通知）。第一个订阅成功就算链路可用。
// Subscribe to one HID input report by writing 0x0001 (notify) to its CCCD. The link counts as
// usable once the first one succeeds.
static void dsc_next(void);
static void subscribe_next(void);

static int cccd_write_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                         struct ble_gatt_attr *attr, void *arg) {
    (void)conn_handle; (void)attr; (void)arg;
    if (error && error->status != 0) {
        ESP_LOGW(TAG, "CCCD write failed: %d", error->status);
        subscribe_next();  // 这一个不行就试下一个，别把订阅整条停住
        return 0;
    }
    s_subscribed_count++;
    remember_live_peer();
    if (!s_connected) {
        s_connected = true;
        s_connecting = false;
        ESP_LOGI(TAG, "connected to %s", s_conn_name);
    }
    // 上一个写完了才发下一个：见 subscribe_next 的说明。
    // The next write goes out only after this one is done; see subscribe_next.
    subscribe_next();
    return 0;
}

// 串行化：绝不从一个 ATT procedure 的回调里启动另一个 procedure。
// NimBLE 同一时刻只允许一个 procedure，嵌着发起的那个要么被丢弃，要么把正在跑的枚举
// 搅乱。上一版 chr_cb 里嵌 disc_all_dscs 和协议模式写、dsc_cb 里嵌 CCCD 写，都是这个毛病。
// Serialised: no ATT procedure is ever started from another's callback. NimBLE allows one at a
// time, so a nested call is dropped or derails the enumeration in flight. The previous version
// nested descriptor discovery and a protocol-mode write inside chr_cb, and a CCCD write inside
// dsc_cb.
static uint16_t s_pmode_handle;
// Report Map(0x2A4B) 的句柄：解码提示要从它读出来。
// Handle of the Report Map (0x2A4B), where the decoding hints come from.
static uint16_t s_report_map_handle;
static uint8_t s_dsc_index;
// 订阅进度：一次只发一个 CCCD 写。/ Subscription progress: one CCCD write at a time.
static uint8_t s_sub_index;
static unsigned s_sub_started;

// 订阅收尾：算上失败的那些，一个也没订上就说明链路虽好却没有可用的按键报告。
// Wrap-up: with none subscribed at all the link is fine but has no usable key report.
static void subscribe_finish(void) {
    ESP_LOGI(TAG, "subscribing %u of %u input report(s)", s_sub_started,
             (unsigned)s_input_chr_count);
    if (!s_sub_started && !s_input_chr_count) {
        if (!s_connected) {
            s_connected = true;
            s_connecting = false;
        }
    }
}

// 一次只发一个 CCCD 写，写完了（回调到了）再发下一个。
// 同时发几个会撞上 NimBLE 很小的 ATT procedure 池：proc alloc failed 之后它的状态不再一致，
// 紧接着就是 spinlock 断言重启——这正是"按一下就重启"的现场。
// One CCCD write at a time, the next going out only once this one's callback arrives. Issuing
// several at once runs into NimBLE's small ATT procedure pool: after proc alloc fails its state is
// no longer consistent, and the spinlock assert plus restart follow -- exactly the scene behind
// "one press and it restarts".
static void subscribe_next(void) {
    while (s_sub_index < s_input_chr_count) {
        const uint16_t cccd = s_input_chr_cccds[s_sub_index++];
        if (!cccd) continue;
        const uint8_t value[2] = {0x01, 0x00};  // 通知 / notify
        if (ble_gattc_write_flat(s_conn_handle, cccd, value, sizeof(value), cccd_write_cb,
                                 NULL) == 0) {
            ++s_sub_started;
            return;  // 等这条的回调再继续 / continue from this one's callback
        }
    }
    subscribe_finish();
}

// 找完所有输入报告的描述符之后才开始订阅。/ Subscribing starts only once every descriptor pass is done.
static void subscribe_ready(void) {
    ble_pt_crumb(BLE_PT_CRUMB_SUBSCRIBE);
    s_sub_index = 0;
    s_sub_started = 0;
    subscribe_next();
}

static void dsc_next(void);

static int dsc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                  uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc, void *arg) {
    (void)conn_handle; (void)chr_val_handle;
    const uint8_t index = (uint8_t)(uintptr_t)arg;
    switch (error->status) {
        case 0: {
            const ble_uuid16_t cccd = BLE_UUID16_INIT(HID_CHR_CCCD);
            const bool is_cccd = ble_uuid_cmp(&dsc->uuid.u, &cccd.u) == 0;
            if (is_cccd) s_input_chr_cccds[index] = dsc->handle;  // 只记录，不在这里订阅
            return 0;
        }
        case BLE_HS_EDONE:
            dsc_next();
            return 0;
        default:
            dsc_next();
            return 0;
    }
}

// 读 Report Map 拿解码提示：照 CrossMux 的 setupHid，先读 0x2A4B 再订阅。
// 这个回调以前根本不存在，于是 report_map_hints() 从未被调用，按键落在哪个字节上全靠猜，
// 遥控器按下去自然没有反应。
// Read the Report Map for decoding hints, the way CrossMux's setupHid does before subscribing.
// The callback simply did not exist before, so report_map_hints() was never called and which byte
// carried the key was pure guesswork -- which is why a press did nothing.
static int report_map_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                         struct ble_gatt_attr *attr, void *arg) {
    (void)conn_handle; (void)arg;
    if (error != NULL && error->status == 0 && attr != NULL && attr->om != NULL) {
        const uint8_t *data = OS_MBUF_DATA(attr->om, const uint8_t *);
        const uint16_t len = (uint16_t)OS_MBUF_PKTLEN(attr->om);
        report_map_hints(data, len);
    } else {
    }
    subscribe_ready();
    return 0;
}

// 一个报告接一个报告地找它的 CCCD。/ Walk the report characteristics one at a time.
static void dsc_next(void) {
    if (s_dsc_index >= s_input_chr_count) {
        // 订阅之前先把 Report Map 读出来，CrossMux 的次序就是这样。
        // The Report Map is read before subscribing, which is CrossMux's order.
        if (s_report_map_handle) {
            if (ble_gattc_read(s_conn_handle, s_report_map_handle, report_map_cb, NULL) == 0)
                return;
        }
        subscribe_ready();
        return;
    }
    const uint8_t i = s_dsc_index++;
    const int rc = ble_gattc_disc_all_dscs(s_conn_handle, s_input_chr_vals[i], s_hid_svc_end,
                                           dsc_cb, (void *)(uintptr_t)i);
    if (rc != 0) {
        // 起不来就跳过这一个，别把整条链卡死。
        // A pass that will not start is skipped rather than stalling the chain.
        dsc_next();
    }
}

static void chr_enum_done(void);

// 协议模式写完再去找描述符。/ Descriptor passes start once the protocol-mode write is done.
static int pmode_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                    struct ble_gatt_attr *attr, void *arg) {
    (void)conn_handle; (void)attr; (void)arg; (void)error;
    chr_enum_done();
    return 0;
}

static void chr_enum_done(void) {
    s_dsc_index = 0;
    if (s_pmode_handle) {
        const uint8_t report_protocol = 1;
        const int rc = ble_gattc_write_flat(s_conn_handle, s_pmode_handle, &report_protocol,
                                            sizeof(report_protocol), pmode_cb, NULL);
        if (rc == 0) return;  // 回调里接着走 / continue from the callback
    }
    dsc_next();
}

static int chr_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                  const struct ble_gatt_chr *chr, void *arg) {
    (void)conn_handle; (void)arg;
    switch (error->status) {
        case 0: {
            // Protocol Mode(0x2A4E)：只记下手柄，写操作等特征枚举结束后再做。
            // Protocol Mode (0x2A4E): record the handle only; the write waits until characteristic
            // enumeration has finished.
            const ble_uuid16_t pmode = BLE_UUID16_INIT(HID_CHR_PROTOCOL_MODE);
            if (ble_uuid_cmp(&chr->uuid.u, &pmode.u) == 0 &&
                (chr->properties & BLE_GATT_CHR_PROP_WRITE)) {
                s_pmode_handle = chr->val_handle;
                return 0;
            }
            const ble_uuid16_t report = BLE_UUID16_INIT(HID_CHR_REPORT);
            // Report Map(0x2A4B)：记下句柄，等描述符找完再读，读到的提示决定按键算在哪个字节。
            // Report Map (0x2A4B): keep the handle and read it after the descriptor passes; what it
            // says decides which byte holds the key.
            const ble_uuid16_t rmap = BLE_UUID16_INIT(HID_CHR_REPORT_MAP);
            if (ble_uuid_cmp(&chr->uuid.u, &rmap.u) == 0 &&
                (chr->properties & BLE_GATT_CHR_PROP_READ)) {
                s_report_map_handle = chr->val_handle;
                return 0;
            }
            // Boot Keyboard Input(0x2A22) 也要订阅：有些翻页器只暴露这一个输入特征，
            // 只认 Report 特征时就会出现"连上了、配对也在，按键一个都进不来"。
            // Boot Keyboard Input (0x2A22) must be subscribed too: some remotes expose only that
            // one input characteristic, and accepting Report alone leaves the link up and paired
            // with no key ever arriving.
            const ble_uuid16_t boot_kbd = BLE_UUID16_INIT(HID_CHR_BOOT_KBD_INPUT);
            const bool is_report = ble_uuid_cmp(&chr->uuid.u, &report.u) == 0;
            const bool is_boot_kbd = ble_uuid_cmp(&chr->uuid.u, &boot_kbd.u) == 0;
            if ((is_report || is_boot_kbd) && (chr->properties & BLE_GATT_CHR_PROP_NOTIFY) &&
                s_input_chr_count < MAX_INPUT_CHRS) {
                const uint8_t index = s_input_chr_count++;
                s_input_chr_vals[index] = chr->val_handle;
                s_input_chr_cccds[index] = 0;
            }
            return 0;
        }
        case BLE_HS_EDONE:
            chr_enum_done();
            return 0;
        default:
            return 0;
    }
}


// 照 CrossMux 的顺序：连上先做「全量服务发现」，再从结果里挑出 HID 服务。
// NimBLE-Arduino 的 connect() 内部就是先发现全部服务，而按 UUID 定向查服务在这台翻页器上
// 回调根本不来（日志里 disc_svc_by_uuid rc=0 之后一片空白）。这里也不从别的 procedure 的
// 回调里启动新 procedure —— 那是之前另一种失败方式。
// CrossMux's order: discover every service first, then pick the HID one out of the result.
// NimBLE-Arduino's connect() discovers all services up front, whereas a UUID-filtered discovery
// never called back on this peripheral. No procedure is started from another's callback either.
static bool s_hid_found;
static uint16_t s_hid_svc_start = 0xFFFF;
// 发现开始的时刻：卡住不动时用它超时重试，而不是永久等一个不会来的回调。
// When discovery started, so a callback that never comes can be retried instead of waited on
// forever.
static uint32_t s_discovery_ms;

static int svc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                  const struct ble_gatt_svc *svc, void *arg) {
    (void)arg;
    switch (error->status) {
        case 0: {
            const ble_uuid16_t hid = BLE_UUID16_INIT(HID_SVC_UUID);
            if (ble_uuid_cmp(&svc->uuid.u, &hid.u) == 0) {
                s_hid_found = true;
                s_hid_svc_start = svc->start_handle;
                s_hid_svc_end = svc->end_handle;
            } else {
            }
            return 0;
        }
        case BLE_HS_EDONE: {
            // 发现到此结束（有没有 HID 都算结束）：由状态机决定是继续还是断开并写明原因。
            // Discovery is over whether or not HID turned up; the state machine decides whether to
            // continue or to drop the link with a reason.
            s_discovery_done = true;
            if (!s_hid_found) return 0;
            const int rc = ble_gattc_disc_all_chrs(conn_handle, s_hid_svc_start, s_hid_svc_end,
                                                   chr_cb, NULL);
            if (rc != 0) {
                // 特征枚举没起来：退回"未开始"，由 poll() 的超时重跑一遍，别停在这里。
                // The characteristic pass would not start: fall back to "not started" and let the
                // poll timeout run it again instead of stopping here.
                s_discovery_started = false;
                s_discovery_ms = 0;
            }
            return 0;
        }
        default:
            return 0;
    }
}

// 开始服务发现。返回 false 表示控制器拒绝了这次 ATT 操作（多半是加密还没完成），
// 调用方保持"未开始"状态稍后重试——这正是"一直正在连接"的死锁来源：
// 旧代码先置 s_discovery_started 再调用、且不看返回值，被拒之后谁也不重试。
// Start service discovery. False means the controller refused the ATT operation (usually because
// encryption is still in flight); the caller keeps the "not started" state and retries. That
// refusal was the deadlock behind a page stuck on "connecting" forever: the old code set
// s_discovery_started before the call and never looked at the result, so nothing ever retried.
static bool start_discovery(void) {
    const int rc = ble_gattc_disc_all_svcs(s_conn_handle, svc_cb, NULL);
    if (rc != 0) {
        return false;
    }
    s_discovery_started = true;
    s_discovery_ms = (uint32_t)(esp_timer_get_time() / 1000);
    return true;
}

// 已用另一种对端地址类型重试过几次。地址类型猜错是"部分设备连不上"最常见的来由，
// 而地址在字面上分不出 public 还是 random，CrossMux 的做法就是失败后换一种再试。
// How many times the alternate peer address type has been tried. Guessing the address type wrong
// is the most common reason "some remotes will not connect", and the two are indistinguishable by
// eye, so CrossMux's move is to try the other one after a failure.
static uint8_t s_addr_type_alt_tries;
static int start_connect(const char* addr, uint8_t type);

/* ---- 建立链路：照 CrossMux 的步骤 / Bring-up, CrossMux's steps ---- */

// CrossMux 在专属任务里按 connect → 确认有 HID → 配对 → 订阅 的顺序推进，每一步失败就断开重来。
// 这里用同一套顺序和同样的判定，只把"阻塞等待"换成 poll() 逐轮推进并带超时：
// 超时或失败都断开链路、写明原因、在屏幕上留一行，绝不会再出现"永远正在连接、也没有解释"。
// CrossMux walks connect, confirm HID, pair, subscribe in a dedicated task and drops the link on
// any failure. This keeps that order and those verdicts, replacing the blocking waits with a
// tick-driven advance that carries timeouts: a stall or a failure drops the link, records why and
// leaves a line on screen, so "connecting forever with no explanation" cannot happen again.
typedef enum {
    STEP_IDLE = 0,
    STEP_LINK,      // 等链路建立 / waiting for the link
    STEP_DISCOVER,  // 服务发现，确认对端有 HID / service discovery, confirm the peer has HID
    STEP_SECURE,    // 配对与加密；明文设备超时即放行 / pairing; a plaintext peer passes on timeout
    STEP_SETUP,     // 协议模式、描述符、订阅 / protocol mode, descriptors, subscribe
    STEP_READY,     // 链路可用 / the link is usable
} step_t;

static step_t s_step;
static uint32_t s_step_at;
static bool s_alt_type_tried;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void step_begin(step_t step) {
    s_step = step;
    s_step_at = now_ms();
}

static bool step_over(uint32_t limit_ms) { return now_ms() - s_step_at >= limit_ms; }

// 断开并写明原因：界面上要能读到"哪一步失败、为什么"。
// Drop the link and record why: the screen has to say which step failed and why.
static void step_fail(const char *why) {
    ble_pt_crumb(BLE_PT_CRUMB_FAIL);
    snprintf(s_failure, sizeof(s_failure), "%s", why);
    s_step = STEP_IDLE;
    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(s_conn_handle, 0x13);
}

// 推进一键建立链路。由 poll() 每轮调用一次。
// Advance the bring-up by one tick; poll() calls this every iteration.
static void step_poll(void) {
    switch (s_step) {
        case STEP_LINK:
            if (s_link_up) {
                ble_pt_crumb(BLE_PT_CRUMB_LINK);
                step_begin(STEP_DISCOVER);
            } else if (!s_connecting || step_over(10000)) {
                // 连接失败事件到了就立刻处理，不用干等满 10 秒；超时也走同一条路。
                // A connect-failure event is handled the moment it lands instead of waiting out the
                // full 10 s, and a timeout takes the same route.
                // 换另一种地址类型再试一次，和 CrossMux 一样。
                // Retry with the other address type, exactly as CrossMux does.
                if (!s_alt_type_tried && s_conn_addr[0]) {
                    s_alt_type_tried = true;
                    const uint8_t alt = s_conn_addr_type == BLE_ADDR_PUBLIC ? BLE_ADDR_RANDOM
                                                                           : BLE_ADDR_PUBLIC;
                    if (start_connect(s_conn_addr, alt) == 0) {
                        step_begin(STEP_LINK);
                        break;
                    }
                }
                step_fail("连接失败");
            }
            break;

        case STEP_DISCOVER:
            // 加密还在进行时 ATT 会被拒，先等一小会儿；被拒就下一轮再试（start_discovery
            // 失败时不置"已开始"）。
            // ATT is refused while encryption is in flight, so wait a moment and retry on refusal
            // (start_discovery leaves the flag clear when it is refused).
            if (!s_discovery_started && !s_discovery_done && step_over(2500)) {
                ble_pt_crumb(BLE_PT_CRUMB_DISCOVER);
                start_discovery();
            }
            if (s_discovery_done) {
                if (!s_hid_found) step_fail("不是 HID 设备");
                else {
                    ble_pt_crumb(BLE_PT_CRUMB_HID);
                    step_begin(STEP_SECURE);
                }
            } else if (step_over(12000)) {
                step_fail("服务发现超时");
            }
            break;

        case STEP_SECURE:
            if (s_enc_done) {
                ble_pt_crumb(BLE_PT_CRUMB_PAIR);
                step_begin(STEP_SETUP);
            } else if (step_over(6000)) {
                // 明文 HID 不发加密事件：放行，但不能一直等。
                // A plaintext HID peer never reports encryption: move on, but do not wait forever.
                ble_pt_crumb(BLE_PT_CRUMB_PAIR);
                step_begin(STEP_SETUP);
            }
            break;

        case STEP_SETUP:
            if (s_connected) {
                ble_pt_crumb(BLE_PT_CRUMB_READY);
                step_begin(STEP_READY);
            } else if (step_over(8000)) {
                step_fail("没有可订阅的输入报告");
            }
            break;

        case STEP_IDLE:
        case STEP_READY:
        default:
            break;
    }
}

/* ---- 建立链路的记录 / Bring-up trail ---- */

// 每一步留一行给崩溃日志，存在 RTC 里、跨软复位保留：崩溃后重启，屏幕上的「上次运行」区
// 列出的就是这一串事件。这台设备没有随手可用的串口，链路建立到哪一步只能这样留下来。
// Every step leaves a line in the crash log, kept in RTC across a soft reset: after a crash the
// "last run" block on screen lists exactly this trail. This board has no convenient console, and
// how far the bring-up got has nowhere else to be remembered.
static const char *crumb_name(uint8_t id) {
    switch (id) {
        case BLE_PT_CRUMB_CONNECT: return "connect";
        case BLE_PT_CRUMB_LINK: return "link up";
        case BLE_PT_CRUMB_DISCOVER: return "discover";
        case BLE_PT_CRUMB_HID: return "HID ok";
        case BLE_PT_CRUMB_PAIR: return "paired";
        case BLE_PT_CRUMB_SUBSCRIBE: return "subscribe";
        case BLE_PT_CRUMB_READY: return "ready";
        case BLE_PT_CRUMB_FAIL: return "FAIL";
        case BLE_PT_CRUMB_RX: return "rx";
        case BLE_PT_CRUMB_INGEST: return "ingest";
        case BLE_PT_CRUMB_EMIT: return "emit";
        case BLE_PT_CRUMB_KEY_PUSH: return "push";
        case BLE_PT_CRUMB_POP: return "pop";
        default: return "?";
    }
}

void ble_pt_crumb(uint8_t id) { rp_crash_log("%s", crumb_name(id)); }

static int gap_cb(struct ble_gap_event *event, void *arg) {
    (void)arg;
    switch (event->type) {
        case BLE_GAP_EVENT_DISC_COMPLETE:
            s_scanning = false;
            s_scan_cancel_ms = 0;
            return 0;

        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status != 0) {
                s_connecting = false;
                // 换不换地址类型、要不要放弃，都交给状态机：这里只负责把结果报出去。
                // Whether to retry another address type or give up belongs to the state machine;
                // this only reports the outcome.
                snprintf(s_failure, sizeof(s_failure), "连接失败 (%d)", event->connect.status);
                return 0;
            }
            s_conn_handle = event->connect.conn_handle;
            s_link_up = true;
            s_connecting = false;  // 链路已建立，"连接中"到此为止 / the link exists, so stop saying "connecting"
            // 链路建立、加密、发现、订阅各打一行：带上时间戳就能看出卡在哪一步。
            // One line each for link up, encryption, discovery and subscription: with timestamps
            // it is obvious which step stalls.
            // 上一个对端留下的句柄不能带进这一次：协议模式句柄写错会打到别的特征上。
            // Handles from the previous peer must not leak into this one: a stale protocol-mode
            // handle would write into some other characteristic.
            s_pmode_handle = 0;
            s_report_map_handle = 0;
            s_peer_remembered = false;
            s_hid_found = false;
            s_hid_svc_start = 0xFFFF;
            s_discovery_started = false;
            s_input_chr_count = 0;
            s_subscribed_count = 0;
            for (uint8_t i = 0; i < MAX_INPUT_CHRS; ++i) s_input_chr_cccds[i] = 0;
            // 顺序照 CrossMux：连接 → 确认服务 → 配对 → setup。这里只发起配对请求，
            // 发现由状态机在 STEP_DISCOVER 里发起（加密未完成时 ATT 会被拒）。
            // CrossMux's order: connect, confirm the service, pair, then set up. This only starts
            // the pairing; the state machine runs discovery in STEP_DISCOVER, since ATT is refused
            // while encryption is still in flight.
            s_connect_ms = (uint32_t)(esp_timer_get_time() / 1000);
            s_enc_done = false;
            s_discovery_done = false;
            // 明文设备不会回这个事件，加密步骤靠状态机的超时放行。
            // A plaintext peer never answers this, and the state machine's timeout lets it pass.
            (void)ble_gap_security_initiate(s_conn_handle);
            return 0;

        case BLE_GAP_EVENT_CONN_UPDATE_REQ: {
            // 只把监督超时保底到 8 秒，其余参数随对端：全盘改写可能被对端拒绝而断链，
            // 而不设保底时对端一旦换成默认的 2.56 秒，主机这边的长阻塞就会掉线。
            // Floor the supervision timeout at 8 s and leave the rest to the peer: rewriting the
            // whole set risks a refusal that drops the link, while no floor at all means the peer's
            // 2.56 s default cannot survive a long block on the host.
            struct ble_gap_upd_params* ours = event->conn_update_req.self_params;
            if (ours != NULL && ours->supervision_timeout < 800) ours->supervision_timeout = 800;
            return 0;
        }

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "disconnected: %d", event->disconnect.reason);
            s_connected = false;
            s_connecting = false;
            s_link_up = false;
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            s_held_usage = 0;
            s_rest_known = false;
            raw_reset();
            s_subscribed_count = 0;
            s_input_chr_count = 0;
            s_discovery_started = false;
            return 0;

        case BLE_GAP_EVENT_ENC_CHANGE: {
            s_enc_done = true;  // 加密这一步到此为止，成功失败都算结束 / the security step is over either way
            if (event->enc_change.status != 0) {
                return 0;
            }
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0) {
                char addr[18];
                peer_addr_from_desc(&desc, addr, sizeof(addr));
                note_peer(addr, NULL);
                // 链路真的通了才记下自动重连目标。/ Only arm auto-reconnect on a live link.
                snprintf(s_reconnect_addr, sizeof(s_reconnect_addr), "%s", addr);
                // 配对完成才落到已配对列表里，这样列表只反映真正可自动重连的设备。
                // Only a completed pairing enters the bond list, so it reflects peers that can
                // actually auto-reconnect.
                if (desc.sec_state.bonded) bond_add(addr, s_conn_name, desc.peer_id_addr.type);
            }
            // 加密完成才做 GATT 发现，顺序同 CrossMux：connect → 确认服务 → secure → setup。
            // GATT discovery only after encryption completes, matching CrossMux's order.
            if (!s_discovery_started) start_discovery();
            return 0;
        }

        case BLE_GAP_EVENT_PASSKEY_ACTION: {
            // 外设要显示配对码：存下来给界面显示，并回一个「已确认」。
            // The peer wants a passkey shown: stash it for the UI and acknowledge.
            if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
                const uint32_t passkey = (uint32_t)(esp_timer_get_time() % 1000000u);
                struct ble_sm_io io = {.action = BLE_SM_IOACT_DISP, .passkey = passkey};
                if (ble_sm_inject_io(event->passkey.conn_handle, &io) == 0) {
                    s_passkey = passkey;
                    s_passkey_ready = true;
                }
            } else if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
                // 双方各自显示一串数字让用户核对：翻页器上没有可点的确认，直接接受。
                // Both sides show a number for the user to compare; a remote has nothing to tap,
                // so accept it.
                struct ble_sm_io io = {.action = BLE_SM_IOACT_NUMCMP, .numcmp_accept = 1};
                ble_sm_inject_io(event->passkey.conn_handle, &io);
            } else {
                // 其余动作（要求我们输入对端显示的码）这台设备没有键盘，只能记下来。
                // The remaining action wants a code typed in, and this device has no keypad, so
                // record it instead.
            }
            return 0;
        }

        case BLE_GAP_EVENT_NOTIFY_RX: {
            ble_pt_crumb(BLE_PT_CRUMB_RX);
            const uint8_t *body = OS_MBUF_DATA(event->notify_rx.om, const uint8_t *);
            const uint16_t len = (uint16_t)OS_MBUF_PKTLEN(event->notify_rx.om);
            // 收到数据就上屏：按键没反应时，第一步要分清"对端根本没发"还是"发了没解出来"。
            // A received report goes on screen: when a press does nothing, the first question is
            // whether the peer sent anything at all or sent something undecodable.
            if (len > 0)
            // 每一条通知都记下原始字节：遥控器到底发了什么、是不是空报告，一眼可见。
            // Every notification records its raw bytes, so what the remote actually sent -- and
            // whether it sent an empty report -- is right there.
            {
                char hex[3 * 24 + 1] = {0};
                const uint16_t shown = len < 24 ? len : 24;
                for (uint16_t i = 0; i < shown; ++i)
                    snprintf(hex + i * 3, 4, "%02x ", body[i]);
            }
            // 收到报告本身就是"链路可用"最可靠的证据：CCCD 订阅可能因为链路未加密而写失败，
            // 但已配对设备的订阅状态由对端保留，翻页器照样会发。之前拿"订阅写成功"当判据，
            // 于是能翻页却一直显示「连接中」，「学习」也因此说你没连。
            // A report arriving is the most reliable proof the link is usable: the CCCD subscribe
            // can fail while the link is unencrypted, yet a bonded peer keeps the subscription on
            // its side and notifies anyway. Keying "connected" off a successful subscribe is why
            // pages turned while the screen said "connecting", and why 学习 claimed no connection.
            if (!s_connected) {
                s_connected = true;
                s_connecting = false;
            }
            // 已配对设备保留着订阅，报告会先于我们的订阅到达，这条同样算链路可用。
            // A bonded peer keeps the subscription on its side, so a report can arrive before our
            // own subscribe; this path proves the link just as well.
            remember_live_peer();
            ingest_report(body, len);
            return 0;
        }

        default:
            return 0;
    }
}

// 按给定地址类型发起连接；成功返回 0，失败返回 NimBLE 的错误码。
// Start a connection with the given peer address type; 0 on success, else NimBLE's error code.
static int start_connect(const char *addr, uint8_t type) {
    unsigned v[6];
    if (sscanf(addr, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6)
        return BLE_HS_EINVAL;
    ble_addr_t peer = {0};
    for (int i = 0; i < 6; ++i) peer.val[5 - i] = (uint8_t)v[i];
    peer.type = type;
    // 连接参数交回协议栈默认值。曾经照 CrossMux 显式指定 itvl/latency/监督超时，
    // 结果控制器以 HCI 0x12（Invalid HCI Command Parameters，NimBLE 报 530）拒绝了整次连接：
    // 那套值在 NimBLE-Arduino 上合法，到了这里的原生 API 与控制器组合就不是。
    // 先要连得上，再谈 8 秒监督超时那样的加固。
    // Connection parameters go back to the stack defaults. Specifying interval, latency and
    // supervision timeout the CrossMux way made the controller refuse the whole attempt with HCI
    // 0x12 (Invalid HCI Command Parameters, NimBLE reports 530): those values are valid through
    // NimBLE-Arduino but not through this native API on this controller. Connecting comes first;
    // hardening such as an 8 s supervision timeout can follow.
    s_connecting = true;
    s_rest_known = false;
    raw_reset();
    s_has_keyboard_page = s_has_consumer_page = false;
    s_preferred_byte = 0xFF;
    s_held_usage = 0;
    s_subscribed_count = 0;
    s_conn_addr_type = type;
    const int rc = ble_gap_connect(s_own_addr_type, &peer, 10000, NULL, gap_cb, NULL);
    if (rc != 0) {
        s_connecting = false;
        // 以前这里只写 s_failure：界面弹"连接失败请重试"，串口却一个字都没有，
        // 谁也看不出是扫描没停（EBUSY）还是参数非法。
        // This used to write s_failure alone: the UI said "connection failed, try again" while the
        // log stayed silent, leaving no way to tell a scan that had not stopped (EBUSY) from an
        // invalid parameter.
    }
    return rc;
}

esp_err_t ble_pt_connect(const char *addr) {
    if (!s_running || !addr || !addr[0]) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_link_up || s_connecting) {
        // 想换一台：先记下目标、断掉现有的，等 DISCONNECT 事件到了 poll() 再连。
        // 直接在这里发起会撞上还没落地的旧链路，返回 ESP_ERR_INVALID_STATE —— 表现就是
        // 点了没反应或者连不上。
        // Switching peer: record the target, drop the current link, and let poll() connect once
        // the DISCONNECT event lands. Starting here would race the closing link and fail with
        // ESP_ERR_INVALID_STATE, which shows up as "tapping it does nothing".
        snprintf(s_pending_addr, sizeof(s_pending_addr), "%s", addr);
        ble_pt_disconnect();
        return ESP_OK;
    }
    if (s_scanning) {
        // 扫描还没停：先请求取消再立刻连接，控制器若还在扫会回 EBUSY，那时交给 poll() 稍后重试。
        // 不在这里排队等 DISC_COMPLETE——事件一旦丢失，连接就永远排不上队。
        // The scan is still up: ask to cancel and connect right away. If the controller is still
        // scanning it answers EBUSY and poll() retries shortly after. Waiting here for
        // DISC_COMPLETE would strand the connect for good if that event never lands.
        ble_pt_scan_stop();
    }

    // 地址类型按可信度取：先用扫描结果里看到的（每台设备都带着自己的类型），
    // 再退到已配对记录，最后才假设公开地址。
    // Address type by trust: the scan result first (every device carries its own type), then the
    // bond record, and only then the public fallback.
    const ble_pt_device_t *dev = device_find(addr);
    const ble_pt_bond_t *bond = bond_find(addr);
    const uint8_t type = dev ? dev->addr_type : bond ? (uint8_t)bond->addr_type : BLE_ADDR_PUBLIC;

    // 重新记一次重试计数：这是用户点的一次全新连接。
    // Reset the retry counter: this is a fresh connect from the user.
    s_addr_type_alt_tries = 0;
    s_alt_type_tried = false;
    note_peer(addr, NULL);
    const int rc = start_connect(addr, type);
    if (rc != 0) {
        if (rc == BLE_HS_EBUSY) {
            // 控制器还忙着（扫描取消没落地）：记下目标，poll() 下一轮重来。
            // The controller is still busy with the scan cancel: record the target and let poll()
            // try again on the next tick.
            snprintf(s_pending_addr, sizeof(s_pending_addr), "%s", addr);
            s_pending_at = (uint32_t)(esp_timer_get_time() / 1000);
            return ESP_OK;
        }
        snprintf(s_failure, sizeof(s_failure), "发起连接失败 (%d)", rc);
        return ESP_FAIL;
    }
    // 链路由状态机按 CrossMux 的顺序推进：等链路 → 确认 HID → 配对 → 订阅。
    // The state machine walks the link the CrossMux way: wait for it, confirm HID, pair, subscribe.
    step_begin(STEP_LINK);
    ble_pt_crumb(BLE_PT_CRUMB_CONNECT);
    return ESP_OK;
}

void ble_pt_disconnect(void) {
    // 记下是谁在断链路：日志里 reason=534 看着像"本地断开"，但调用点不明会误导排查。
    // Record who terminates: reason=534 looks like a local termination, and not knowing the
    // caller would send the next round of debugging the wrong way.
    s_reconnect_addr[0] = 0;
    s_step = STEP_IDLE;  // 用户主动断开：建立流程就地结束 / an explicit disconnect ends the bring-up here
    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(s_conn_handle, 0x13);
}

// 界面上的"已连接"就是"链路建立了"，与我们的 HID 订阅是否成功无关。以前这里报的是
// "订阅可用"，而订阅可能失败、翻页器却照旧发通知，于是页面永远停在「连接中」。
// "Connected" in the UI means the link exists, regardless of whether our HID subscription
// succeeded. This used to report "subscription usable", and since a subscription can fail while
// the remote keeps notifying, the page sat on "connecting" forever.
bool ble_pt_connected(void) { return s_link_up; }
bool ble_pt_connecting(void) { return s_connecting && !s_link_up; }
const char *ble_pt_connected_name(void) { return s_conn_name; }

bool ble_pt_take_failure(char *out, size_t cap) {
    if (!s_failure[0]) return false;
    if (out && cap) snprintf(out, cap, "%s", s_failure);
    s_failure[0] = 0;
    return true;
}

bool ble_pt_take_passkey(uint32_t *out) {
    if (!s_passkey_ready) return false;
    if (out) *out = s_passkey;
    s_passkey_ready = false;
    return true;
}

/* ---- 生命周期 / Lifecycle ---- */

static void on_sync(void) {
    if (ble_hs_util_ensure_addr(0) != 0) {
        ESP_LOGE(TAG, "no usable address");
        return;
    }
    ble_hs_id_infer_auto(0, &s_own_addr_type);
    // 推流接收端与翻页器共用这套栈：GATT 服务在这里注册，待武装/中断的广播
    // 也在这里续上。/ The push sink shares this stack: its GATT services are
    // registered here, and a pending or interrupted advertisement resumes here.
    push_sink_on_stack_sync();
}

static void host_task(void *param) {
    (void)param;
    nimble_port_run();
    s_host_task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t start_locked(void) {
    if (atomic_load(&s_network_users)) return ESP_ERR_INVALID_STATE;
    if (s_running) return ESP_OK;

    // 内存闸门：NimBLE 的 host 动态分配走 PSRAM（CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL），
    // 但控制器与协议栈任务必须落在内部 RAM——它跑在硬件路径上，读不到 PSRAM。所以卡住的
    // 是内部 RAM，不是 PSRAM。不够就当场拒绝，绝不进到一半再崩。
    // Memory gate. NimBLE's host allocations go to PSRAM
    // (CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL), but the controller and the stack task must live
    // in internal RAM - they sit on the hardware path and cannot read PSRAM. So the binding
    // constraint is internal RAM, not PSRAM. Refuse outright rather than half-initialise.
    const size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t largest_internal =
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (free_internal < BLE_PT_MIN_FREE_INTERNAL ||
        largest_internal < BLE_PT_MIN_LARGEST_INTERNAL || free_psram < BLE_PT_MIN_FREE_PSRAM) {
        ESP_LOGW(TAG, "not enough memory: internal %u B free / %u B largest, psram %u B free",
                 (unsigned)free_internal, (unsigned)largest_internal, (unsigned)free_psram);
        snprintf(s_failure, sizeof(s_failure), "内存不足（内部 %uK，最大块 %uK）",
                 (unsigned)(free_internal / 1024), (unsigned)(largest_internal / 1024));
        return ESP_ERR_NO_MEM;
    }

    const esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init: %s", esp_err_to_name(err));
        return err;
    }

    ble_hs_cfg.sync_cb = on_sync;
    // 与 CrossMux 的 BleKeyboardHost 对齐：Just Works + LE Legacy 配对。
    // 要求 Secure Connections（sc=1）时，不支持 SC 的老翻页器会直接以认证失败收场，
    // 表现正是"配对表里有它、按键一个都进不来"。
    // Aligned with CrossMux's BleKeyboardHost: Just Works over LE Legacy pairing. Demanding
    // Secure Connections (sc=1) makes an older remote that lacks SC fail authentication outright,
    // which looks exactly like "it is in the paired list and no key ever arrives".
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_YES_NO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 0;
    // 只分发加密密钥，与参考实现一致；多要 ID 密钥（IRK）会让部分对端在配对上挑刺。
    // Distribute the encryption key only, as the reference does; asking for the identity key as
    // well makes some peers fuss during pairing.
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC;
    // store 由 IDF 在 nimble_port_init() 内部初始化（ble_store_config_init 是内部符号，
    // 公开头里没有声明），bond 的持久化由 CONFIG_BT_NIMBLE_NVS_PERSIST 决定。
    // The store is initialised by IDF inside nimble_port_init() (ble_store_config_init is an
    // internal symbol with no public declaration); bond persistence is decided by
    // CONFIG_BT_NIMBLE_NVS_PERSIST.

    bonds_load();
    // 常驻注册推流 GATT 服务：必须排在 host 任务创建之前——NimBLE 的
    // ble_hs_start()（host 启动路径，ble_hs.c）在 ble_hs_sync() 触发 sync 回调
    // 之前就一次性调用 ble_gatts_start() 消费 add_svcs 的队列。排在 host 任务
    // 之后（旧代码排在 sync 回调里）等于白排：add_svcs 返回 0，属性表里却没有
    // 我们的服务，手机端表现为「能连上、Services 页空白」。失败不拦翻页器：
    // push_sink 的 sync 兜底会再试，日志留证。
    // Resident registration of the push GATT service: it must happen before the
    // host task is created - NimBLE's ble_hs_start() (host start path,
    // ble_hs.c) calls ble_gatts_start() once, before ble_hs_sync() fires the
    // sync callback, and that is what consumes the add_svcs queue. Queueing
    // after the host task (where the old code queued, inside the sync
    // callback) queues in vain: add_svcs returns 0 yet the attribute table
    // never holds our service - the phone-side symptom "connects fine,
    // Services page blank". Failure does not block the page-turner: push_sink's
    // sync fallback retries and the log keeps the evidence.
    if (push_sink_gatt_register() != ESP_OK) {
        ESP_LOGW(TAG, "push gatt register failed; sync fallback will retry");
    }
    // 检查任务分配结果，失败时完整回收，避免假启动及停止死等。/ Check task allocation and unwind failures instead of claiming a running host.
    if (xTaskCreatePinnedToCore(host_task, "nimble_host", CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE,
                               NULL, configMAX_PRIORITIES - 4, &s_host_task,
                               CONFIG_BT_NIMBLE_PINNED_TO_CORE) != pdPASS) {
        s_host_task = NULL;
        (void)nimble_port_deinit();
        return ESP_ERR_NO_MEM;
    }
    s_running = true;
    ESP_LOGI(TAG, "started, %u bond(s)", (unsigned)s_bond_count);
    // 配对策略摘要：日志里一眼就能看出这一版报的是什么能力、分发哪些密钥，
    // 免得对着一串失败码猜固件是哪一版。
    // Pairing policy summary: one line shows what capabilities this build advertises and which
    // keys it distributes, so a wall of failure codes never leaves the build in doubt.
    return ESP_OK;
}

static esp_err_t stop_locked(void) {
    if (!s_running) return ESP_OK;
    // 先告诉推流端：广播停掉、句柄作废，但 .part 与 NVS 会话留着，栈回来后续传。
    // Tell the push sink first: advertising stops and handles are invalidated, but
    // the partial file and NVS session survive for resume once the stack returns.
    push_sink_on_stack_stop();
    ble_pt_scan_stop();
    ble_pt_disconnect();
    // 让断开走完再拆 host；bond 留在 NVS，下次 start() 重新载入。
    // Let the disconnect settle before tearing the host down; bonds stay in NVS for the next start().
    vTaskDelay(pdMS_TO_TICKS(200));
    const int stopped = nimble_port_stop();
    if (stopped != 0) return ESP_FAIL;
    const esp_err_t error = nimble_port_deinit();
    if (error != ESP_OK) return error;
    s_running = false;
    s_connected = s_connecting = s_scanning = s_link_up = false;
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_stable_since = 0;
    s_held_usage = 0;
    portENTER_CRITICAL(&s_ring_lock);
    memset(&s_keys, 0, sizeof(s_keys));
    memset(&s_raws, 0, sizeof(s_raws));
    portEXIT_CRITICAL(&s_ring_lock);
    return ESP_OK;
}

esp_err_t ble_pt_start(void) {
    if (!lifecycle_take(2000)) return ESP_ERR_TIMEOUT;
    const esp_err_t error = start_locked();
    lifecycle_give();
    return error;
}
esp_err_t ble_pt_stop(uint32_t timeout_ms) {
    if (!lifecycle_take(timeout_ms > 2000 ? 2000 : timeout_ms)) return ESP_ERR_TIMEOUT;
    const esp_err_t error = stop_locked();
    lifecycle_give();
    return error;
}
esp_err_t ble_pt_network_acquire(void) {
    atomic_fetch_add(&s_network_users, 1);
    const esp_err_t error = ble_pt_stop(2000);
    if (error != ESP_OK) atomic_fetch_sub(&s_network_users, 1);
    return error;
}
void ble_pt_network_release(void) {
    unsigned users = atomic_load(&s_network_users);
    while (users && !atomic_compare_exchange_weak(&s_network_users, &users, users - 1)) {}
}

bool ble_pt_running(void) { return s_running; }

/* ---- 启动策略 / Start policy ---- */

// 连续失败计数落在 NVS：硬崩（panic 重启）时计数已经先写下去了，所以死循环能被自锁打断。
// The consecutive-failure count lives in NVS: it is written before the risky call, so a hard
// crash still leaves the evidence that breaks the loop.
static uint8_t boot_tries_load(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS_BLE_STATE, NVS_READONLY, &h) != ESP_OK) return 0;
    uint8_t tries = 0;
    nvs_get_u8(h, "tries", &tries);
    nvs_close(h);
    return tries;
}

static void boot_tries_store(uint8_t tries) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS_BLE_STATE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "tries", tries);
    nvs_commit(h);
    nvs_close(h);
}

// 失败原因也要落 NVS：它在 RAM 里的话，自锁后重启就只剩「失败 3 次」而看不到到底为什么，
// 那样这个自锁除了挡住用户什么用都没有。
// The failure reason is persisted too. Kept only in RAM it is gone after a reboot, leaving the
// self-lock showing "failed 3 times" with no cause, which blocks the user and explains nothing.
static void why_store(const char *why) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS_BLE_STATE, NVS_READWRITE, &h) != ESP_OK) return;
    if (why && why[0]) nvs_set_str(h, "why", why);
    else nvs_erase_key(h, "why");
    nvs_commit(h);
    nvs_close(h);
}

static bool why_load(char *out, size_t cap) {
    if (!out || !cap) return false;
    out[0] = 0;
    nvs_handle_t h;
    if (nvs_open(NVS_NS_BLE_STATE, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = cap;
    const bool ok = nvs_get_str(h, "why", out, &len) == ESP_OK && out[0];
    nvs_close(h);
    return ok;
}

// wanted 要稳定这么久才照做。/ The requested value must hold this long before acting on it.
#define BLE_PT_WANT_DEBOUNCE_MS 2000

void ble_pt_sync(bool wanted) {
    if (atomic_load(&s_network_users)) wanted = false;
    // 停用立即生效；仅重新启动防抖，网络不能等两秒再释放内存。/ Stop immediately; debounce only restarts so networking never waits two seconds for memory.
    if (!wanted && s_running) {
        if (ble_pt_stop(2000) != ESP_OK) return;
    }
    // 先防抖。主循环每轮都会问一次，而"要不要蓝牙"这个值来自 WiFi 状态；那个状态在
    // 启动/停止的过渡里会抖，照单全收就会把蓝牙栈反复拆了又建——既表现为连接不稳定，
    // 也会在链路还活着的时候把"已连接"标志清掉（通知照旧到达，于是翻页器还能翻页）。
    // Debounce first. The main loop asks every iteration and the answer depends on WiFi state,
    // which flickers while the transfer service starts or stops. Acting on every flicker tore the
    // stack down and rebuilt it repeatedly: that reads as an unstable link, and it cleared the
    // connected flag while a live link was still delivering notifications, so the remote kept
    // turning pages while the screen said it was not connected.
    static bool have_want;
    static bool steady_want;
    static uint32_t want_changed_at;
    const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    if (!have_want || wanted != steady_want) {
        if (wanted != steady_want) {
            steady_want = wanted;
            want_changed_at = now_ms;
            if (!have_want) {
                have_want = true;
            } else {
                return;  // 等它稳定下来 / wait for it to settle
            }
        }
        have_want = true;
    } else if (now_ms - want_changed_at < BLE_PT_WANT_DEBOUNCE_MS) {
        return;
    }

    if (!wanted) {
        if (s_running) ble_pt_stop(1000);
        s_retry_after_ms = 0;
        // 这里不清崩溃计数：WiFi 抢占射频时也会走到这个分支，清掉就等于把自锁削弱了。
        // 计数只在栈稳定跑满 BLE_PT_STABLE_MS 之后才清零。
        // The failure count is deliberately not cleared here: WiFi taking over the radio also
        // lands in this branch, and clearing would weaken the self-lock. It is only cleared once
        // the stack has run stably for BLE_PT_STABLE_MS.
        return;
    }
    if (s_running) {
        // 稳定跑够时间才认为这次启动是好的。/ Only a stack that stayed up counts as a good start.
        const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        if (!s_stable_since) s_stable_since = now;
        else if (now - s_stable_since >= BLE_PT_STABLE_MS && boot_tries_load()) {
            boot_tries_store(0);
        }
        return;
    }

    const uint8_t tries = boot_tries_load();
    char why[120];
    const bool have_why = why_load(why, sizeof(why));
    // 锁着、并且确实记下了原因，才真的停手并把原因显示出来。
    // 如果计数到了上限却没有原因（旧固件留下的锁），这里放行一次诊断尝试去把原因抓出来；
    // 否则用户面对的是一把既不说明原因、又解不开的锁。
    // Stop only when the count is exhausted *and* a cause was recorded. When the count is
    // exhausted with no cause (a lock left behind by an older build), let one diagnostic attempt
    // through to capture it; otherwise the user faces a lock that neither explains itself nor
    // can be released.
    if (tries >= BLE_PT_MAX_BOOT_TRIES && have_why) {
        snprintf(s_start_failure, sizeof(s_start_failure),
                 "启动失败 %u 次：%s（关掉再开启可重试）", (unsigned)tries, why);
        return;
    }

    const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (s_retry_after_ms && now < s_retry_after_ms) return;  // 退避中 / backing off

    // 先记账再动手：这一步之后硬崩，重启时计数已经加过了。
    // Record first, then act: a hard crash after this point still leaves the count raised.
    boot_tries_store((uint8_t)(tries + 1));
    const esp_err_t err = ble_pt_start();
    if (err == ESP_ERR_INVALID_STATE && atomic_load(&s_network_users)) {
        boot_tries_store(tries);
        return;
    }
    if (err != ESP_OK) {
        s_retry_after_ms = now + BLE_PT_RETRY_BACKOFF_MS;
        // 优先用 start() 写下的详细原因（里面有内存数字），只有它没写时才退回错误名。
        // Prefer the detailed reason start() recorded (it carries the memory figures) and fall
        // back to the bare error name only when there is none.
        if (s_failure[0]) {
            snprintf(s_start_failure, sizeof(s_start_failure), "%s", s_failure);
            s_failure[0] = 0;
        } else {
            snprintf(s_start_failure, sizeof(s_start_failure), "启动失败：%s", esp_err_to_name(err));
        }
        why_store(s_start_failure);
        ESP_LOGW(TAG, "start failed (%s), backing off %u ms", esp_err_to_name(err),
                 (unsigned)BLE_PT_RETRY_BACKOFF_MS);
    } else {
        s_retry_after_ms = 0;
        s_start_failure[0] = 0;
        why_store(NULL);
    }
}

bool ble_pt_take_start_failure(char *out, size_t cap) {
    if (!s_start_failure[0]) return false;
    if (out && cap) snprintf(out, cap, "%s", s_start_failure);
    s_start_failure[0] = 0;
    return true;
}

void ble_pt_reset_failure(void) {
    boot_tries_store(0);
    why_store(NULL);
    s_retry_after_ms = 0;
    s_start_failure[0] = 0;
    s_failure[0] = 0;
}

// 自动重复：按住 500ms 后开始，之后每 120ms 一次。
// Auto-repeat: begins 500 ms after the press, then one repeat every 120 ms.
#define REPEAT_DELAY_MS 500
#define REPEAT_PERIOD_MS 120

// 推流武装期让位（裁定 c）：翻页器链路就地断，但重连目标留着——退出武装后
// poll 的自动重连会把它接回来。特意不用 ble_pt_disconnect()：那个函数清掉
// s_reconnect_addr，退出武装后就再也回不来了。
// Yield to the push sink while armed (ruling c): the page-turner link drops in
// place but the reconnect target stays, so poll's auto-reconnect brings it back
// once the arm ends. ble_pt_disconnect() is deliberately avoided: it clears
// s_reconnect_addr and the remote would never return after the arm.
static void turner_yield_locked(void) {
    s_step = STEP_IDLE;  // 建立流程就地结束，退出武装后由重连逻辑重启 / end the bring-up here
    s_pending_addr[0] = 0;
    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGI(TAG, "yielding the link to a push");
        ble_gap_terminate(s_conn_handle, 0x13);
    }
}

static void poll_locked(void) {
    if (!s_running) return;

    // 推流武装期：整段 poll 让位。手机推流期间翻页器既不重连也不发包，
    // 射频整个归推流用。/ While armed for push: the whole poll yields. During a
    // phone push the page-turner neither reconnects nor sends, and the radio
    // belongs to the push alone.
    if (push_sink_turner_paused()) {
        if (s_link_up || s_connecting) turner_yield_locked();
        return;
    }

    // 自动重连：翻页器会自己省电断开，或走出范围。链路曾经建立过就记下目标，掉线后按
    // 退避重试。必须排除「正在扫描」——扫描页是持续扫描，两者同时抢控制器就会互相打架，
    // 表现就是连接不稳定（CrossMux 的同类判断里也有 !scanning_）。
    // Auto-reconnect: a remote drops the link to save power, or walks out of range. Once a link
    // has been up the target is remembered and retried with a back-off. Scanning must be excluded:
    // the scan page scans continuously, and letting both drive the controller at once makes them
    // fight, which shows up as an unstable connection (CrossMux guards with !scanning_ too).
    if (!s_link_up && !s_connecting && !s_scanning && s_reconnect_addr[0]) {
        const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        if (now - s_reconnect_at >= BLE_PT_RECONNECT_MS) {
            s_reconnect_at = now;
            char addr[18];
            snprintf(addr, sizeof(addr), "%s", s_reconnect_addr);
            if (ble_pt_connect(addr) == ESP_OK)
                ESP_LOGI(TAG, "reconnecting to %s", s_conn_name);
        }
    }

    // 建立链路：照 CrossMux 的 connect → 确认 HID → 配对 → 订阅，一步一步带超时推进。
    // 每一步的结论都写进屏显 trace，卡住或失败都会断开并写明原因。
    // Bring-up follows CrossMux's connect, confirm HID, pair, subscribe with a timeout per step.
    // Every step's verdict goes to the on-screen trace, and a stall or failure drops the link with
    // a recorded reason.
    step_poll();

    // 取消扫描迟迟没有 DISC_COMPLETE（控制器异常）时兜底清标志，否则再也连不上：
    // 连接会被 s_scanning 一直挡在队列里。
    // A cancel whose DISC_COMPLETE never lands would otherwise block every connect behind
    // s_scanning forever, so clear it on a timeout.
    if (s_scanning && s_scan_cancel_ms &&
        (uint32_t)(esp_timer_get_time() / 1000) - s_scan_cancel_ms > 500) {
        s_scanning = false;
        s_scan_cancel_ms = 0;
    }

    if (s_pending_addr[0] && !s_link_up && !s_connecting &&
        (s_pending_at == 0 ||
         (uint32_t)(esp_timer_get_time() / 1000) - s_pending_at >= 300)) {
        char addr[18];
        snprintf(addr, sizeof(addr), "%s", s_pending_addr);
        s_pending_addr[0] = 0;
        s_pending_at = (uint32_t)(esp_timer_get_time() / 1000);
        if (ble_pt_connect(addr) == ESP_OK) ESP_LOGI(TAG, "connecting to %s", s_conn_name);
    }

    // 原始边沿的收尾：遥控器不发松开报告时，静默够久就把这一次按下结束掉，下一次按下
    //（哪怕是同一个键）才算新的一次。放在这里是因为主循环每轮都调 poll()，而按下与松开之间
    // 可能隔很久、中间一条报告都没有。
    // Closing raw presses: when the remote sends no release report, enough silence ends the press so
    // the next one -- even of the same key -- counts as a new press. This lives here because the main
    // loop pumps poll() every iteration, while press and release can be far apart with no report at
    // all in between.
    {
        const uint32_t raw_now = (uint32_t)(esp_timer_get_time() / 1000);
        if (s_raw_down && raw_now - s_raw_last_ms > RAW_RELEASE_GAP_MS) raw_release();
    }

    const uint8_t usage = s_held_usage;
    if (!usage) return;
    const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    // 超时兜底要放在重复之前：报告断了就当作松开，别让自动重复没有终点。
    // The timeout is checked before any repeat: a silent link counts as released, so auto-repeat
    // can never run without an end.
    if (s_last_rx_ms && now - s_last_rx_ms > HELD_TIMEOUT_MS) {
        s_held_usage = 0;
        s_last_repeat = 0;
        return;
    }
    if (!s_last_repeat) {
        if (now - s_held_since >= REPEAT_DELAY_MS) s_last_repeat = now;
        return;
    }
    if (s_repeat_count >= REPEAT_MAX) {  // 重复有终点，失控也停得下来 / repeats end even if something runs away
        s_held_usage = 0;
        s_last_repeat = 0;
        return;
    }
    if (now - s_last_repeat >= REPEAT_PERIOD_MS) {
        s_last_repeat = now;
        ++s_repeat_count;
        emit_usage(usage, s_held_mods);
    }
}

void ble_pt_poll(void) {
    if (!lifecycle_take(0)) return;
    poll_locked();
    lifecycle_give();
}

/* ---- 输入 / Input ---- */

bool ble_pt_pop_key(ble_pt_event_t *out) {
    if (!out) return false;
    bool got = false;
    portENTER_CRITICAL(&s_ring_lock);
    if (s_keys.tail != s_keys.head) {
        *out = s_keys.items[s_keys.tail];
        s_keys.tail = (uint8_t)((s_keys.tail + 1) % KEY_RING_LEN);
        got = true;
    }
    portEXIT_CRITICAL(&s_ring_lock);
    // 面包屑放在临界区外：它带时间戳，没必要占着环形缓冲的锁。
    // The crumb stays outside the critical section: it carries a timestamp and has no business
    // holding the ring's lock.
    if (got) ble_pt_crumb(BLE_PT_CRUMB_POP);
    return got;
}

bool ble_pt_pop_raw(ble_pt_raw_t *out) {
    if (!out) return false;
    bool got = false;
    portENTER_CRITICAL(&s_ring_lock);
    if (s_raws.tail != s_raws.head) {
        *out = s_raws.items[s_raws.tail];
        s_raws.tail = (uint8_t)((s_raws.tail + 1) % RAW_RING_LEN);
        got = true;
    }
    portEXIT_CRITICAL(&s_ring_lock);
    return got;
}

/* ---- 已配对 / Pairings ---- */

uint8_t ble_pt_bond_count(void) { return s_bond_count; }

const ble_pt_bond_t *ble_pt_bond(uint8_t index) {
    return index < s_bond_count ? &s_bonds[index] : NULL;
}

void ble_pt_forget(const char *addr) {
    if (!addr) return;
    for (uint8_t i = 0; i < s_bond_count; ++i) {
        if (strcmp(s_bonds[i].addr, addr)) continue;
        memmove(&s_bonds[i], &s_bonds[i + 1], sizeof(s_bonds[0]) * (s_bond_count - i - 1));
        s_bond_count--;
        bonds_save();
        break;
    }
    nvs_handle_t h;  // 绑定也跟着清掉 / drop the bindings with it
    if (nvs_open(NVS_NS_BINDINGS, NVS_READWRITE, &h) == ESP_OK) {
        char key[16];
        nvs_erase_key(h, bond_key(key, sizeof(key), addr));
        nvs_commit(h);
        nvs_close(h);
    }
}

/* ---- 手动映射 / Manual bindings ---- */

void ble_pt_learn_begin(void) {
    // 刚连上就点学习时还没收到过帧，那就什么都不做：「全零」规则照旧够用。
    // Learning tapped right after connecting has no frame yet; the all-zero rule still applies.
    if (!s_raw_prev.len) return;
    s_rest = s_raw_prev;
    s_rest_known = true;
}

esp_err_t ble_pt_bind(ble_pt_action_t action, const ble_pt_frame_t *frame) {
    if (action != BLE_PT_ACTION_PREV && action != BLE_PT_ACTION_NEXT) return ESP_ERR_INVALID_ARG;
    if (!s_bindings.addr[0]) return ESP_ERR_INVALID_STATE;  // 没连着外设 / no peer connected
    if (!frame || frame->len > BLE_PT_FRAME_MAX) return ESP_ERR_INVALID_ARG;

    ble_pt_frame_t frames[BLE_PT_MAX_BINDINGS];
    memcpy(frames, s_bindings.frames, sizeof(frames));
    const unsigned index = action == BLE_PT_ACTION_PREV ? 0 : 1;
    if (!frame->len) {
        // 空帧 = 清除这一条。/ An empty frame clears the slot.
        memset(&frames[index], 0, sizeof(frames[0]));
    } else {
        bool all_zero = true;
        for (size_t i = 0; i < frame->len && all_zero; ++i) all_zero = frame->bytes[i] == 0;
        // 全零帧是「什么都没按」：拿它当身份，静止的外设就会一直翻页。
        // An all-zero frame is "nothing pressed"; as an identity it would turn pages while the
        // peripheral sits idle.
        if (all_zero) return ESP_ERR_INVALID_ARG;
        frames[index] = *frame;
        // 同一个帧不能同时绑两个动作：先绑的那个让位，否则翻页方向由槽位顺序决定。
        // One frame must not drive both actions: the earlier binding gives way, or the direction
        // would depend on slot order.
        if (frame_same(&frames[1 - index], &frames[index])) memset(&frames[1 - index], 0, sizeof(frames[0]));
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS_BINDINGS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    char key[16];
    err = nvs_set_blob(h, bond_key(key, sizeof(key), s_bindings.addr), frames, sizeof(frames));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) memcpy(s_bindings.frames, frames, sizeof(frames));
    return err;
}

bool ble_pt_binding(ble_pt_action_t action, ble_pt_frame_t *out) {
    if (action != BLE_PT_ACTION_PREV && action != BLE_PT_ACTION_NEXT) return false;
    const ble_pt_frame_t *slot = &s_bindings.frames[action == BLE_PT_ACTION_PREV ? 0 : 1];
    if (!slot->len) return false;
    if (out) *out = *slot;
    return true;
}

// 内置映射：上/下、左右、PageUp/PageDown 都算翻页。市面上的翻页器按键五花八门，
// 全认下来比让用户逐个配置省事得多。
// Built-in map: up/down, left/right and PageUp/PageDown all turn pages. Remotes differ wildly
// in which keys they send, so accepting them all beats configuring each one.
ble_pt_action_t ble_pt_action_for_usage(uint8_t usage, uint8_t mods) {
    (void)mods;
    switch (usage) {
        // 阅星瞳协议：把 0x01 / 0x02 当作主码值发出。这里写字面量而不是上面的宏：主机测试
        // 按函数体提取这个函数，文件级宏不会被带过去，引用它们会编不过。
        // YueXingTong: 0x01 / 0x02 sent as the primary code. Literals rather than the macros above
        // because the host test extracts this function by body, and file-level macros do not come
        // with it -- referencing them there would not compile.
        case 0x01:
            return BLE_PT_ACTION_PREV;
        case 0x02:
            return BLE_PT_ACTION_NEXT;
        case 0x52:  // Up
        case 0x50:  // Left
        case 0x4B:  // PageUp
            return BLE_PT_ACTION_PREV;
        case 0x51:  // Down
        case 0x4F:  // Right
        case 0x4E:  // PageDown
        case 0x2C:  // Space
        case 0x28:  // Enter
            return BLE_PT_ACTION_NEXT;
        default:
            return BLE_PT_ACTION_NONE;
    }
}

ble_pt_action_t ble_pt_action_for_frame(const ble_pt_frame_t *frame) {
    if (!frame || !frame->len) return BLE_PT_ACTION_NONE;
    // 手动绑定优先：它更具体，也允许用户把内置映射覆盖掉。
    // Manual bindings win: they are more specific and let the user override the built-in map.
    if (frame_same(frame, &s_bindings.frames[0])) return BLE_PT_ACTION_PREV;
    if (frame_same(frame, &s_bindings.frames[1])) return BLE_PT_ACTION_NEXT;
    // 阅星瞳协议：裸的一字节报告，报告号和字节下标都为 0，所以整帧就是 0x01 / 0x02。
    // 声明了键盘页的外设上，byte 0 是修饰键字节，那里的 0x01 是左 Ctrl；9 字节帧的 byte 0 是
    // 报告号。两者都不是键码，不能当成翻页。
    // YueXingTong: a bare report, so the whole frame is 0x01 / 0x02. On a peripheral that declared
    // a keyboard page, byte 0 is the modifier byte and 0x01 there is left Ctrl; on a 9-byte frame
    // byte 0 is the report id. Neither is a key code, and neither may turn a page.
    if (s_has_keyboard_page || frame->len == 9) return BLE_PT_ACTION_NONE;
    if (frame->bytes[0] == YXT_CODE_PREV) return BLE_PT_ACTION_PREV;
    if (frame->bytes[0] == YXT_CODE_NEXT) return BLE_PT_ACTION_NEXT;
    return BLE_PT_ACTION_NONE;
}
