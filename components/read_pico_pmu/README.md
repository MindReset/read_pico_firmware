# read_pico_pmu

小纸 Pico 电源管理协议的主机端。板上 PMU 为 CW32L010，负责电池、主机使能轨、电源键、指示灯、RTC 与闹钟；ESP32-S3 通过 I2C 以带长度前缀和 CRC 校验的帧与之通信，而不是寄存器表。

Host side of the Read Pico power management protocol. The board's PMU is a CW32L010 that
owns the battery, the host enable rail, the power key, the indicator LED, an RTC and an
alarm; the ESP32-S3 talks to it over I2C with a length-prefixed, CRC-checked frame
format instead of a register map.

组件名 / Registry name: `mindreset/read_pico_pmu`

## 做什么 / What it does

- `read_pico_pmu_refresh()`：读取身份、状态、诊断、一条排队事件、配置、时间和闹钟，即调试页显示的全部内容。/ Identity, status, diagnostics, one queued event, config, time and alarm — everything the debug page shows.
- `read_pico_pmu_poll()`：只读取状态、快照电池和待处理事件数，开销足够低，可在页面 tick 中调用。/ Status, quick counters and the pending-event count only, cheap enough to call from a page's tick.
- `read_pico_pmu_get()`：最新快照，各数据块附 `*_ok` 标志，页面可区分「未读到」与「读到零」。/ The latest snapshot, including per-block `*_ok` flags so a page can tell "not read" from "read as zero".
- `read_pico_pmu_cmd()` / `read_pico_pmu_action()`：原始命令与动作接口。/ Raw command and action entry points.
- 事件：`read_pico_pmu_drain_events()` 丢弃开机残留事件；`read_pico_pmu_take_key_short()` / `read_pico_pmu_take_key_wakeup()` 供锁屏使用。/ Events: `read_pico_pmu_drain_events()` to discard boot leftovers, `read_pico_pmu_take_key_short()` / `read_pico_pmu_take_key_wakeup()` for the lock screen.
- 电源交接：`read_pico_pmu_report_ready()`、`read_pico_pmu_report_sleep()`（等待 PMU 确认后再拉低 EN）、`read_pico_pmu_power_off()`。/ Power handoff: `read_pico_pmu_report_ready()`, `read_pico_pmu_report_sleep()` (waits for the PMU to acknowledge before it drops EN), `read_pico_pmu_power_off()`.
- 面板 VCOM：`read_pico_pmu_vcom_get()` 只读。每台设备的面板标定值在出厂时写入 PMU；主机开机读取一次用于配置驱动，不再写回。/ Panel VCOM: `read_pico_pmu_vcom_get()` is read-only. Each unit's panel calibration is bound on the PMU at manufacturing time; the host reads it once at boot to configure the display driver and never writes it back.

## 协议 / Protocol

`include/read_pico_pmu_protocol.h` 定义线格式：magic、帧类型、序号、命令码、载荷、CRC。命令按功能分组（状态、配置、事件、时间、LED、电源动作）。未知命令与版本不匹配以状态码返回，而不是超时。

`include/read_pico_pmu_protocol.h` is the wire format: magic, frame kind, sequence,
command code, payload, CRC. Commands are grouped by page (status, config, events,
time, LED, power actions). Unknown commands and version mismatches come back as
status codes rather than timeouts.

## 文档 / Documentation

`docs/` 内为 PMU 主机对接说明：`llms-full_*.md` 为正文，`pmu_protocol_*.h` 与
`pmu_registers_*.json` 为配套的协议头与寄存器表，`read_pico_pmu_datasheet_v1.0.8_*.pdf`
为印刷版。中英各一份。

`docs/` holds the host integration guide for the PMU: `llms-full_*.md` is the text,
`pmu_protocol_*.h` and `pmu_registers_*.json` are the matching protocol header and
register map, and `read_pico_pmu_datasheet_v1.0.8_*.pdf` is the printed edition.
Chinese and English editions are provided.

## 硬件 / Hardware

| 项目 / Item | 规格 / Spec |
| --- | --- |
| 芯片 / Part | 跑小纸 Pico PMU 固件的 CW32L010 / CW32L010 running the Read Pico PMU firmware |
| 总线 / Bus | I2C，400 kHz，地址 `PMU_I2C_ADDR` / I2C, 400 kHz, address `PMU_I2C_ADDR` |
| 中断 / Interrupt | 经 FCA9555 扩展口（P0.2），也是浅睡唤醒源 / Routed through the FCA9555 expander (P0.2), also the light-sleep wake source |

## 许可 / License

Apache-2.0，见 [LICENSE](LICENSE)。

Apache-2.0, see [LICENSE](LICENSE).
