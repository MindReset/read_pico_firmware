/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：BLE HID 主机（central），配对市面上的蓝牙翻页器/键盘。同一时刻只连一个外设。
 * 本模块只负责「连上与收键」，翻页语义由调用方决定；键事件进环形缓冲，主循环用
 * ble_pt_pop_key() 取。
 *
 * English: BLE HID host (central) for off-the-shelf Bluetooth page-turners and keyboards.
 * One peripheral at a time. This module only connects and receives; page semantics belong to
 * the caller. Key events land in a ring the main loop drains with ble_pt_pop_key().
 *
 * 冻结：本模块不做刷屏、不碰设置存储以外的 NVS、不决定翻页行为。
 * Frozen: no drawing, no NVS beyond its own settings namespace, no page-turn policy.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

// 扫描结果上限；满了就不再收新的，覆盖会打乱列表。/ Scan capacity; later results are dropped.
#define BLE_PT_MAX_DEVICES 24
// 已配对设备上限。/ Maximum bonded peers.
#define BLE_PT_MAX_BONDS 4
// 一个外设最多学多少个按键。/ Learned buttons per peripheral.
#define BLE_PT_MAX_BINDINGS 4
// 一帧报告的最大字节数。报告一般 8-9 字节，16 足够覆盖带报告号的复合设备。
// Maximum report frame size. Reports are typically 8-9 bytes; 16 covers composite devices that
// prefix a report id.
#define BLE_PT_FRAME_MAX 16
// 扫描时长：一直扫到被停掉为止。三级扫描页用它。
// Scan duration: keep going until stopped. The scan page uses this.
#define BLE_PT_SCAN_FOREVER 0x7FFFFFFFu

/// HID 特殊键。可打印字符走 `ch`。/ HID special keys; printable keys arrive as `ch`.
typedef enum {
    BLE_PT_KEY_NONE = 0,
    BLE_PT_KEY_ENTER,
    BLE_PT_KEY_BACKSPACE,
    BLE_PT_KEY_TAB,
    BLE_PT_KEY_ESCAPE,
    BLE_PT_KEY_DELETE,
    BLE_PT_KEY_LEFT,
    BLE_PT_KEY_RIGHT,
    BLE_PT_KEY_UP,
    BLE_PT_KEY_DOWN,
    BLE_PT_KEY_HOME,
    BLE_PT_KEY_END,
    BLE_PT_KEY_PAGE_UP,
    BLE_PT_KEY_PAGE_DOWN,
} ble_pt_key_t;

/// 一次按键（含按住自动重复）。/ One key press, including held-key auto-repeat.
typedef struct {
    char ch;              ///< 可打印 ASCII，特殊键为 0 / printable ASCII, 0 for a special key
    uint8_t usage;        ///< 原始 HID usage id / raw HID usage id
    uint8_t mods;         ///< HID 修饰键位掩码 / HID modifier bitmask
    ble_pt_key_t special; ///< 特殊键，否则 BLE_PT_KEY_NONE / special key, else BLE_PT_KEY_NONE
} ble_pt_event_t;

/// 扫描到的设备。/ A device seen while scanning.
typedef struct {
    char addr[18];      ///< "AA:BB:CC:DD:EE:FF"
    char name[32];      ///< 没有广播名时回落为地址 / falls back to the address
    int8_t rssi;
    uint8_t addr_type;  ///< 重连需要 / needed to reconnect
    bool has_name;      ///< 广播名确实收到了 / the advertised name was actually received
    bool hid;           ///< 广播了 HID 服务 0x1812 / advertises the HID service 0x1812
    bool bonded;        ///< 已在已配对列表里 / already in the bond list
} ble_pt_device_t;

/// 已配对（bonded）的设备，持久化在 NVS。/ A bonded peer, persisted in NVS.
typedef struct {
    char addr[18];
    char name[32];
    uint8_t addr_type;
} ble_pt_bond_t;

/// 一帧原始报告：外设按下某个键时发来的整串字节，含报告号。
/// 外设只在状态变化时发报告，同一个键每次按下的字节一模一样，所以整帧本身就是这个键的身份。
/// 学习时记下这一串，使用时逐字节比对——不用猜哪个字节是键码，也不用等一条「松开」报告。
/// One raw report frame: the whole byte string the peripheral sends while a key is down, report
/// id included. A peripheral reports only on change and the same key sends exactly the same bytes
/// every time, so the frame itself is that key's identity. Learning records the string and runtime
/// compares it byte for byte: no guessing which byte holds the code, and no waiting for a release
/// report that many remotes never send.
typedef struct {
    uint8_t len;                      ///< 有效字节数，0 表示没有 / valid bytes, 0 for none
    uint8_t bytes[BLE_PT_FRAME_MAX];  ///< 整帧内容 / the frame contents
} ble_pt_frame_t;

/// 一次原始按键边沿。身份就是整帧。/ One raw button edge; its identity is the whole frame.
typedef struct {
    ble_pt_frame_t frame;  ///< 按下时外设发的那一串 / the string sent while the key was down
    bool pressed;          ///< 按下为真，松开为假 / true for a press, false for a release
    uint32_t at_ms;        ///< 帧到达时刻 / frame arrival time
} ble_pt_raw_t;

/// 手动映射的动作。只做翻页，所以只有两个。
/// Manual-binding actions. Page turning only, hence just two.
typedef enum {
    BLE_PT_ACTION_NONE = 0,
    BLE_PT_ACTION_PREV,
    BLE_PT_ACTION_NEXT,
} ble_pt_action_t;

/* ---- 生命周期 / Lifecycle ---- */

/// 起 BLE 栈、载入 bond 表。可重复调用。内存不够时返回 ESP_ERR_NO_MEM 且不做任何初始化。
/// Bring the stack up and load bonds. Idempotent. Returns ESP_ERR_NO_MEM without initialising
/// anything when there is not enough memory.
esp_err_t ble_pt_start(void);

/// 让蓝牙栈跟随设置：需要时启动，不需要时拆掉，启动失败后按退避重试。
/// 主循环只调这一个函数——不要自己写「设置与运行状态不一致就 start()」，启动失败会让那句
/// 话每轮都成立，变成每秒几十次的重复初始化，把内存耗光并导致反复重启。
/// Follow the setting: start when wanted, tear down when not, and back off after a failed start.
/// The main loop calls only this - never roll your own "setting differs from state, so start()",
/// because a failed start keeps that condition true and re-initialises dozens of times a second.
void ble_pt_sync(bool wanted);

/// 启动失败时的原因（取走即清空），供给界面显示。
/// Reason for a failed start, cleared once taken, for the UI to show.
bool ble_pt_take_start_failure(char *out, size_t cap);

/// 用户明确把蓝牙关掉时调用：清掉失败计数与原因，下次开启重新给机会。
/// 与 sync(false) 不同——sync(false) 也会被「WiFi 抢占射频」触发，那种情况下不能清，
/// 否则崩溃自锁会被 WiFi 的开关悄悄解开。
/// Call this when the user explicitly switches Bluetooth off: it clears the failure count and
/// reason so the next enable gets a fresh chance. Unlike sync(false), which WiFi taking over the
/// radio also triggers and where clearing would let a WiFi toggle quietly disarm the self-lock.
void ble_pt_reset_failure(void);

/// 完全拆栈，把 NimBLE 占的内存（host 在 PSRAM）还给堆。用户关掉蓝牙时用这个，
/// 而不是 ble_pt_disconnect()，好让 EPUB 解压之类吃内存的活儿能重新分配。
/// 等最多 timeout_ms（上限 2s）取得生命周期锁，再等待 NimBLE 完成停机。
/// Tear the stack down and return NimBLE's memory (the host lives in PSRAM). Use this rather
/// than disconnect() when the user turns Bluetooth off, so memory-hungry work such as EPUB
/// inflate can allocate again. Waits at most timeout_ms (capped at 2s).
esp_err_t ble_pt_stop(uint32_t timeout_ms);
bool ble_pt_running(void);
/// WiFi 分配前释放蓝牙并禁止重启，结束后释放占用；失败不允许继续分配网络。
/// Stop BLE before WiFi allocations and inhibit restarts until released; abort networking on failure.
esp_err_t ble_pt_network_acquire(void);
void ble_pt_network_release(void);

/// 主循环每次迭代泵一次：驱动自动重连与按键自动重复。很轻，不阻塞。
/// Pump once per main-loop iteration: drives auto-reconnect and key auto-repeat. Cheap.
void ble_pt_poll(void);
/// 用户按键序号；只观察，不消费队列。/ Observe user input without consuming key events.
uint32_t ble_pt_input_serial(void);

/* ---- 扫描 / Discovery ---- */

void ble_pt_scan_start(uint32_t ms);
void ble_pt_scan_stop(void);
bool ble_pt_scanning(void);
uint8_t ble_pt_device_count(void);
const ble_pt_device_t *ble_pt_device(uint8_t index);

/* ---- 连接 / Connection ---- */

/// 异步发起连接；链路加密且订阅到 HID 输入报告后 ble_pt_connected() 才转真。
/// Start an async connect; ble_pt_connected() flips once the link is encrypted and the HID
/// input report is subscribed.
esp_err_t ble_pt_connect(const char *addr);
void ble_pt_disconnect(void);
bool ble_pt_connected(void);
bool ble_pt_connecting(void);
const char *ble_pt_connected_name(void);

/// 取走一次连接失败原因（取走后清空）。/ Take a pending connect-failure reason, clearing it.
bool ble_pt_take_failure(char *out, size_t cap);

/* ---- 建立链路的记录 / Bring-up trail ---- */

/// 把建立链路的一个事件写进崩溃日志（RTC，跨软复位保留）。
/// 崩溃后重启，屏幕上的「上次运行」区列出的就是这一串事件。
/// Record one bring-up event in the crash log (RTC, surviving a soft reset). After a crash the
/// "last run" block on screen lists exactly this trail.
void ble_pt_crumb(uint8_t id);

/// 事件编号。/ Event ids.
enum {
    BLE_PT_CRUMB_CONNECT = 1,   ///< 开始连接 / a connect started
    BLE_PT_CRUMB_LINK,          ///< 链路建立 / the link came up
    BLE_PT_CRUMB_DISCOVER,      ///< 开始服务发现 / service discovery started
    BLE_PT_CRUMB_HID,           ///< 确认对端有 HID / the peer turned out to have HID
    BLE_PT_CRUMB_PAIR,          ///< 配对/加密结束 / pairing finished
    BLE_PT_CRUMB_SUBSCRIBE,     ///< 订阅输入报告 / subscribed to an input report
    BLE_PT_CRUMB_READY,         ///< 链路可用 / the link became usable
    BLE_PT_CRUMB_FAIL,          ///< 某一步失败 / a step failed
    BLE_PT_CRUMB_RX,            ///< 收到一条通知 / a notification arrived
    BLE_PT_CRUMB_INGEST,        ///< 开始解析报告 / report parsing started
    BLE_PT_CRUMB_EMIT,          ///< 解出一个按键 / a key was decoded
    BLE_PT_CRUMB_KEY_PUSH,      ///< 按键进队列 / the key entered the ring
    BLE_PT_CRUMB_POP,           ///< 主循环取走按键 / the main loop took a key
};

/// 取走一次配对码（取走后清空）。非 0 时界面应显示它。
/// Take a pending pairing passkey, clearing it. A non-zero value should be shown.
bool ble_pt_take_passkey(uint32_t *out);

/* ---- 已配对 / Pairings ---- */

uint8_t ble_pt_bond_count(void);
const ble_pt_bond_t *ble_pt_bond(uint8_t index);
void ble_pt_forget(const char *addr);

/* ---- 输入 / Input ---- */

/// 取下一个键事件；队列空返回 false。/ Pop the next key event; false when empty.
bool ble_pt_pop_key(ble_pt_event_t *out);

/// 取下一个原始按键边沿。与 ble_pt_pop_key() 同源但各自一个环，不取就完全没影响。
/// 一次按下只有在它的释放还有位置时才入环；环满时整对丢弃，绝不只丢释放。
/// 边沿按整帧判定：帧变了、或静止帧之后又来了一帧才算一次按下；松开报告一直不来时，静默
/// 150ms 之后由 poll 收尾，好让同一个键的下一次按下再算一次。
/// Pop the next raw button edge. Same reports as ble_pt_pop_key() but a ring of its own, so an
/// app that never calls it sees no change. A press only enters with room left for its release;
/// when the ring is full the whole press is dropped, never a release alone.
/// Edges are judged on whole frames: a changed frame, or any frame after the resting one, is a
/// press; when no release report ever comes, poll closes the press after 150 ms of silence so the
/// next press of the same key counts again.
bool ble_pt_pop_raw(ble_pt_raw_t *out);

/* ---- 手动映射 / Manual bindings ---- */

/// 学习开始：把此刻那一帧（用户没按键时外设发的静止帧）当基准。
/// 有些遥控器的静止帧带着非零常量（协议字节、状态字节），只按「全零」判断会把静止帧也当成
/// 一次按下，学到的就是它。学习界面在提示用户按键之前调一次。
/// Begin learning: the frame present right now -- what the remote sends with nothing pressed --
/// becomes the baseline. Some remotes put non-zero constants in their resting frame, and an
/// all-zero test alone would read that frame as a press and learn it instead of the key. A
/// learning screen calls this once before asking the user to press.
void ble_pt_learn_begin(void);

/// 把一个学到的整帧绑到动作上；len 为 0 表示清除该动作的绑定。
/// 绑定按外设地址分别保存。/ Bind a learned frame to an action; a zero-length frame clears it.
/// Bindings are stored per peripheral address.
esp_err_t ble_pt_bind(ble_pt_action_t action, const ble_pt_frame_t *frame);

/// 读回某个动作绑定的整帧；未绑定时返回 false，且不写 out。
/// Read back an action's bound frame; false when unbound, leaving out untouched.
bool ble_pt_binding(ble_pt_action_t action, ble_pt_frame_t *out);

/// 把一帧折算成动作。手动绑定优先，其次是内置映射。
/// 内置映射含阅星瞳协议：整帧就是 0x01 上一页、0x02 下一页；声明了键盘页的外设上 byte 0 是
/// 修饰键，那里的 0x01 是左 Ctrl，不算翻页。
/// Resolve a frame to an action: manual bindings first, then the built-in map. The built-in map
/// covers the YueXingTong protocol -- a bare 0x01 previous and 0x02 next. On a peripheral that
/// declared a keyboard page, byte 0 is the modifier byte and 0x01 there is left Ctrl, not a page
/// turn.
ble_pt_action_t ble_pt_action_for_frame(const ble_pt_frame_t *frame);

/// 把 HID usage 折算成动作（内置映射，不查手动绑定）。真机不带码值所以单列。
/// 内置映射含阅星瞳协议：0x01 上一页、0x02 下一页。
/// Resolve a HID usage to an action via the built-in map only; separate because a usage has
/// no raw code. The built-in map covers the YueXingTong protocol: 0x01 previous, 0x02 next.
ble_pt_action_t ble_pt_action_for_usage(uint8_t usage, uint8_t mods);
