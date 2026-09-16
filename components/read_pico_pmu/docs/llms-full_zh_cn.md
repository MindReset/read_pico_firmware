# 小纸 Pico（Read Pico）电源管理 · 主机对接（固件 1.0.8 / 协议 1.1）

小纸 Pico 是深圳思维重置科技有限公司推出的一块 ESP32-S3 加 4.7 寸单色墨水屏的开发板，面向希望为墨水屏设备编写开源固件的开发者。本文是这块板上电源管理芯片 read_pico_pmu 的主机对接说明。

配套文件：同目录 `pmu_protocol_zh_cn.h`、`pmu_registers_zh_cn.json`。印刷版：同目录 `read_pico_pmu_datasheet_v1.0.8_zh_cn.pdf`。

## 摘要

- 芯片 read_pico_pmu，用于小纸 Pico（Read Pico）。硅片：武汉芯源 CW32L010F8U6，QFN20 3.0×3.0 mm。一次性烧录，无 OTA / IAP。
- I2C 7 位地址 0x2A（写 0x54，读 0x55）。400 kHz，兼容 100 kHz。小端。
- device_id 0x3201。IDENTITY 魔数 ASCII "PMU1"。协议 1.1。固件 1.0.8。build_hash 20260905。hw_revision 1。
- 寄存器：0x00 IDENTITY 32R；0x20 STATUS 64R；0x60 DIAG 32R；0x80 CMD 64W；0x81 RESP 64R；0x82 PEEK 16R；0x83 ACK 4W；0x84 COUNT 1R；0x85 QUICK_BATTERY 8R。
- 命令帧与响应均为 64 字节。payload 最多 44。magic 0xA5。session_id 必须等于当前 boot_id。sequence 禁止 0。
- CRC16-CCITT-FALSE：poly 0x1021，init 0xFFFF，refin/refout false，xorout 0。向量 "123456789" = 0x29B1。覆盖到 crc 字段之前。
- 事件 FIFO 深度 8，每条 16 字节。CW_INT 开漏，低电平表示有未 ACK 事件。PEEK 不弹出；ACK = event_id u16 + crc16（只对这 2 字节）。
- 电源状态看 STATUS.power_state，不看命令返回码。充电状态看 charge_state，不用电压判满。
- 断电、复位、下载等动作使用 PREPARE(0x0300)+COMMIT(0x0301)。token 约 2 s。COMMIT 后 delay 上限 5 s。
- DeepSleep 期间 I2C 不响应。逻辑关机或软睡完成后从机停止应答，须再拉起主机才会重新应答。冷启动后尚未关从机时，短按可唤醒，通信窗口 15 s。
- VCOM_GET 0x0510 / VCOM_SET 0x0511。单位绝对值 mV，500–2500 且对齐 10 mV。未标定 valid=0、mv=0。非法值 INVALID_ARGUMENT。FACTORY_DEFAULT 只恢复 RAM 开关，不改 VCOM。
- 读保护经 ISP 设定。MAINT_GET_INFO（0x05F2）可读 UID、循环、更换次数、维修码与读保护次数。
- 主机启动：读 IDENTITY → 存 boot_id → 收 HOST_STARTED → HOST_READY(boot_reason) → TIME_SYNC。sequence 从 last_command_sequence+1 起。

## 分工

read_pico_pmu 在板上常供电，负责供电时序、电池、充电检测、按键。主机 ESP32-S3 只在 ESP_EN=1 时运行，负责业务、UI、落盘、发命令、收事件、校时、读写 VCOM。充电检测和电压换算由 PMU 完成，主机直接读取结果。

## 引脚（QFN20 顶视，逆时针，1 脚圆点）

1 PB07/NRST；2 PA00 ESP_EN 输出高有效；3 PA01 CW_INT 开漏低有效；4 VSS；5 Vcore（100–470 nF 到地，勿外供）；6 VDD 约 3.3 V；7 PA02 BAT_MEASURE_EN；8 PB00 CHARGE_STATUS；9 PB01 未用；10 PA03 BAT_ADC；11 PA04 ESP_BOOT；12 PA05 LED_RED PWM；13 PA06 LED_WHITE PWM；14 PA07 SWDIO / UART1_RXD（ISP）；15 PA08 SWCLK / UART1_TXD（ISP）；16 PB02 POWER_KEY 低有效；17 PB03 未用；18 PB04 未用；19 PB05 I2C_SDA；20 PB06 I2C_SCL。

## 电气与时钟

VDD 1.62–5.5 V，本板 3.3 V。工作温度 −40–85 °C。系统时钟内部 HSI/6 = 8 MHz。LSI 约 32 kHz，供给 IWDT、RTC。无外部晶振。Flash 64 KB，末页 0xFE00 为维护数据。SRAM 4 KB。硅片 DeepSleep 典型 0.3–1.2 µA。GPIO VIH 0.7 VDD，VIL 0.3 VDD。

## 电源状态 power_state

0 OFF，EN=0。1 POWERING_ON。2 BOOT_WAIT，等待 HOST_READY；超时 15 s 只置 fault_flags bit0，不断电。3 RUNNING。4 SHUTDOWN_PENDING，协作关机超时 10 s 后强制断电。5 RESETTING，EN 低 250 ms。6 DOWNLOAD_MODE，BOOT=0 后复位，约 500 ms 后释放 BOOT；主机发出 HOST_READY 后回到 RUNNING。7 FAULT，预留。8 SOFT_SLEEP，先改 STATUS，约 100 ms 后再拉 EN=0。此后短按只拉高开机。

### 软睡

仅 RUNNING 可进入。窗口内且 EN 仍为高时，HOST_READY 会取消下电并回到 RUNNING。EN 已为低时到达的 HOST_READY 丢弃。按键或闹钟开机时清掉尚未落地的软睡请求。SOFT_SLEEP 且 EN=0 后也可进入 DeepSleep。闹钟到点开机，wake_reason=6。

### 两阶段动作 action

1 HOST_LOGICAL_OFF → SHUTDOWN_PENDING。2 HARD_RESET。3 NORMAL_RESTART，同硬复位，BOOT=1。4 ENTER_DOWNLOAD。5 EXIT_DOWNLOAD 后硬复位。6 POWER_ON，仅 OFF，wake_reason=3。7 SOFT_SLEEP，仅 RUNNING。

PREPARE payload：action u8 + delay_ms u16 + reason u16，至少 5 字节。响应 token u32 + expires_in_ms=2000 + 回显 delay_ms。执行时 delay 上限 5000 ms。

便捷命令：0x0310 软睡；0x0311 关机（force u8 + reason u16）；0x0312 复位（reason u16）。同样受幂等约束。

协作关机：收到 SHUTDOWN_REQUESTED 后落盘，发 SHUTDOWN_READY(event_id)，再隔约 100 ms EN=0。强制关机：按住 10 s，或 force=1。电池 ≤3050 mV 时 PMU 发起协作关机，reason=13。

IWDT 1 s 溢出复位后自动给主机上电，wake_reason=4。RTC 继续走时；闹钟与 RAM 配置丢失。

last_action_reason：10 = 10 s 键；11 = 协作超时；12 = SHUTDOWN_READY 完成；13 = 低电；20 = 按键进下载。

## 按键

消抖 30 ms。EN=0 按住 ≥1 s 开机；电压有效且 ≤3050 mV 且未充电则拒绝开机，红灯闪两下。EN=0 且处于 SOFT_SLEEP（或 power_state 为 RUNNING 但 EN 已掉）时，短按即开机。EN=1 按住 ≥10 s 强制断电。EN=1 短按两次（间隔 ≤1.2 s）再长按 ≥3 s 进入下载；两下短按仍报 KEY_SHORT，第三下不报 KEY_LONG。短按、满 1.5 s、满 8 s 只报事件，不自动改 EN。KEY_DOWN / KEY_UP 可用 0x0503 关闭。

key_state：bit0 按下；bit1 已过 1.5 s；bit2 已过 8 s；bit3 下载组合已收齐两下短按。

## 电池

分压上臂 33 kΩ、下臂 100 kΩ，系数 1.33。BAT_MEASURE_EN 打开后等待 10 ms。用内部带隙反算 VDDA。有效范围 2700–4400 mV。连续 3 次失败清除 BATTERY_VALID。采样周期：运行约 30 s，关机未充电约 120 s，关机充电中约 60 s。上电后首次有效电压约 800 ms 内可得。立即采样用 0x0100，返回 ACCEPTED，完成后事件 0x11。

阈值：低电 3500 mV，事件 0x12；严重低电 3300 mV，事件 0x13；强制关机 3050 mV；滞回 100 mV。CONFIG 中的 full_battery_mv=4150 不用于判满。片内温度分辨率 0.01 °C，范围 −40–125 °C。

SOC 查表（千分比）：4200=1000，4100=960，4000=900，3900=780，3800=620，3700=450，3650=330，3600=200，3550=110，3450=70，3300=50，3150=20，3050=0，中间线性插值。未充电时单调递减。充电中保持充电前的值，充满置 1000。重新定基：充电结束、相邻采样跳变超过 500 mV。约 ±10 个百分点，用于电量条。低 SOC 通知默认关闭，默认阈值 50‰，回升滞回 30‰。等效循环：累计放电满 1000‰ 加 1。

## 充电 LY4566-420

charge_state：0 UNKNOWN；1 NOT_CHARGING；2 CHARGING（在位且 2 s 内有边沿）；3 FULL_INFERRED（在位且 3 s 无边沿）；4 FAULT（预留）。采样 20 ms；关机且未插充电器时 100 ms。拔出门限 = clamp(最近高半周×1.5+200, 1100, 1800) ms，尚无半周记录时用 1400 ms。EN 下降沿后 2.5 s 内不判拔出；上升沿后 2.5 s 内不判满。事件 0x10 的 arg0 为新状态、arg1 为前一状态。0x0501 开启后，确认插入且主机 OFF 时自动开机，wake_reason=5。该开关仅存 RAM，默认关。

## LED（优先级从高到低）

下载：0.15 s × 7，灭-白-灭-红-灭-白-灭。关机：红闪 150 ms × 4。上电：红 150 ms、灭 150 ms、白 150 ms、灭。主机 LED_SET 默认 5 s，或开机白闪 250 ms / 复位红闪 250 ms。充电：红呼吸，周期 2.6 s，暗段 0.3 s。充满：白常亮（BOOT_WAIT 不显示）。按住电源键：红常亮。EN=0 且未充电：灭。0x0504=0 关闭充电灯。LED_SET：color 0/1/2/3，brightness 0 为灭、非 0 为亮；2 字节默认淡入淡出 400 ms；4 字节带 fade_ms，0 为立刻切换，上限 4000 ms。led_state：bit0 红，bit1 白，bit2 呼吸，bit3 主机覆盖，bit4 充电策略占用。充电灯开启时，LED_SET 可能被充电指示盖住。

## 低功耗

普通 Sleep：I2C 可响应。DeepSleep 条件：power_state 为 OFF 或 SOFT_SLEEP，且 EN=0、未充电、按键与 LED 空闲、无未决命令，距上次命令和 I2C 活动均 ≥15 s。唤醒源：按键边沿、充电器插入、RTC 闹钟。无效唤醒立刻睡回。IWDT 在 DeepSleep 暂停。DIAG 偏移 14 为深睡进入次数。

DeepSleep 期间 I2C 不响应。冷启动后、从机尚未停应答时，短按（不足开机门限）可唤醒，15 s 内的 I2C 活动会延长窗口。逻辑关机、软睡完成或复位脉冲之后，从机停止应答；再次上电主机后恢复应答。

## 时间

TIME_SYNC 0x0005：UTC unix 秒，范围 946684800–4102444799（2000-01-01 00:00:00 至 2099-12-31 23:59:59）。对时时不钳死 SCL。TIME_GET 0x0007：unix u32（未校准为 0）+ millis u16（恒 0）+ synced u8 + 保留。复位后 synced=0。闹钟目标随校时平移，剩余秒数不变。ALARM_SET：mode 0 关 / 1 单次 / 2 循环 + seconds（10–2678400）。ALARM_GET：10 字节，mode + 保留 + remaining + target。闹钟状态只在 RAM。RTC_RAW 0x000A 返回 9×u32，供排障。事件 timestamp_ms 是 PMU 开机毫秒，不是 UTC。LSI 约 ±1–2%，须定期校时。

## I2C 事务

读：START + 0x54 + REG + RESTART + 0x55 + N + STOP。也兼容先写寄存器并 STOP，再读。写：START + 0x54 + REG + PAYLOAD + STOP。建议单次超时 20 ms，失败退避 2 / 5 / 20 ms，最多 3 次。总线卡死可发最多 9 个 SCL 后 STOP。从机约 100 ms 无总线活动则自行恢复。同一任务内串行访问，不要拆开一帧。

## IDENTITY 0x00（32 字节）

0 u8[4] PMU1；4 major=1；5 minor=1；6 fw 1；7 fw 0；8 patch 8；9 hw 1；10 u16 device 0x3201；12 u32 boot_id（不为 0）；16 u32 caps，bit0–5 为 1；20 max_frame=64；21 fifo=8；22 u16 build_number = 20260905 的低 16 位（0x4F09）；24 u32 build_hash=20260905；28 u16 0；30 crc16，覆盖 0–29。

boot_id 在每次 PMU 复位时更换。仅复位 ESP32 时不变。更换后须丢弃已保存的 sequence 与 token，重读 IDENTITY、STATUS 和事件。

## STATUS 0x20（64 字节）

0 u16 generation；2 u8 power_state；3 u8 wake_reason；4 u32 flags；8 u16 battery_mv；10 u16 adc_raw；12 u16 soc_permille；14 i16 temp_centi；16 u8 charge_state；17 u8 key_state；18 u8 led_state；19 u8 host_state（0 未知，1 关，2 启动中，3 就绪，4 软睡，5 关机中，6 故障）；20 u32 fault_flags，bit0 为 BOOT_WAIT 超时（粘滞）；24 u8 pending_events；25 u8 cw_reset_reason；26 u8 0；27 u8 last_command_status；28 u32 uptime_ms；32 u32 0；36 u32 config_generation；40 u16 last_command_sequence；42 u16 last_event_id；44 u32 last_action_reason；48–61 为 0；62 crc16，覆盖 0–61。

flags：0 BATTERY_VALID；1 LOW；2 CRITICAL；3 CHARGING_ACTIVE（仅 charge_state==2）；4 CHARGE_PIN_HIGH；5 KEY；6 EN_HIGH；7 BOOT_LOW；8 EVENT_PENDING；9 COMMAND_BUSY；10 CONFIG_VALID（恒为 1）；13 CW_INT_ASSERTED。

## QUICK_BATTERY 0x85（8 字节）

0 u16 mv；2 u16 soc，无效时 0xFFFF；4 u8 charge_state；5 flags：bit0 valid，bit1 soc_valid，bit2 charging；6 crc16，覆盖 0–5。

## DIAGNOSTICS 0x60（32 字节）

字段为 u16，饱和到 0xFFFF。CLEAR_DIAGNOSTICS 将前 30 字节清零。0 i2c_rx；2 总线恢复；4 crc；6 bad_len；8 unknown_cmd；10 保留（读为 0）；12 overflow；14 deepsleep；16 host_reset；18 保留（读为 0，IWDT 复位看 STATUS.cw_reset_reason==4）；20 adc；22 0；24 保留（读为 0）；26 保留（读为 0）；28 0；30 crc16，覆盖 0–29。

## 命令帧（64 字节）

0 magic 0xA5；1 header_version 1；2 kind：请求 0 / 响应 1；3 flags；4 proto major；5 proto minor；6 u16 sequence；8 u16 opcode；10 u16 status（请求填 0）；12 u8 payload_length，0–44；13 reserved；14 u32 session_id；18 payload[44]；62 crc16，覆盖 0–61。

幂等键为 (session_id, sequence)。相同请求重试返回缓存结果。相同键但 opcode 或 payload 不同则 SEQUENCE_CONFLICT。缓存 4 条。HOST_READY 命中缓存且当前不是 RUNNING 时仍会执行。断电、复位、下载命令按缓存处理，不重复执行。响应丢失时用同一 sequence 再读，不要换号重发。

## 状态码

0x0000 OK。0x0001 ACCEPTED，已受理，再读 STATUS 或等事件。0x0002 BUSY，预留。0x0010 BAD_CRC。0x0011 BAD_MAGIC。0x0012 BAD_LENGTH。0x0013 UNSUPPORTED_VERSION。0x0014 UNKNOWN_COMMAND。0x0015 INVALID_ARGUMENT。0x0016 SEQUENCE_CONFLICT。0x0017 STALE_SESSION。0x0018 NOT_SUPPORTED。0x0020 INVALID_STATE。0x0021 NOT_ARMED。0x0022 TOKEN_EXPIRED。0x0023 PERMISSION_DENIED。0x0030 CONFIG_INVALID。0x0031 NVM_FAILURE。0x0040 ADC_FAILURE。0x0041 HOST_TIMEOUT。0x00FF INTERNAL_ERROR。

采样失败时命令仍回 ACCEPTED，结果走事件。BOOT_WAIT 超时只置故障位，不返回 HOST_TIMEOUT。

## 事件（16 字节）

0 u16 event_id，从 1 起，跳过 0；2 type；3 severity：0 INFO，1 NOTICE，2 WARNING，3 CRITICAL；4 u32 timestamp_ms；8 u32 arg0；12 u16 arg1；14 crc16，覆盖 0–13。

FIFO 满时先丢 INFO，再丢 NOTICE。日常收取请逐条 ACK，不要用 EVENTS_CLEAR_ALL。

0x01 KEY_DOWN。0x02 KEY_UP，arg0 为按时长。0x03 KEY_SHORT。0x04 KEY_LONG。0x05 KEY_FORCE_OFF（不自动断电）。0x10 CHARGE，arg0 新 / arg1 前一状态。0x11 SAMPLE，mV / raw。0x12 LOW。0x13 CRITICAL。0x14 SOC_LOW，当前‰ / 阈值‰。0x15 ALARM，mode。0x20 HOST_STARTED，wake_reason。0x21 HOST_READY，耗时 ms / boot_reason。0x22 SHUTDOWN_REQUESTED，reason / deadline_ms。0x24 HOST_RESET，reason / 计数。0x30 COMMAND_COMPLETED，sequence / status。0x40 CONFIG_RECOVERED，generation / 原因。0x41 CW_RESET，reset_reason / boot_id 低 16 位。0x7F OVERFLOW，lost。

收取：CW_INT 为低 → 读 0x84 → 循环 PEEK、处理、ACK → 直到 count=0。每秒仍读一次 STATUS。

## 命令

0x0001 PING。0x0002 GET_EXTENDED_INFO，响应 6 字节：fw_major、minor、patch、hw、build_number u16。0x0004 HOST_READY，须 boot_reason u8；可从 BOOT_WAIT / POWERING_ON / RESETTING / RUNNING / DOWNLOAD_MODE / EN 仍高的 SOFT_SLEEP 进入 RUNNING；EN 已掉的 SOFT_SLEEP 忽略。同一批次先受理 HOST_READY 再受理软睡。0x0005 TIME_SYNC，unix u32。0x0006 CLEAR_DIAGNOSTICS，清前 30 字节。0x0007 TIME_GET，8 字节。0x0008 ALARM_SET。0x0009 ALARM_GET，10 字节。0x000A RTC_RAW，36 字节。

0x0100 BATTERY_SAMPLE，ACCEPTED。0x0101 BATTERY_SET_PROFILE，NOT_SUPPORTED。0x0200 LED_SET。0x0203 LED_OVERRIDE_CLEAR。

0x0300 PREPARE。0x0301 COMMIT，token u32，ACCEPTED。0x0302 CANCEL。0x0303 SHUTDOWN_READY，event_id u16。0x0310 SOFT_SLEEP，非 RUNNING 则 INVALID_STATE。0x0311 REQUEST_OFF。0x0312 REQUEST_RESET。0x0313 EVENTS_CLEAR_ALL。

0x0500 CONFIG_GET，36 字节（3500 / 3300 / 1500 / 8000 / 1000 / 10000 / 4150 / generation / 3050 / 100 / 30 / 15000 / 10000 / 5000 + 四个开关 + 低 SOC 阈值）。0x0501 WAKE_ON_CHARGE。0x0502 LOW_SOC，enable [+ 阈值 1–1000]。0x0503 KEY_EVENTS。0x0504 CHARGE_LED。0x0505 FACTORY_DEFAULT 只恢复 RAM 开关，推 CONFIG_RECOVERED，不改 VCOM 和维护页。上述开关仅存 RAM，复位后：wake=0，key_raw=1，charge_led=1，low_soc=0，阈值=50。

0x0510 VCOM_GET，4 字节：mv + valid + 0。0x0511 VCOM_SET，mv；非法则 INVALID_ARGUMENT，响应回显 2 字节。合法范围 500–2500 且 10 mV 对齐。

0x05F2 MAINT_GET_INFO，36 字节：uid[10] + cycle u16 + replace u16 + accum u16 + repair[16] + readout_level + used + remaining + 0。

## 维护页（0xFE00，记录 32 字节）

0 u32 magic 0x314D5650（"PVM1"）；4 u16 version=1；6 cycle；8 replace；10 accum 0–999；12 repair[16]；28 vcom_mv；30 crc16，覆盖 0–29。整页擦写。FACTORY_DEFAULT 不改本页。

## 复位与唤醒

cw_reset_reason：0 未知，1 POR，2 欠压，3 NRST，4 IWDT，5 软件，6 LOCKUP，7 时钟。本版本会上报：IWDT→4，POR→1，NRST→3，其余 0。

wake_reason：0 未知，1 冷启动关机，2 按键，3 主机请求，4 IWDT 恢复，5 充电接入（须先开启），6 闹钟。

## 生产

烧录接口 SWD（PA07 / PA08）。发布文件名 read_pico_pmu_v1.0.8_build20260905.hex。上电红、白灯各闪一次，共 0.6 s。

读保护只能经 ISP 设定（CW_Programmer + CW-Writer），等级 1。进入 ISP：保持复位，向 SWDIO 馈约 50 kHz 方波，释放复位后延时 5 ms。降级须整片擦除，维护页一并清空。

## 主机启动与关机顺序

1. 读 0x00，校验 CRC 与 "PMU1"。
2. session_id = boot_id。sequence = STATUS.last_command_sequence+1（若为 0 则从 1 起）。
3. 收取事件直到 count=0。
4. 发 HOST_READY，等到 power_state==RUNNING。
5. TIME_SYNC。需要时再 VCOM_GET。
6. 循环：CW_INT 或每秒读 STATUS；电量读 0x85 或 0x20。
7. 关机：落盘 → PREPARE LOGICAL_OFF → COMMIT（可带 delay），或 REQUEST_OFF → 等到 EN=0。
8. boot_id 变化后丢弃缓存，回到步骤 1。此前会话的命令返回 STALE_SESSION。

按住满 8 s 只产生 KEY_FORCE_OFF 事件；按住满 10 s 才强制断电。TIME_SYNC 超出范围返回 INVALID_ARGUMENT。
