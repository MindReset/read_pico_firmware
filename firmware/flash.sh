#!/bin/sh
# 生产版烧写脚本（无 instrumentation，第②层 + 其余六项用这个）/ Flash script for the PRODUCTION build
#
# 用法 / Usage:
#   sh flash.sh /dev/ttyUSB0        # Linux / macOS
#   sh flash.sh COM3                # Windows (Git Bash / MSYS2)
#
# 本板没有 BOOT / RESET 物理键，esptool 的 --before default-reset 会自动进下载模式，
# 不需要按任何键。连不上先换端口、再拔插 USB。
# The board has no BOOT/RESET button; esptool's --before default-reset enters download mode
# automatically. If it fails, try another port or replug USB.
#
# 波特率：默认 460800；若烧录中途报错/校验不过，降到 230400 重试：
#   sh flash.sh /dev/ttyUSB0 230400
# Baud: 460800 by default; on mid-flash errors or checksum failures, drop to 230400:
#   sh flash.sh /dev/ttyUSB0 230400
#
# 警告 / WARNING: Read_Pico.bin 必须落在 0x10000。
#                 写到 0x0 会覆盖 bootloader / 分区表 / NVS / phy_init。
#                 Read_Pico.bin MUST go to 0x10000; 0x0 would overwrite the
#                 bootloader, partition table, NVS and phy_init.
#
# 四个偏移对应 partitions_16M.csv 的 5.5MB app 布局：ota_0 在 0x10000、
# ota_1 在 0x590000，故 otadata 落在 0xb10000。不是上游默认的 4MB 布局。
# These offsets match our partitions_16M.csv 5.5MB app layout: ota_0 at 0x10000,
# ota_1 at 0x590000, hence otadata at 0xb10000 - not upstream's 4MB default.
#
# 烧前请把完整输出存盘（esptool 的哈希/摘要行是烧录证据）：
#   sh flash.sh /dev/ttyUSB0 2>&1 | tee flash-prod-$(date +%Y%m%d-%H%M%S).log
# Save the FULL output (esptool hash lines are the flashing evidence).

set -e
PORT="${1:-/dev/ttyUSB0}"
BAUD="${2:-460800}"
DIR="$(cd "$(dirname "$0")" && pwd)"

# 锚定值 = gen14 production（修复 gen13 阻断级推送失败：BEGIN 帧被拒 + 手机端
#   STATUS 超时；版本串 bump 至 0.3.3-pico14）。
#   根因（gen13 真机串口日志 + NimBLE 源码逐行实证 + 第三方 BLE 调试助手交叉验证）：
#   双 bug 叠加——
#   (1) CONFIG_BT_NIMBLE_ATT_MAX_PREP_ENTRIES=4（sdkconfig.defaults:100）太小。
#       手机与设备 MTU 协商失败时（有效 MTU 23），83 字节 BEGIN 由客户端走
#       Prepare Write 长写，每包 20 字节需 5 个 prep entry；即使协商成功，
#       241 字节 DATA 也需 13 个。队列第 5 个 os_memblock_get 返回 NULL，
#       ble_att_svr_insert_prep_entry 打 rc=6（BLE_HS_ENOMEM）——失败只打
#       LOGD，release 构建不可见，真机只表现「写了但没反应」。gen13 真机日志
#       485E 第一周期 149 秒后 E (402329) ble_att_svr_insert_prep_entry rc=6
#       实锤（第三方助手连接、未协商 MTU、手动写 BEGIN 触发的长写）。
#   (2) notify 空口失败路径全部静默：ble_gatts_notify_custom 返回 0 只代表
#       HCI 命令入队，真正的空口结果在 BLE_GAP_EVENT_NOTIFY_TX 的 status；
#       notify_status 的 rc!=0 与 NOTIFY_TX status!=0 过去都只打 LOGD。
#       gen13 真机：485E 第二周期（APK，MTU 247、BEGIN 一次写完、552208
#       一次 notify initiated）手机端 20 秒等不到 STATUS；同一台设备换
#       第三方 BLE 调试助手手动发合法 83B BEGIN（token=0x485E、title「Test」），
#       助手「收: 0」——两个独立客户端表现一致，指向设备侧 notify 通路。
#   gen14 修法：
#   - sdkconfig.defaults / sdkconfig / sdkconfig.ci：MAX_PREP_ENTRIES 4→16
#     （覆盖 241B DATA 长写的 13 个 entry + 3 余量），MSYS_1_BLOCK_COUNT
#     12→20（每个 prep entry 从 MSYS_1 池领一个 mbuf，16 个 entry 需 16 块）。
#   - push_sink.c：notify 失败全链路可见 + 重投——notify_status 的 mbuf 领取
#     失败与 rc!=0 升级 LOGW 并排 esp_timer 100ms 重发 s_last_status（每连接
#     周期 3 次配额）；NOTIFY_TX status!=0 升级 LOGW 并排重投；DISCONNECT
#     撤配额防 timer 野发（cancel 只翻旗，避免与 timer 回调的 s_lock 死锁）。
#   - push_sink.c 诊断日志：BEGIN/DATA/CTRL 每个拒绝点打印 len、头 4 字节、
#     token/type/total——真机一测便知 decode 败在哪个检查（gen13 静态对表
#     全绿却无法归因，补此日志）。
#   - MainActivity.java：MTU 协商失败（<32）明确失败重试，不闷头走长写；
#     waitStatus 接受 RESUME 帧（offset 0 按 READY 继续，非 0 提示重新完整
#     发送——gen13 只等 READY/ERR，RESUME 被丢弃白等 20 秒）。
#   协议两侧逐字节对表复核（用户假设②排除）：PushProto.java encodeBegin 与
#   push_proto.c push_begin_decode 在 magic/op/ver/type/token/total/crc/sid/
#   title[64] 的偏移与小端序上完全一致，25/25 宿主对表 + 168 项 host test 全绿；
#   用户假设①的「max_len 不够」修正：ESP-IDF NimBLE 的 ble_gatt_chr_def 没有
#   max_len 字段，实质限制是 prep 队列深度（修法见上）。
#   gen13 = 03e59a07…（广播回归修复轮，真机验证广播/扫描/连接/MTU 247 全通，
#   但推送全链止于 BEGIN 被拒）改名留档为 Read_Pico.bin.gen13-03e59a07，勿烧。
#   gen12 = 6e1d15c1…（GATT 注册时序修复轮；注册修复有效但广播回归不可扫描）
#   改名留档为 Read_Pico.bin.gen12-6e1d15c1，勿烧。
#   gen11 = 6490999f…（二期 M1 完整轮，真机连接 OK 但 Services 空白）改名留档为
#   Read_Pico.bin.gen11-6490999f，勿烧。
#   gen10 = 9e573c84…（r95 rebase 落定轮，真机轮已 PASS）改名留档为 Read_Pico.bin.gen10-
#   9e573c84，勿烧（不含 M1）。
#   gen9 = a0bbc7a3…（#121 字体弹回修复轮，真机轮已 PASS）留档，勿烧（底座 r93，缺 r95 全部改动）。
#   gen8 = 2610ce20…（缺陷发现轮，已烧设备）留档，勿烧。
#   gen7 = ace5373b…（修复前，G7-A1 前）留档，勿烧。
# Anchor = gen14 production (fixes the gen13 blocking push failure: the BEGIN
# frame refused and the phone's STATUS wait timing out; version bumped to
# 0.3.3-pico14). Root cause (gen13 device serial log + NimBLE source audit +
# cross-validation with a third-party BLE debugger): two stacked bugs -
# (1) CONFIG_BT_NIMBLE_ATT_MAX_PREP_ENTRIES=4 was too small. When the MTU
# exchange fails (effective MTU 23), the 83-byte BEGIN goes out as a Prepare
# Write long write needing 5 prep entries at 20 bytes each; even with a
# successful exchange a 241-byte DATA needs 13. The 5th os_memblock_get
# returned NULL and ble_att_svr_insert_prep_entry logged rc=6
# (BLE_HS_ENOMEM) - the failure only logged at LOGD, invisible in release, so
# hardware just showed "written but no response". The gen13 log line
# E (402329) ble_att_svr_insert_prep_entry rc=6 (149 s into the first 485E
# cycle: third-party debugger, no MTU exchange, manual BEGIN) proves it.
# (2) Every notify on-air failure path was silent: a zero return from
# ble_gatts_notify_custom only means the HCI command was queued, the real air
# result being NOTIFY_TX's status; both notify_status's rc!=0 and NOTIFY_TX
# status!=0 used to log at LOGD only. On gen13 hardware: the second 485E cycle
# (APK, MTU 247, BEGIN written in one shot, one notify at 552208) left the
# phone waiting 20 s for STATUS; the same device driven by a third-party BLE
# debugger with a legal 83 B BEGIN (token=0x485E, title "Test") showed
# "received: 0" - two independent clients, one symptom: the device-side
# notify path. gen14 fixes:
# - sdkconfig.defaults / sdkconfig / sdkconfig.ci: MAX_PREP_ENTRIES 4->16
#   (covers the 13 entries of a 241 B DATA long write plus slack) and
#   MSYS_1_BLOCK_COUNT 12->20 (each prep entry takes one MSYS_1 mbuf; 16
#   entries need 16 blocks).
# - push_sink.c: visible notify failures plus retries - mbuf-allocation
#   failure and rc!=0 in notify_status escalate to LOGW and schedule an
#   esp_timer 100 ms re-send of s_last_status (3 attempts per connection
#   cycle); NOTIFY_TX status!=0 escalates to LOGW and schedules a retry;
#   DISCONNECT drops the quota so the timer cannot fire into the next cycle
#   (cancel flips a flag only, avoiding a deadlock with the timer callback's
#   s_lock).
# - push_sink.c diagnostics: every BEGIN/DATA/CTRL refusal prints len, the
#   first 4 bytes and token/type/total - one real-hardware run then tells
#   which check rejected the frame (gen13's static tables all agreed yet the
#   refusal could not be attributed; this log fills the gap).
# - MainActivity.java: a failed MTU exchange (<32) fails explicitly for a
#   retry instead of silently entering a long write; waitStatus accepts the
#   RESUME frame (offset 0 continues as READY, non-zero prompts a full
#   resend - gen13 waited for READY/ERR only, dropped RESUME and idled out
#   the full 20 s).
# Byte-for-byte protocol cross-check (user hypothesis 2 dismissed):
# PushProto.java's encodeBegin and push_proto.c's push_begin_decode agree on
# every field offset and little-endian order (magic/op/ver/type/token/total/
# crc/sid/title[64]); 25/25 host table tests and 168 host-test checks pass.
# User hypothesis 1 corrected: ESP-IDF NimBLE's ble_gatt_chr_def has no
# max_len field - the real limit was the prep-queue depth (fixed above).
# gen13 = 03e59a07… (adv-regression fix round; hardware verified adv/scan/
# connect/MTU 247 all working, but the push chain stopped at the refused
# BEGIN) archived as Read_Pico.bin.gen13-03e59a07 - do NOT flash.
# gen12 = 6e1d15c1… (GATT registration-timing round; the fix worked but the
# adv regression made the device unscannable) archived as
# Read_Pico.bin.gen12-6e1d15c1 - do NOT flash. gen11 = 6490999f… (full phase-2
# M1 round; hardware connected but Services blank) archived as
# Read_Pico.bin.gen11-6490999f - do NOT flash. gen10 = 9e573c84… (r95 rebase
# round, hardware PASS) archived as Read_Pico.bin.gen10-9e573c84 - do NOT
# flash (no M1). gen9 = a0bbc7a3… (#121 font-snap-back round, hardware PASS)
# archived, do NOT flash (r93 base, missing all r95 rebase changes). gen8 =
# 2610ce20… (defect-discovery round, burned) archived, do NOT flash. gen7 =
# ace5373b… (pre-fix) archived.
#
# 版本串 / Version string: 0.3.3-pico22（gen21 为 0.3.3-pico21；沿用旧串会让 TF 卡升级
# 判「版本相同」而拒绝，故每代 fork 专属可区分串）。
# Version string: 0.3.3-pico22 (gen21 was 0.3.3-pico21; reusing an older tag makes
# the TF-card upgrade path reject the package as "same version", so each
# generation gets its own fork-specific distinguishable tag).
#
# 段表锚（esptool image-info，与 gen17 对照）：
#   seg1 DRAM  Load addr(vaddr)=0x3fc9c700 十代完全相同（世代不变锚）；
#   seg1 len=0x08a8c（与 gen17 持平）；File offs=0x002336e8（gen17 为 0x00233448，
#   差 +0x2a0=672B）—— 仅因 seg0(DROM) 增长顺移，vaddr 不动。
#   真机 boot 日志预测锚：seg1 paddr = 0x002336e8 + 8(段头) + 0x10000(ota_0 基址) = 0x002436f0
#   （该公式由 gen5/6/10 三轮真机日志实证：gen10 实测 paddr=0x00242040 ✓；
#   gen14 实测 paddr=0x00243250 与预测 0x00243250 逐字吻合 ✓）。
#   体积：4,053,392 B，相对 gen17（4,051,072 B）**+2,320 B**（双操作数：
#   4,053,392 − 4,051,072 = 2320），来自 push_sink 取消原因链、首页推流状态卡
#   与自动切回导航三处新增代码，以及审计逐行复查补的 quick_toggle_wifi 武装期豁免守卫（+64B）。
#   bootloader 21,232 B（哈希 eecf08fb…，gen17 为 bf176b86…；bootloader 带构建
#   时间戳，哈希每代必变，零行为变化——该结论由 gen12→gen13 逐字节 diff 实证：
#   0x54 处时间串 6B + 0x52cf-0x52ef 摘要 33B）；
#   partition-table / ota_data_initial 与 gen17 同尺寸同内容（sha256 逐字节一致：
#   9d5d890b… / 7d2c7ac4…）。
# Segment anchor (esptool image-info, vs gen16): seg1 DRAM Load addr (vaddr) =
# 0x3fc9c700 identical across ten generations (generation-invariant); seg1
# len = 0x08a8c (flat vs gen16); File offs = 0x00233448 (gen16 0x00233308, +0x140
# = 320B) - a shift from seg0 (DROM) growth, vaddr unmoved. Predicted hardware
# boot-log anchor: seg1 paddr = 0x00233448 + 8 (segment header) + 0x10000 (ota_0
# base) = 0x00243450; that formula is empirically confirmed by gen5/gen6/gen10
# device logs (gen10 measured paddr = 0x00242040) and again by gen14, whose
# measured paddr=0x00243250 matched its prediction character for character.
# Size: 4,051,072 B, **+640 B** vs gen16 (4,050,432 B) - both operands:
# 4,051,072 − 4,050,432 = 640 - from the dictionary-open diagnostic strings and
# the probe-semantics comments.
# bootloader 21,232 B (hash bf176b86…, gen16 was 852da709…; the bootloader embeds a
# build timestamp so its hash changes every generation with zero behavior change -
# proven by the gen12->gen13 byte diff: a 6B time string at 0x54 + a 33B digest
# at 0x52cf-0x52ef); partition-table / ota_data_initial same size and content as
# gen16.
#
# 重锚记录（2026-10-20，gen21 真机验收全绿后的发布前打磨轮 / gen22）：
#   版本串：0.3.3-pico22（CMakeLists.txt 一处 + 本记录 + 烧录说明，三处同步）。
#   用户 gen21 真机八项验收面全绿，发布前提出三项，一次做完：
#   ① push 虚拟书专属封面：push.txt 无内嵌封面，按路径哈希落到深色版式（黑底白字
#     占位观感），与带内嵌封面的 epub 放一起很突兀。新增第 4 版式 push_delivery
#     （纸纹浅底 + 「手机→电波→书」图形 + 底部书名），沿用现有灰阶原语，不加图片
#     资源。缓存版本必须同步递增，否则旧结果一直命中：BOOK_AUTO_COVER_VERSION 4→5、
#     FAST_COVER_REVISION 1→2。
#   ② 时钟跑偏（每天快几分钟、已累计 34 分钟）：根因是硬件事实、配置改不掉——
#      组件文档 llms-full 明载「LSI 约 32 kHz 供给 IWDT、RTC，无外部晶振」；深睡时
#      PMU 切断 ESP 供电（sleep.c 注释：SOFT_SLEEP drops the ESP rail），时间完全由
#      PMU 内部 RC 走。修法三条：
#      · 漂移率学习补偿：每次校时记录 (真实UTC, 校正前本地秒) 存 NVS，估算 ppm；
#        开机从 PMU 取时按 ppm 反向修正（无网络也有效）。EMA 平滑，样本 <1h 或
#        >2% 视为噪声丢弃，倒退/超 24h 跨度不补偿。
#      · 放宽触发：离线短连校时 6h → 2h（需已保存 WiFi）。
#      · 蓝牙校时：新增 CTRL 操作码 PUSH_OP_SET_TIME(0x05) 携带 4 字节 UTC（小端）。
#        BEGIN 帧定长 83B 且 Java 侧标注「冻结」，故不动 BEGIN；SET_TIME 独立于
#        「必须武装」闸门之外，连上即可校时——无 Wi-Fi 场景的唯一高频通道。
#        APK 已重新编译（encodeSetTime + 连接后下发）。
#   ③ 防串台（MAC 绑定）：广播名原为随机数 PicoPush-XXXX，咖啡馆两台设备会重名、
#      手机可能推错人。改为 token 由出厂 MAC 后两字节派生（mac_token()），广播名
#      自动变成固定唯一的 PicoPush-1A2B；墨水屏显示的广播名同源同步。
#      BEGIN 帧的 token 校验链路完整保留（值与广播名同源，校验照旧）。
#      APK 侧不再「扫到第一个就连」：收集全部 PicoPush- 设备，首个命中后多收 2.5 秒，
#      **仅一台才自动连接**，多台则弹列表让用户按屏幕上显示的代号点选。
#   Gate：push_sink 纯核 168 checks 0 red；APK 协议对表 25/25 全过（build-apk.sh）；
#     fullclean 全量重建 exit 0，警告 **8 条与基线同集**（app_book×7 + ui_pinpad×1，增 0）。
#   重锚说明（gen22 第二次落盘）：审计放行 `0da62846…` 后，为核销三追问又补了
#     ① push_proto.h 的「协议变更记录」注释块（gen22 / 0x05 条目）与 ② 参考实现
#     push_pico.py 同步（encode_set_time + --set-time）。注释与主机工具都不改固件
#     行为，但源码树已变，故按「源码与交付产物必须同源」重跑 fullclean 重编，
#     锚更新为 `fdd48866…`；体积 4,060,496 B 未变、seg1 offs/paddr 未变，
#     功能与放行的 `0da62846…` 等价。ESP-IDF 产物含非确定性因素（每代重编必换
#     sha，bootloader 时间戳亦同），故每落盘一次重锚一次是常态。
#   Re-anchor note (gen22, second drop): after the audit cleared `0da62846…`, the three
#     question close-out added (1) the protocol change-log comment block in push_proto.h
#     (gen22 / 0x05 entry) and (2) reference-implementation sync in push_pico.py. Neither
#     changes firmware behaviour, but the source tree moved, so "source and shipped binary
#     must be same-origin" forced a fresh fullclean rebuild; the anchor is now
#     `fdd48866…` with size 4,060,496 B and seg1 offs/paddr unchanged - functionally
#     equivalent to the cleared `0da62846…`. ESP-IDF output is non-deterministic (every
#     rebuild changes the sha, as the bootloader timestamp does), so re-anchoring on each
#     drop is the norm.
#   终版锚：Read_Pico.bin 4,060,496 B（相对 gen21 终版 4,053,984 **+6,512 B**，
#     双操作数 4,060,496−4,053,984=6512），sha256 `fdd48866…`；
#     seg1 File offs `0x00233888`（gen21 终版 0x00233778，DROM 段 +0x110=272B 顺移）、
#     paddr `0x00243890`；seg1 Load addr 0x3fc9c700 世代不变锚不动。
#     partition-table / ota_data_initial 与 gen21 逐字节一致（9d5d890b… / 7d2c7ac4…）。
#     APK ReadPicoPush.apk sha256 `6e26875a…`（本轮重新编译，含设备列表 + 校时下发）。
# Re-anchor record (2026-10-20, pre-release polish after gen21's all-green hardware
# round / gen22): version 0.3.3-pico22. Three items, all from the user's release review:
#   (1) dedicated cover for the push pseudo-book - push.txt has no embedded art and its
#     path hash landed on a dark template (black plate, white type) that clashed with
#     epubs carrying real covers. New 4th template push_delivery (grained paper, a
#     "phone -> waves -> book" motif, title at the bottom) drawn with the existing
#     grayscale primitives - no image asset. Cache versions bumped or stale entries keep
#     winning: BOOK_AUTO_COVER_VERSION 4->5, FAST_COVER_REVISION 1->2.
#   (2) clock drift (minutes per day, 34 minutes accumulated) - the root cause is a
#     hardware fact no config can undo: the PMU docs state "LSI ~32 kHz feeds IWDT and
#     RTC; no crystal", and in sleep the PMU drops the ESP rail, so the PMU's internal
#     RC carries time alone. Three fixes: learned drift compensation (persist true UTC and
#     the pre-correction local second per sync, estimate ppm, correct the PMU value at
#     boot - works offline; EMA smoothing, samples under 1 h or beyond 2% discarded,
#     no compensation on backwards jumps or >24 h spans); offline resync shortened
#     6 h -> 2 h; and time over BLE via the new CTRL opcode PUSH_OP_SET_TIME (0x05)
#     carrying 4 LE bytes of UTC. BEGIN is a frozen 83-byte layout, so it is untouched;
#     SET_TIME is deliberately outside the "must be armed" gate - the only high-frequency
#     channel when there is no WiFi. The APK was rebuilt to send it.
#   (3) anti-crosstalk (MAC binding) - the adv name used to be random (PicoPush-XXXX), so
#     two panels in a cafe could collide and a phone could push to the wrong screen. The
#     token now derives from the factory MAC's last two bytes (mac_token()), making the
#     name stable and unique (PicoPush-1A2B); the on-screen name comes from the same
#     source. BEGIN's token check is fully retained (same origin as the name). The APK no
#     longer "connects to the first hit": it collects every PicoPush- device, listens
#     2.5 s past the first hit, auto-connects **only when there is exactly one**, and
#     otherwise shows a picker keyed to the code on the screen.
#   Gate: push_sink 168 checks / 0 red; APK protocol table 25/25 (build-apk.sh); fullclean
#     rebuild exit 0 with 8 warnings (same baseline set, none added). Final anchor:
#     4,060,496 B (+6,512 vs gen21), sha256 0da62846..., seg1 File offs 0x00233888
#     (+0x110 DROM shift), paddr 0x00243890; seg1 Load addr 0x3fc9c700 unchanged.
#     partition-table / ota_data_initial byte-identical to gen21. APK sha256 6e26875a...
#
# 重锚记录（2026-10-20，gen20 真机状态条打磨修复轮 / gen21）：
#   版本串：0.3.3-pico21（CMakeLists.txt 一处 + 本记录 + 烧录说明，三处同步）。
#   绝对红线：本次仅为蓝牙推流优化黑色状态条，「最近阅读」大卡片（通用组件）零改动。
#   两项修复（用户 gen20 真机截图实证）：
#   ① 痛点2 状态条文字垂直不居中：根因 = draw_push_card 里 label_on 委托
#     ui_text_fixed_context_aa_vc，第三参是「垂直中心」而非顶边；gen20 误按顶边式
#     传 566+(64-22)/2=587 → 文字中心整体偏高 11px。修正为 598（卡体 566..630 的
#     真中心）。全仓 _vc 调用点仅 2 处，另一处（app_dashboard 时钟）语义自洽不动。
#   ② 痛点1 未读提醒（黑白反色）：读到 100% 大卡变空后，新推送到达黑条无变化、
#     用户错过。修法 = push_sink COMMIT 成功（仅文本推送，单词卡不置位）置「未读位」，
#     NVS 持久化（复用 PUSH_NVS_NS "rp_pushsink" 独立 key，不动 session blob，重启不丢）；
#     开书成功即清（open_book_impl 唯一汇合点）；快照差分驱动重绘（push_card_snap_t
#     加 .unread 位，零新增轮询）。
#   用户拍板定稿（终版形态，覆盖初稿两处）：
#     · 文案「有新内容」；已读判定 = 打开即清（方案 A）；
#     · 徽章形态弃 104x32 大胶囊 → **实心反色小圆点**（12x12 圆角块 r=6 ≈ 圆）
#       + 旁注「有新内容」，右缘 624、垂直中心 598——墨水屏更清爽；
#     · **未开启（IDLE）态绝不亮点**：未开启收不到数据，且白条里黑底白字块突兀；
#       IDLE 有历史未读时右侧文字直接显示「未开启·有未读内容」；
#     · 未读只在武装（ARMED/WINDOW）与连接（CONNECTED/COOLDOWN）态显示；
#       在场时右侧状态文转短式（去广播名）。
#   ③ 手动息屏体验修复（推翻 gen18 豁免覆盖表①条目）：武装期按电源键想息屏
#     曾被 enter_lock_and_sleep 拦截（日志 `lock refused: push sink armed`），反人类。
#     改为手动电源键拥有最高优先级——prepare_lock 加 manual 标志，手动路径
#     `push_sink_cancel_why("manual sleep")` 静默取消武装（安全关射频）→ 日志
#     `armed canceled by manual sleep` → 正常息屏；唤醒后状态卡回 IDLE/「未开启」闭环成立。
#     自动锁屏/深睡（manual=false）仍豁免保活（M1.5 关屏保活），由 enter_lock_and_sleep 拒锁，不在此取消。
#     技术风险点「手动 vs 自动」已规避：on_before_lock 既被手动也被自动调用，故 cancel 上移到
#     prepare_lock 并仅对 manual 触发；五个调用点中 502/516（run_power_action→深睡自带 cancel）
#     与 1133（auto_lock_due）传 false，735/915/998（手动电源键/关机页/长按）传 true。
#   Gate：push_sink 纯核 168 checks 0 red；fullclean 全量重建 exit 0，警告 **8 条与
#     基线同集**（app_book×7 + ui_pinpad×1，全部 unused-function，gen21 增 0）。
#   三次修订链（同一 fork 专属串 pico21，仅重编）：
#     · 初稿 `28b1474d…` 4,053,792 B（居中 + 104×32 大胶囊未读徽章）；
#     · 拍板修订 `89bb284d…` 4,053,936 B（+144B：IDLE 不亮块 + 小圆点 + 旁注）；
#     · 终版 `c6e433dd…` 4,053,984 B（+48B：手动息屏 prepare_lock 标志 + cancel 调用）。
#   终版锚：seg1 File offs `0x00233778`（gen20 0x00233708，DROM 段随三轮代码增长
#     +0x70=112B 顺移）、paddr `0x00243780`，EXPECTED 随之重锚。
#   bootloader 21,232 B（哈希每代随构建时间戳必变，零行为变化）；partition-table /
#     ota_data_initial 与 gen20 逐字节一致（sha256 9d5d890b… / 7d2c7ac4…）。
# Re-anchor record (2026-10-20, gen20 status-bar polish fix round / gen21):
#   Version string: 0.3.3-pico21. Absolute red line: this round only touches the
#   black status bar for BLE push; the "recent reading" big card (shared
#   component) is untouched. Two fixes (user screenshots on gen20 hardware):
#   (1) vertical centering - draw_push_card delegated label_on to
#     ui_text_fixed_context_aa_vc whose 3rd arg is a VERTICAL CENTER, not a top
#     edge; gen20 passed 587 (top-edge formula) so the text sat 11px high; now
#     598, the true center of the 566..630 card. (2) unread reminder - COMMIT
#     success (text pushes only) sets an unread bit persisted in NVS (own key in
#     the existing "rp_pushsink" namespace; survives reboot), cleared on
#     successful book open (open_book_impl); snapshot-diff redraw via a .unread
#     bit in push_card_snap_t, zero new polling. Final form per user ruling: a
#     solid inverted 12x12 dot (r=6) plus the caption "有新内容" (replacing the
#     104x32 capsule), right edge 624, vcenter 598; ARMED states only - IDLE
#     never shows the dot and simply says "未开启·有未读内容" in the right text;
#     while up, the right status text drops the advertise name. (3) manual
#     screen-off no longer refused - prepare_lock gained a manual flag; manual
#     power-key paths call push_sink_cancel_why("manual sleep") then sleep
#     (log "armed canceled by manual sleep"); auto lock keeps the M1.5 exemption
#     (still refused by enter_lock_and_sleep).
#   Gate: push_sink 168 checks / 0 red; fullclean rebuild exit 0 with 8 warnings
#     (same baseline set, gen21 adds none). Final build: 4,053,984 B (+192 B vs
#     gen20), sha256 c6e433dd..., seg1 File offs 0x00233778 (+0x70 DROM shift),
#     paddr 0x00243780; EXPECTED re-anchored.
#
# 重锚记录（2026-10-20，gen19 真机验收后状态条打磨轮 / gen20）：
#   版本串：审计③点名 gen19 漏 bump（出厂仍带 0.3.3-pico18，TF 升级判同版本会拒）。
#     本代恢复每代一跳 → 0.3.3-pico20（CMakeLists.txt 一处 + 本记录 + 烧录说明，三处同步）。
#   三处真机打磨（用户 gen19 截图实证）：
#   ① 问题1「长文推送后状态条文字不更新」+ 问题2「信息冗余」合并修：砍掉状态条
#     副行「最近：xxx」。根因 = 该副行与大卡右上推送摘录重复，且长文 BEGIN.title 为空时
#     book_title_set 被 valid_utf8 拒（book_title.c:53 空串/首尾空格/控制字符一律拒），
#     副行冻在上一条有标题的推送（截图「最近: 橡胶」）。删行后症状类整体消失。
#   ② 状态条单行化（已武装/未开启两态一眼可分）：
#     · 未开启＝白底 + 浅灰描边(0xb0) + 暗淡字(0x90)；
#     · 已武装＝纯黑底(0x00)反白字(0xff) + 右侧带广播名（等待连接·剩 X 分钟 · PicoPush:XXXX）。
#   ③ take_committed 机制保留（缓存写回书架/阅读器标题覆盖），但不再强制重绘（行已删，
#     卡片由状态快照差分驱动）。push_sink 纯核 168 checks 0 red 不受影响。
#   Gate：push_sink 纯核 168 checks 0 red；全仓 fullclean 全量 **8 警告**（app_book×7 + ui_pinpad×1，
#     全部 unused-function，与 gen18/19 同集，gen20 增 0）；增量零错误。
#   改后重编：Read_Pico.bin 4,053,792 B（相对 gen19 4,053,856 **-64 B**——状态条副行代码删除致缩，
#     相对 gen18 4,053,392 **+400 B**），sha256 `71f05db1…`、seg1 File offs `0x00233708`、paddr `0x00243710`，
#     EXPECTED 随之重锚。
#
# 重锚记录（2026-10-20，gen18 真机回归 5 Bug 修复轮 / gen19）：
#   根因（gen18 真机测试逐条实证）：底层防假死 / 连续推流完美，但暴露 4 个 UI/状态机 Bug + 1 个底层刷新 Bug，本次一并修出。
#   ① Bug1 状态卡 UI 错位（黑条太扁、第二行文字贴边蒸发）：PUSH_CARD_RECT 高 54→64、文字 y 坐标重排（app_dashboard.c）。
#   ② Bug2 推送入口点击概率性失效（首页卡有时进不去、「手机推送」按钮每次需重进）：app_transfer.c method_gesture origin==2 当场 push_arm_now 武装（修入口失效）；
#     app_dashboard.c on_tick 手势期禁刷状态卡（修点击被吞）。
#   ③ Bug3 退出阅读器后状态卡异常（空数据 / 显示「断开连接 / 未开启」）：app_book.c / app_transfer.c 摘除错误 push_sink_cancel（日志实证 armed canceled by 退出阅读器）。
#   ④ Bug4 底层刷新欠载（line buffer underrun + pclk 回 12MHz，BLE 高速收数并发抢 SPI/CPU）：display.c 加 pclk 策略 + 武装期收口不必要的局部刷新。
#   ⑤ Bug5 首页「最近推送」文字不更新（连续推送卡在首推旧值「but」）：根因是 book_title_set(PUSH_TEXT_PATH,...) 仅在 app_transfer 前台消费 take_open 时调用，
#     首页前台时缓存不更新；且 s_push_snap 不含标题、push_card_due 不重绘。修法：push_sink 新增 take_committed 一次性事件（每次成功 COMMIT、非单词卡触发），
#     首页 on_tick 消费后写回 book_title_set 缓存并强制状态卡局部重绘。
#   Gate：push_sink 纯核 168 checks 0 red、全仓增量编译零错误、fullclean 全量 8 警告（app_book×7 + ui_pinpad×1，与 gen18 同集，增量编译仅重编改动文件故一度被误报为 7）。
#   改后重编：Read_Pico.bin 4,053,856 B（相对 gen18 4,053,392 **+464 B**），sha256 `80901528…`、seg1 File offs `0x00233708`、paddr `0x00243710`，EXPECTED 随之重锚。
#
# 重锚记录（2026-10-20，无缝连续推送体验修复轮 / gen18 — 含 18-a 保活 + 自动切回 + 首页 deep-link + 状态卡 + 假武装排雷）：
#   根因（gen17 真机日志逐条实证 + push_session 状态机全转移审计）：推长文开书后蓝牙断开、连推多段需手动退出阅读器→重进接收页→等重连。
#   断裂点 100% 在 UI 层——push_session 状态机 PUSH_STATE_COOLDOWN 已内置连续推流全部转移（DISCONNECT→WINDOW、CONNECT→CONNECTED、
#   BEGIN→RECEIVING），协议层零改动。gen17 日志铁证：verify ok(COOLDOWN) → transfer leave(+100ms，transfer_on_exit cancel) → 手机
#   notify off+disconnected（APK finally closeQuietly()，比设备晚 2s，两边各自拆台）。反证：单词卡回合手机断开→设备回 WINDOW 继续广播→重连二推成功。
#   假武装排雷（静默失败零容忍，任何武装期 BLE 拆除必须打「armed canceled by X」）：全量 grep 通往 ble_pt_stop / 深睡的入口，逐一补 cancel+原因，共 10 路——
#     ① 电源键息屏（app_device_settings.c:2310 on_before_lock / app_loop.c prepare_lock）：**gen21 推翻原「豁免保活」裁定**——手动电源键（息屏/休眠）
#        拥有最高优先级，不再拦截（`lock refused: push sink armed` 退出历史）。改为在 prepare_lock 加 manual 标志、手动路径
#        `push_sink_cancel_why("manual sleep")` 静默取消武装（安全关射频）+ 日志 `armed canceled by manual sleep` → 正常息屏；唤醒后
#        状态卡回 IDLE/「未开启」闭环成立。自动锁屏/深睡（manual=false）仍豁免保活（M1.5 关屏保活），由 enter_lock_and_sleep 拒锁。
#        on_before_lock 的 `if(!push_sink_armed()) ble_pt_stop` else「screen-off skipped ble stop」仅对自动路径生效（手动路径到达时已拆栈）。
#     ② 飞行模式开（app_device_settings.c:1724 flight_mode_toggle）：先 `push_sink_cancel_why("飞行模式已开启，接收暂停")` 再 stop；
#     ③ 关机/深睡（sleep.c:421 app_enter_host_sleep）：先 `push_sink_cancel_why("关机/深睡，接收结束")` 再 stop；
#     ④ USB 存储模式（usb_storage.c:81 usb_storage_start）：`push_sink_cancel_why("USB 传书已接管，接收暂停")`；
#     ⑤ TF 卡拔出（app_transfer.c:1384 transfer_on_media_lost）：`push_sink_cancel_why("TF 卡已拔出，接收结束")`；
#     ⑥ 退出阅读器（app_book.c:6593 book_on_exit）：`else push_sink_cancel_why("退出阅读器，接收结束")`；
#     ⑦ 用户主动退出接收（push_sink.c:1453 push_sink_cancel）：包装 `push_sink_cancel_why("已退出接收")`；
#     ⑧ 栈停兜底（push_sink.c:1365 push_sink_on_stack_stop）：武装期打「stack stopped while armed」覆盖 WiFi 租约等静默入口；
#     ⑨ 锁屏认证（lock_screen.c:87 lock_screen_authenticate）：武装期已在 enter_lock_and_sleep:489 拒锁（`lock refused: push sink armed`），此路不可达——**豁免（上游拒锁）**；
#     ⑩ 拔电收官（push_sink.c poll 拔电判定）：`close_why_set("充电线已拔出，接收结束")` 覆盖 UNPLUG 与宽限收官两路。
#   新增 API：`push_sink_cancel_why(const char*)` / `push_sink_last_close_why(void)`；`push_sink_arm()` 清原因、`push_sink_poll()` 拔电补原因、`ui_post_closed()` 空原因补默认「接收已结束」。
#   gen18 四类功能：① 18-a 保活——transfer_on_exit 条件化（交接标记 s_push_handed_off 双向）+ book_on_exit 收摊；② 自动切回——CONNECT 事件判 s_path==PUSH_TEXT_PATH → request_app=app_transfer
#   （app_book.c on_tick / app_transfer.c on_enter，不重新武装）；③ 首页 deep-link——首页推流卡只跳传书页，绝不静默武装；④ 首页状态卡案1——读 push_sink_state()：IDLE 暗态/ARMED 亮+倒计时/
#   CONNECTED·RECEIVING 进度/最近一推标题，APK 零改动。
#   门禁：13 个 host runner 全绿（含补桩的 transfer_ui/book_management/test_usb_storage）；fullclean 重编 8 条警告与基线函数名集合逐一对上（app_book.c 7 + ui_pinpad.c 1）；
#   partition-table/ota_data_initial 与 gen17 逐字节相同（sha256 9d5d890b…/7d2c7ac4…）；闪前自检 EXPECTED 重锚至本代 Read_Pico.bin 哈希。
# Rebuild record (2026-10-20, seamless continuous-push UX fix round / gen18 — 18-a keep-alive + auto-switchback + home deep-link + status card + false-arm mine sweep):
#   Root cause (gen17 device log, line by line, plus a full push_session state-machine transition audit): after pushing a long text and opening the book, BLE
#   drops and pushing another segment forces a manual exit from the reader, re-enter the receiver page and wait for reconnect. The break is 100% in the UI layer —
#   push_session's PUSH_STATE_COOLDOWN already holds every continuous-push transition (DISCONNECT->WINDOW, CONNECT->CONNECTED, BEGIN->RECEIVING) with zero protocol change.
#   gen17 log proof: verify ok(COOLDOWN) -> transfer leave (+100ms, transfer_on_exit cancel) -> phone notify off + disconnected (APK finally closeQuietly(), 2s later
#   than the device, both sides tearing down). Counterexample: single-card round, phone disconnects -> device returns to WINDOW, keeps advertising -> reconnect, second push succeeds.
#   False-arm sweep (zero tolerance for silent failure; any BLE teardown while armed must log "armed canceled by X"): grep every entry to ble_pt_stop / deep sleep, add cancel+why to each, 10 paths —
#     ① power-key screen-off (app_device_settings.c:2310 on_before_lock / app_loop.c prepare_lock): **gen21 reverses the old "exempt keepalive"
#        ruling** - a manual power key (screen-off/sleep) now outranks the arm and is never refused ("lock refused: push sink armed" is retired);
#        prepare_lock gained a manual flag and the manual path calls push_sink_cancel_why("manual sleep") (silent disarm + "armed canceled by
#        manual sleep" log) then sleeps; on wake the status card returns to IDLE/"未开启". Auto lock/deep-sleep (manual=false) keeps the
#        exemption (M1.5 screen-off keepalive) and is still refused by enter_lock_and_sleep.
#     ② flight mode on (app_device_settings.c:1724): push_sink_cancel_why("飞行模式已开启，接收暂停") before stop;
#     ③ power-off / deep sleep (sleep.c:421 app_enter_host_sleep): push_sink_cancel_why("关机/深睡，接收结束") before stop;
#     ④ USB storage (usb_storage.c:81 usb_storage_start): push_sink_cancel_why("USB 传书已接管，接收暂停");
#     ⑤ TF card out (app_transfer.c:1384 transfer_on_media_lost): push_sink_cancel_why("TF 卡已拔出，接收结束");
#     ⑥ exit reader (app_book.c:6593 book_on_exit): else push_sink_cancel_why("退出阅读器，接收结束");
#     ⑦ user quit (push_sink.c:1453 push_sink_cancel): wraps push_sink_cancel_why("已退出接收");
#     ⑧ stack-stop fallback (push_sink.c:1365 push_sink_on_stack_stop): "stack stopped while armed" covers WiFi-lease silent entries;
#     ⑨ lock auth (lock_screen.c:87 lock_screen_authenticate): while armed, enter_lock_and_sleep:489 already refuses lock ("lock refused: push sink armed"), path unreachable — EXEMPT (upstream refuse);
#     ⑩ unplug (push_sink.c poll): close_why_set("充电线已拔出，接收结束") covers both UNPLUG and grace-period wind-down.
#   New API: push_sink_cancel_why(const char*)/push_sink_last_close_why(void); push_sink_arm() clears the why, push_sink_poll() sets it on unplug, ui_post_closed() defaults empty to "接收已结束".
#   gen18 four features: (1) 18-a keep-alive — transfer_on_exit conditional (s_push_handed_off both ways) + book_on_exit cleanup; (2) auto-switchback — CONNECT judges
#   s_path==PUSH_TEXT_PATH -> request_app=app_transfer (app_book.c on_tick / app_transfer.c on_enter, no re-arm); (3) home deep-link — the home push card only jumps to the transfer page, never silently arms;
#   (4) home status card plan-1 — reads push_sink_state(): IDLE dim / ARMED bright+countdown / CONNECTED·RECEIVING progress / last push title, zero APK change.
#   Gate: all 13 host runners green (incl. restubbed transfer_ui / book_management / test_usb_storage); fullclean rebuild, 8 warnings match the baseline function-name set (app_book.c 7 + ui_pinpad.c 1);
#   partition-table/ota_data_initial byte-identical to gen17 (sha256 9d5d890b…/7d2c7ac4…); pre-flash self-check EXPECTED re-anchored to this build's Read_Pico.bin hash.
#   审计逐行复查补丁（门禁核销后、烧录前）：全量 grep `ble_pt_stop`/`ble_pt_sync(false)`
#   暴露第 6 处 BLE 拆除入口 `app_loop.c:141 quick_toggle_wifi()` 的 `ble_pt_sync(false)`
#   武装期无 `push_sink_armed()` 守卫——原覆盖表漏列。补与息屏同款豁免守卫
#   （`if(!push_sink_armed()) ble_pt_sync(false); else log "wifi toggle skipped ble stop"`），
#   全仓 BLE 拆除入口现全闭（2 处豁免守卫 + 2 处预期拆除带 cancel + 1 处上游拒锁不可达
#   + on_stack_stop 全局兜底日志）。改后重编：Read_Pico.bin 4,053,392 B（+64B），
#   sha256 `2fafca21…`、seg1 File offs `0x002336e8`、paddr `0x002436f0`，EXPECTED 随之重锚。
#
# 重锚记录（2026-10-20，单词卡 ERR(9) 误报修复轮 / gen17）：
#   用户举证：TF 卡 /sdcard/dict/ 下确实有 dict.bin，阅读时长按查词完全正常，
#   但蓝牙推流的单词卡报 ERR(9)「无词典」。gen16 把原因赖到「卡上没词典」——
#   **判错了**。
#   逐条排除用户提出的三个假设：① 路径拼写 —— DICT_DIR 恒为绝对路径
#   "/sdcard/dict"，前缀不缺，排除；② 文件系统并发锁 —— ERR(9) 出现在 37681ms，
#   早于推流 rename 失败（67612ms），不可能是它的后果；且 CONFIG_FATFS_FS_LOCK=0
#   （FatFs 未启用锁）；且 dict_ready() 只读内存指针 s_dict.file，根本不 fopen，
#   排除；③ VFS 挂载 —— 需实测，已加 errno 日志待证。
#   真根因：探针挂的是 `dict_ready()`，其语义是「词典句柄当前是否已打开」
#   （`s_dict.file != NULL && s_dict.sparse != NULL`），**不是「卡上有没有文件」**。
#   词典是惰加载的：只有查词/预热才真正 fopen。长按查词之所以正常，是
#   dict_popup.c 里写着 `if (!dict_ready()) ready = dict_open();` 会自己打开；
#   而推送的 BEGIN 前置闸门只**读**这个状态、从不尝试打开 → 开机后一次词都没查过
#   时，单词卡必然误报 ERR(9)。
#   修法：探针改为 `dict_warm()`（已打开则 true，否则尝试 dict_open()，且失败
#   **不武装退避**——其注释正是为「卡片可能还没就绪」而写）；并在 dict_open /
#   try_open 打印尝试的完整绝对路径与 errno；push_sink 的 err=9 日志改为诚实措辞
#   「探针此刻判定不可用，不等于卡上没有词典」。
# Rebuild record (2026-10-20, word-card ERR(9) false-positive fix round / gen17):
#   the user showed that /sdcard/dict/ really holds dict.bin and that long-press
#   lookup works, yet pushed word cards answered ERR(9) "no dictionary". gen16
#   blamed "no dictionary on the card" - **that diagnosis was wrong**.
#   The user's three hypotheses, ruled out one by one: (1) path spelling -
#   DICT_DIR is the absolute "/sdcard/dict", the prefix is not missing, ruled out;
#   (2) filesystem lock - ERR(9) fired at 37681 ms, BEFORE the push rename failure
#   (67612 ms), so it cannot be a consequence; CONFIG_FATFS_FS_LOCK=0 (FatFs has
#   no locking); and dict_ready() only reads the in-memory pointer s_dict.file
#   without any fopen, ruled out; (3) VFS mount - needs measurement, errno logging
#   added for that.
#   Real root cause: the probe was `dict_ready()`, whose meaning is "is the
#   dictionary handle currently open" (`s_dict.file != NULL && s_dict.sparse !=
#   NULL`), **not** "does a dictionary exist on the card". The dictionary is loaded
#   lazily - only a lookup or a warm-up actually opens it. Long-press lookup works
#   because dict_popup.c says `if (!dict_ready()) ready = dict_open();`, i.e. it
#   opens the file itself; the push BEGIN pre-check only **reads** that state and
#   never tries to open, so a word card always misfired ERR(9) when no word had
#   been looked up since boot.
#   Fix: probe `dict_warm()` instead (true if already open, otherwise try
#   dict_open(), and a failure does **not** arm the back-off - its comment is
#   written exactly for "the card may not be ready yet"); log the full absolute
#   path attempted plus errno in dict_open/try_open; and reword the push_sink
#   err=9 log honestly - "the probe says unavailable right now, which is not proof
#   that the card lacks a dictionary".
#
# 重锚记录（2026-10-20，gen15 真机「再推全失败」修复轮 / gen16）：
#   根因两条叠加：① FatFs 的 f_rename **不覆盖已存在目标**（FR_EXIST → EEXIST），
#   POSIX 的「原子替换」语义它没有——首轮 push.txt 不存在所以成功，**第二轮起
#   rename 必败** → reply_err(ERR(5))，APK 显示「TF 卡不可用」（文案误导，实为
#   EEXIST）。② do_verify() 递归进 exec_actions 时 COMMIT 与 OPEN_BOOK 同位携带，
#   do_commit() 原返回 void、失败无法短路 → **旧 push.txt 被照样弹出**。
#   真机铁证：`verify ok: sz=30 crc=0x15542824` 紧跟 `commit rename: File exists`，
#   其后 `book: open start` 弹出的仍是首次内容。
#   修法：① rename 前先 remove(旧 push.txt)；② do_commit() 改返回 bool，
#   exec_actions 里提交失败即清除 OPEN_BOOK/WORD_CARD 位；③ RESUME 通道加 sid
#   诊断日志（审计裁定：保留分支、改造为诊断通道）；④ APK 的 ERR(5) 文案改诚实。
# Rebuild record (2026-10-20, gen15 on-hardware "every later push fails" round /
# gen16): two root causes stacked. (1) FatFs's f_rename **never overwrites an
# existing target** (FR_EXIST -> EEXIST) - the POSIX "atomic replace" semantics
# are absent - so the first push works (no push.txt yet) while **every later one
# fails the rename** -> reply_err(ERR(5)), shown by the APK as "TF card
# unavailable" (misleading: it is really EEXIST). (2) do_verify() re-enters
# exec_actions with COMMIT and OPEN_BOOK carried together and do_commit() returned
# void, so a failure could not short-circuit - the OLD push.txt was opened anyway.
# Hardware proof: `verify ok: sz=30 crc=0x15542824` immediately followed by
# `commit rename: File exists`, and then `book: open start` still opened the
# first push's content.
#   Fixes: (1) remove() the old push.txt before rename; (2) do_commit() returns
# bool and exec_actions clears OPEN_BOOK/WORD_CARD on failure; (3) the RESUME path
# logs its sid (audit ruling: keep the branch, turn it into a diagnostic channel);
# (4) the APK's ERR(5) label is now honest.
#
# 重锚记录（2026-10-19，gen14 真机 CRC 失败修复轮 / gen15）：
#   根因：ESP-IDF 的 FATFS VFS **没有注册 .flush_p**（vfs_fat.c 全文零 "flush"），
#   fflush() 只把 newlib 缓冲推进 f_write，不触发 f_sync；sdkconfig 实证
#   CONFIG_FATFS_IMMEDIATE_FSYNC 未开，f_write 后也不补 f_sync —— 于是 .part 的
#   **目录项 size 永不更新**。do_verify() 紧随其后的 stat()（读目录项）与
#   fopen("rb") 回读拿到过期值：长度对账必然不等、CRC 必然不符 → 真机 err=4
#   (PUSH_ERR_CHECKSUM)，UI「内容校验失败，请重新发送」。
#   修法：回读前 part_close()（f_close 内部第一行即 f_sync，见 ff.c:4345），
#   顺带消掉 FATFS FS_LOCK=0 下「同一文件两次打开」的非法打开（ffconf.h 明文禁止）。
#   附带修复：session_id 恒定导致「一次失败→永久续传」卡死（APK 改逐次唯一 sid）；
#   NVS 检查点补 part_sync()，让「.part 可见长度不超前于记账」这条注释真正成立。
# Rebuild record (2026-10-19, gen14 on-hardware CRC failure fix round / gen15):
#   root cause: ESP-IDF's FATFS VFS registers **no .flush_p** (zero "flush" in
#   vfs_fat.c), so fflush() only pushes the newlib buffer into f_write without
#   f_sync, and CONFIG_FATFS_IMMEDIATE_FSYNC is off (sdkconfig) so f_write does
#   not sync either - the .part **directory entry never learns the new size**.
#   The stat() (directory entry) and fopen("rb") readback that follow in
#   do_verify() therefore read stale values: the length reconciliation can never
#   match and the CRC can never agree -> on-hardware err=4 (PUSH_ERR_CHECKSUM),
#   UI "内容校验失败，请重新发送".
#   Fix: part_close() before the readback (f_close's first statement is f_sync,
#   see ff.c:4345), which also removes the duplicate open that ffconf.h forbids
#   under FATFS FS_LOCK=0. Also fixed: a constant session id deadlocked every push
#   into a permanent resume after one failure (APK now issues a distinct sid per
#   push); the NVS checkpoint now calls part_sync() so the "visible length never
#   runs ahead of the counters" comment finally holds.
# 烧前自检：Read_Pico.bin 的 sha256 必须与锚定值逐字一致，杜绝烧错 bin。
# Pre-flash self-check: Read_Pico.bin's sha256 must match the anchor exactly.
EXPECTED="fdd48866a8cc18237f0abc06f9c492d4e67c1ce8d39ae35b2f7ef9762331248d"
if command -v sha256sum >/dev/null 2>&1; then
    ACTUAL=$(sha256sum "$DIR/Read_Pico.bin" | cut -d' ' -f1)
elif command -v shasum >/dev/null 2>&1; then
    ACTUAL=$(shasum -a 256 "$DIR/Read_Pico.bin" | cut -d' ' -f1)
else
    ACTUAL=""
    echo "WARN: no sha256 tool found; skipping pre-flash check" >&2
fi
if [ -n "$ACTUAL" ] && [ "$ACTUAL" != "$EXPECTED" ]; then
    echo "FATAL: Read_Pico.bin sha256 mismatch - refusing to flash." >&2
    echo "  expected: $EXPECTED" >&2
    echo "  actual:   $ACTUAL" >&2
    echo "  Check package integrity; do NOT flash a bin off its anchor." >&2
    exit 1
fi
[ -n "$ACTUAL" ] && echo "sha256 OK: $ACTUAL (production build, anchor verified)"

python -m esptool --chip esp32s3 -b "$BAUD" \
  --before default-reset --after hard-reset write-flash \
  --flash-mode dio --flash-size 16MB --flash-freq 80m \
  0x0      "$DIR/bootloader.bin" \
  0x8000   "$DIR/partition-table.bin" \
  0x10000  "$DIR/Read_Pico.bin" \
  0xb10000 "$DIR/ota_data_initial.bin"

echo "Flash done (production build)."
