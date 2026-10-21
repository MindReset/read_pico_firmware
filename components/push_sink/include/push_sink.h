/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：push_sink 的胶水层公共 API。纯核（push_proto/push_session）不含任何外设
 * 依赖；本头文件面向的调用方只有两处：ble_page_turner（栈生命周期与射频互斥的
 * 钩子）与 main（武装入口、主循环轮询、UI 一次性事件消费）。
 * English: the public API of the push_sink glue. The pure core
 * (push_proto/push_session) touches no hardware; this header serves exactly two
 * kinds of callers: ble_page_turner (stack-lifecycle and radio-mutex hooks) and
 * main (the arm entry, the main-loop pump and the one-shot UI events).
 *
 * 冻结：GATT 服务与广播名格式随设计草案 §3.1/§6.5；改 UUID 或字段先改草案。
 * Frozen: the GATT service and adv-name format follow design draft sections
 * 3.1/6.5; document any change before touching the code.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "push_proto.h"
#include "push_session.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 常量 / Constants ---- */

/// 武装期广播名前缀，后缀是 4 位十六进制 token（鉴权要素，见草案 §6.5）。
/// Advertising-name prefix while armed; the suffix is the 4-hex-digit token
/// (the auth element, draft section 6.5).
#define PUSH_ADV_NAME_PREFIX "PicoPush-"

/// .part 暂存与成品路径：TF 卡隐藏目录，设备自动创建（用户可见文档只说到
/// `.readpico` 这一层，路径细节仅作注解）。
/// Staging and product paths in the card's hidden directory, created by the
/// device (user-facing docs mention only the .readpico level).
#define PUSH_DIR "/sdcard/.readpico"
#define PUSH_PART_PATH PUSH_DIR "/push.part"
#define PUSH_TEXT_PATH PUSH_DIR "/push.txt"

/// 待武装超时：点了接收但栈迟迟起不来（内存闸门/失败退避）就放弃，
/// 别让界面干等。/ Pending-arm timeout: give up when the stack never comes up
/// (memory gate or back-off) instead of leaving the UI spinning.
#define PUSH_PENDING_ARM_TIMEOUT_MS 10000u

/* ---- 生命周期 / Lifecycle ---- */

/// 初始化（建互斥锁、清会话）。app_main 启动时调一次，之后所有入口都可重入。
/// Initialise (mutex, session). Called once from app_main; every entry is
/// re-entrant afterwards.
void push_sink_init(void);

/// 注册词典可用性探针（main 用 dict_ready() 注册；不注册则视为可用，
/// 便于组件独立工作与宿主测试）。/ Register the dictionary-availability probe
/// (main passes dict_ready(); unregistered means "available").
void push_sink_set_dict_probe(bool (*probe)(void));

/* ---- 武装 / Arming ---- */

/// 用户点「接收推送」。token 传 0 由本模块随机生成（读 push_sink_adv_name()
/// 取回展示）；window_ms 传 0 表示充电时常开（无超时、拔电即退，裁定 d）。
/// 栈还没起来时进入「待武装」，栈就绪后由 on_stack_sync 自动续上。
/// The user pressed receive. token == 0 generates a random one (read it back
/// with push_sink_adv_name()); window_ms == 0 means charger-held (no timeout,
/// unplug exits, ruling d). With the stack still down this becomes a pending
/// arm that on_stack_sync completes.
esp_err_t push_sink_arm(uint16_t token, uint32_t window_ms);

/// 用户取消（UI 上的退出按钮）。/ The user cancelled (the UI's exit button).
void push_sink_cancel(void);

/// 带原因取消（gen18 排雷）：任何武装期 BLE 拆除都必须留痕——
/// 「armed canceled by X」日志 + 原因可被 UI 取回展示，静默失败零容忍。
/// Cancel with a reason (gen18 mine-sweeping): every armed-period BLE
/// teardown must leave a trace - an "armed canceled by X" log plus a reason
/// the UI can read back and show. Zero tolerance for silent failure.
void push_sink_cancel_why(const char *why);

/// 最近一次收摊的原因串（窗口到期/拔电/取消等）；无原因为 NULL。
/// The reason string of the most recent close-up (window expiry, unplug,
/// cancel, ...); NULL when there is none.
const char *push_sink_last_close_why(void);

/// 是否处于武装窗口的任意非 IDLE 态：栈必须保持运行、翻页器让位。
/// Whether the machine is in any non-IDLE armed state: the stack must stay up
/// and the page-turner yields.
bool push_sink_armed(void);

/// 当前状态（UI 轮询刷新用）。/ Current state, for the UI to poll.
push_state_t push_sink_state(void);

/// 当前广播名（ARMED 之后有效）；未武装时返回空串。
/// The current advertising name (valid once ARMED); empty when not armed.
const char *push_sink_adv_name(void);

/// 窗口剩余毫秒；0 表示无超时或已退出。/ Milliseconds left in the window; 0
/// means no timeout or already closed.
uint32_t push_sink_window_left_ms(void);

/// 传输进度（RECEIVING 时有效）。/ Transfer progress, valid while RECEIVING.
void push_sink_progress(uint32_t *received, uint32_t *total);

/* ---- 栈钩子（ble_page_turner 调用）/ Stack hooks (called by ble_page_turner) ---- */

/// 栈同步完成（on_sync 末尾调）：实证核对 GATT 注册结果（没注册上就显式补一次
/// ble_gatts_start），续上待武装或把中断的广播找回来。
/// Stack sync done (called at the end of on_sync): verify the GATT
/// registration empirically (calling ble_gatts_start() explicitly if it never
/// landed), complete a pending arm and bring back an interrupted advertisement.
void push_sink_on_stack_sync(void);

/// 常驻注册 GATT 服务：把服务定义排进 NimBLE 的注册队列。必须在
/// nimble_port_init() 之后、host 任务创建之前调用——NimBLE 的 ble_hs_start()
/// 在 host 启动时一次性调用 ble_gatts_start() 消费该队列，且发生在 sync 回调
/// 之前；排在 sync 回调里的定义永远排不上（gen12 修复：手机端「能连上、
/// Services 页空白」的根因）。失败不影响翻页器：日志留证，sync 里还会兜底。
/// Register the GATT service residently: queue the definitions into NimBLE's
/// registration queue. Must be called after nimble_port_init() and before the
/// host task is created: NimBLE's ble_hs_start() calls ble_gatts_start()
/// exactly once at host start, before the sync callback; definitions queued in
/// the sync callback never make it (gen12 fix - the root cause of the phone
/// symptom "connects fine, Services page blank"). Failure does not affect the
/// page-turner: it is logged and the sync path retries as a fallback.
esp_err_t push_sink_gatt_register(void);

/// 栈即将拆除（stop_locked 开头调）：停广播、作废句柄。.part 与 NVS 会话保留，
/// 供栈回来后续传。/ The stack is about to be torn down (called at the start of
/// stop_locked): stop advertising and invalidate handles. The partial file and
/// the NVS session survive for resume after the stack returns.
void push_sink_on_stack_stop(void);

/// 武装期为真：ble_page_turner 的 poll 据此暂停翻页器连接（裁定 c）——有链路
/// 就地断但保留重连目标，退出武装自动接回。/ True while armed: ble_page_turner's
/// poll pauses the page-turner link (ruling c) - a live link drops in place with
/// its reconnect target kept, so leaving arm reconnects automatically.
bool push_sink_turner_paused(void);

/* ---- 主循环 / Main loop ---- */

/// 主循环每轮调一次：喂 TICK 驱动窗口/宽限到期，看守充电常开的拔电即退，
/// 兼管待武装超时。/ Called once per main-loop iteration: feeds TICK for window
/// and grace expiry, watches the charger-held unplug exit and the pending-arm
/// timeout.
void push_sink_poll(void);

/* ---- UI 一次性事件 / One-shot UI events ---- */

/// 推送成功：取走开书请求（title 与 type 一并带出）。/ Push succeeded: take the
/// open-book request (title and type come with it).
bool push_sink_take_open(char *title, size_t cap, uint8_t *type);

/// 正文已成功落盘（COMMIT 完成）的一次性事件。首页状态卡据此把“最近推送”
/// 标题同步成最新内容并触发局部重绘——否则连续推送时该文字会卡在首推旧值。
/// 标题取自本次 BEGIN 的 title，type 为 PUSH_TYPE_*；单词卡（PUSH_TYPE_WORD）
/// 不写 push.txt，不会触发此事件。
/// One-shot raised when a body has just committed. The home status card uses
/// it to sync the "last push" title to the newest content and repaint - without
/// it a continuous push would freeze the line on the first push's old title.
/// The title comes from this push's BEGIN and type is PUSH_TYPE_*; word cards
/// (PUSH_TYPE_WORD) write no push.txt and never raise this event.
bool push_sink_take_committed(char *title, size_t cap, uint8_t *type);

/// 单词卡推送成功：取走单词（title 即单词）。/ Word-card push: take the word
/// (the title is the word).
bool push_sink_take_word_card(char *word, size_t cap);

/// 取走一条要展示给用户的错误（码见 push_proto.h 的 PUSH_ERR_*）。
/// Take an error to show the user (codes in push_proto.h).
bool push_sink_take_err(int *code);

/// 武装结束（窗口超时/取消/拔电/完成收摊）：UI 应退出武装页。
/// The armed window closed (timeout, cancel, unplug or wrap-up): the UI leaves
/// the arm page.
bool push_sink_take_closed(void);

/// 待武装失败（栈起不来）。/ A pending arm failed (the stack never came up).
bool push_sink_take_start_failed(void);

/* ---- 反向通道 / Reverse channel ---- */

/// 用户在设备上标记已读：notify EVT_READ（惊喜2，手机端清剪贴板的触发点）。
/// The user marked it read on the device: notify EVT_READ (surprise 2, the
/// phone-side clipboard clear trigger).
void push_sink_notify_read(void);

/// gen21 未读位：正文 COMMIT 落盘即置位（单词卡不置位），打开 push.txt 即清除；
/// NVS 持久化，重启不丢。驱动首页状态条的反色「有新内容」徽章。
/// gen21 unread flag: set when a body commit lands (word cards never set it),
/// cleared when push.txt is opened; persisted in NVS across reboots. Drives the
/// home status bar's inverted "new content" badge.
bool push_sink_unread(void);
void push_sink_clear_unread(void);

/// 通知当前电量百分比（BATT）。/ Notify the current battery percent.
void push_sink_notify_battery(void);

/// gen22 蓝牙校时：取走一次手机下发的 UTC 真值（一次性，非线程重复消费）。
/// 设备侧已在收到时写入系统时钟；本 API 交给 main 层登记漂移样本并同步 PMU。
/// gen22 time-over-BLE: take one phone-supplied UTC truth (one-shot). The device
/// already applied it to the system clock; main uses this to record the drift
/// sample and sync the PMU.
bool push_sink_take_time_sync(uint32_t *utc);

#ifdef __cplusplus
}
#endif
