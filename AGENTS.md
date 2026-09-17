# AGENTS.md

面向 AI agent 的仓库入口。人类也可读。产品概述、硬件表、引脚、编译命令见 [README.md](README.md)。

Agent entry for this tree. Humans can read it too. Product overview, hardware, pinout and flash commands live in [README.md](README.md).

## 构建 / Build

需要 ESP-IDF v6.1。`components/read_pico/read_pico_flash_hpm.c` 依赖 v6 才有的 `esp_flash_chips/spi_flash_override.h`。

Requires ESP-IDF v6.1. `components/read_pico/read_pico_flash_hpm.c` needs `esp_flash_chips/spi_flash_override.h` (v6 only).

```
idf.py set-target esp32s3
idf.py build
```

- `sdkconfig.defaults`：真机 120 MHz flash / PSRAM 时序，依赖板上 Zbit 型号。/ Board timing for 120 MHz flash / PSRAM; depends on the Zbit part.
- `sdkconfig.ci`：默认时序，只验证能否编过。/ Default timing; compile-only CI check.

不要在没有对应 flash 的机器上把 CI 配置当产品默认。/ Do not ship the CI timing as the product default.

## 目录职责 / Layout

| 路径 / Path | 职责 / Role |
| --- | --- |
| `main/app_main.c` | 开机装配：板级 init、读设置、定 VCOM、开机图、字体，然后交给 `app_loop`。/ Boot wiring, then `app_loop`. |
| `main/app/app.h` | `app_desc_t` / `app_ctx_t` / `app_redraw_t` 契约。/ Page contract. |
| `main/app/app_loop.c` | 与页面无关的事件循环：触摸去抖、三键与菜单把手、切页、刷屏、锁屏、SD 字体延迟加载。/ Shared event loop. |
| `main/app/app_registry.c` | 菜单顺序。加页只改这张表。/ Menu table. |
| `main/app/app_config.h` | 均衡 / ALL_DU 刷新档位。/ Refresh profile. |
| `main/ui/` | `ui_kit` 排版常量与绘制原语；`ui_menu` 两层菜单。页面互不引用。/ Shared drawing; pages do not include each other. |
| `main/font/` | stb_truetype 字形缓存。`fallback.h` 是生成位图，勿手改。/ Glyph cache. Do not edit `fallback.h` by hand. |
| `main/factory/` | 设备功能自检与出厂 VCOM 标定。/ Device self-test and factory VCOM. |
| `main/display.*` `sleep.*` `settings.*` | 刷屏封装、睡眠/锁屏、NVS 设置。/ Present helpers, sleep/lock, NVS. |
| `components/read_pico/` | 板级 BSP：I2C、EPD 板定义与扫描时序、TF 卡、蜂鸣器、flash HPM。/ Board BSP. |
| `components/read_pico_pmu/` | CW32L010 协议主机端。线格式在 `read_pico_pmu_protocol.h`。/ PMU host. |
| `components/epdiy/` | 上游裁剪 fork（LCD 路径）。改动清单在其 LICENSE。不要为注释去改它。/ Trimmed upstream fork. Leave it alone for comment work. |
| `components/continuous_du/` | 连续 DU，触摸跟手。/ Continuous DU for finger tracking. |
| `components/cst836u/` `sc7a20h/` `fca9555/` `sy7636a/` | 芯片驱动。/ Chip drivers. |
| `components/e0470_epaper_waveform/` | 面板波形与裁剪。`waveforms/*.h` 是数据表。/ Panel waveforms. |
| `components/pwm_audio/` | PWM 音频（蜂鸣器底层之一）。/ PWM audio helper. |

### demo 页 / Demo pages

每个文件只导出 `app_desc_t`。顺序与 `app_registry.c` 一致。

One file, one `app_desc_t`. Order matches `app_registry.c`.

| 文件 / File | 演示 / Demo | 依赖 / Depends on |
| --- | --- | --- |
| `app_home.c` | 概览：I2C 普查、识别码、电池、构建时间。/ Overview. | `read_pico`, `read_pico_pmu` |
| `app_refresh.c` | GC16 / DU / 灰阶梯子与耗时。/ Refresh matrix. | `epdiy`, `e0470_epaper_waveform` |
| `app_reading.c` | 内置正文翻页。/ Reading demo. | `ttf_font` |
| `app_touch.c` | 两点跟手连续 DU、深睡唤醒。/ Touch + continuous DU. | `cst836u`, `continuous_du` |
| `app_axis.c` | 读数页 Live/Tap/Orient 与诊断页 Setup/Event/FIFO/Test。/ IMU pages. | `sc7a20h` |
| `app_power.c` | 电池与 SY7636A 只读。/ Battery + rails. | `read_pico_pmu`, `sy7636a` |
| `app_pmu.c` | CW32 协议五视图。/ Protocol views. | `read_pico_pmu` |
| `app_key.c` | 电源键原始事件。/ Power-key events. | `read_pico_pmu` |
| `app_sleep.c` | 浅睡 / 深睡 / 关机。/ Sleep modes. | `sleep`, `read_pico_pmu`, `sc7a20h` |
| `app_sd.c` | TF 卡与蜂鸣器。/ Card + buzzer. | `read_pico_sd`, `read_pico_buzzer` |
| `app_font_pick.c` | 卡上 TTF 列表与字重。/ Font picker. | `ttf_font` |
| `app_ioe.c` | FCA9555 Port-0。/ Expander pins. | `fca9555` |
| `app_selftest.c` | 设备功能自检入口。/ Device self-test UI. | `pmu_selftest` |

加页：在 `main/apps/` 新建文件，实现需要的回调，把它加入 `app_registry.c` 的 `s_apps[]`。不要改 `app_loop.c`。

To add a page: new file in `main/apps/`, implement the callbacks you need, append it to `s_apps[]` in `app_registry.c`. Leave `app_loop.c` alone.

## `app_desc_t` 契约 / Contract

定义在 [`main/app/app.h`](main/app/app.h)。主循环按返回值刷屏，见 [`main/app/app_loop.c`](main/app/app_loop.c) 的 `app_present()`。

Defined in [`main/app/app.h`](main/app/app.h). The loop presents via `app_present()` in [`main/app/app_loop.c`](main/app/app_loop.c).

| 返回值 / Return | 主循环做什么 / Loop does |
| --- | --- |
| `APP_REDRAW_NONE` | 不刷。/ No update. |
| `APP_REDRAW_AREA` | 只推 `area_hint()`。回调必须先画好 fb。模式是 `APP_DYNAMIC_REFRESH_MODE`（DU）。/ Push `area_hint()` only. Caller paints fb first. |
| `APP_REDRAW_PAGE` | 调 `render()`，整页 `APP_PAGE_REFRESH_MODE`（均衡档是 GL16）。/ `render()` then page mode. |
| `APP_REDRAW_FULL` | 调 `render()`；`APP_PAGE_FORCE_FULL` 时整屏 GC16，否则仍走页模式。/ `render()`; GC16 when force-full is on. |
| `APP_REDRAW_DONE` | 页面自己已经刷完。/ Page already presented. |

回调纯度：

- `render()`：纯绘制。禁止 I2C 写、蜂鸣、睡眠、改设置。KEY2 整屏强刷会复用它。/ Paint only. No I2C writes, buzzer, sleep, or settings. KEY2 reuses it.
- `on_enter()`：上电、唤醒传感器、拉一次数据。/ Power-up and first sample.
- `on_exit()`：掉电、停传感器。/ Power-down.
- `present()`：自定义推屏。返回 true 表示已经刷过，主循环不再推。/ Custom present; true means done.
- `on_touch()` / `on_key()` / `on_tick()`：可有副作用，用返回值要刷屏。/ Side effects OK; return the redraw.
- `on_key()` 只收 `UI_KEY_1`。KEY2 整屏强刷、KEY3 菜单把手由主循环处理。/ Only KEY1. KEY2/KEY3 are global.

标志：

- `holds_pmu`：长时间独占 PMU（自检）。主循环的锁屏按键轮询让路。/ Page owns the PMU; lock-key poll yields.
- `enter_full`：进页走 `APP_REDRAW_FULL`，避免差分刷留边。/ Enter with a full refresh.

`app_ctx_t.request_app` 非空时，主循环在本轮末尾切页。/ Non-null `request_app` switches at end of tick.

## 术语表 / Glossary

| 术语 / Term | 中文 | English |
| --- | --- | --- |
| DU | 快速差分刷新，只驱动变化像素，灰阶少、残影多。 | Fast differential update. Drives changed pixels only. |
| GC16 | 16 级灰度全像素刷新。区域内白像素先压黑再擦白，刷新处一条黑带。 | 16-gray full-pixel update. Whites go black then white; a black flash. |
| GL16 | 16 级灰度，白底保持，不先压黑。 | 16-gray update that keeps an already-white background. |
| 跟随 DU / FOLLOW DU | 开机生成的 8 帧短波形，一次刷新连 diff 带扫描，用来跟手数字或读数。 | Boot-built 8-frame short waveform: one refresh does diff + scan. Used for live digits. |
| 连续 DU / continuous DU | 每轮只扫一个相位，软件记每像素剩余相位。帧率与黑度解耦。 | One DU phase per scan; software tracks leftover phases per pixel. |
| 跟手 | 手指或传感器在动时用 DU/连续 DU 追画面。 | Live tracking with DU / continuous DU while the finger or sensor moves. |
| 定稿 | 动作停下后用 GC16/GL16 整页（或指定区）清残影。 | Settle after motion with GC16/GL16 to clear ghosting. |
| 把手 | 底栏菜单按钮或 KEY3。打开/关闭全屏一级菜单，不交给当前页。 | Menu handle (bottom-right or KEY3). Toggles the root menu; not dispatched to the page. |
| 冻结 / Frozen | 文件头里的产品决策。agent 不得改行为去“优化”它。 | Product decisions in the file banner. Do not “improve” them. |
| 均衡配置 | `APP_REFRESH_BALANCED`：整页 GL16，周期 GC16 压灰底，动态区 DU。 | Default refresh profile: GL16 pages, periodic GC16, DU for motion. |
| ALL_DU | 实验档：整机都走 DU，只看速度。 | Experimental profile: everything is DU. |
| 锁屏 | 电源键短按后的锁页。浅睡按键或拿起回原页。 | Lock face after a short power-key press. |
| 软睡 | `SOFT_SLEEP`：PMU 拉低 EN，再短按开机，不是硬复位。 | Soft sleep: PMU drops EN; a short press powers on without a hard reset. |
| VCOM | 面板公共电压（绝对值 mV）。出厂写入 PMU，主机只读。 | Panel common voltage (absolute mV). Factory-written in the PMU; host is read-only. |

注释里不要再展开这些词。/ Do not re-explain these terms in comments.

## 硬约束 / Hard rules

- 对外产品文案：中文用“小纸 Pico”，英文和日文用“Read Pico”；Read/0 仅可作为内部代号保留。/ Public product copy: use “小纸 Pico” in Chinese and “Read Pico” in English and Japanese; reserve Read/0 for the internal codename.

`冻结 / Frozen:` 段落是产品决策，不是建议。改行为前必须先改这段，并说明为什么决策变了。

A `冻结 / Frozen:` block is a decision, not a hint. Change the behavior only after rewriting that block and saying why the decision changed.

- 面板 VCOM 存在 PMU 里。主机开机读一次，不写 ESP NVS，产品页不提供修改入口。标定页（`vcom_setup.c`）是未标定时的出厂拦住路径。/ VCOM lives in the PMU. Host reads once. No NVS copy. No product UI to edit it. `vcom_setup.c` is the factory gate when unset.
- 不要写 SY7636A 的 VCOM / VLDO / 放电 / 延时 / VCOMCTL。电源页只读。/ Do not write SY7636A VCOM / VLDO / discharge / delay / VCOMCTL. Power page is read-only.
- `sdkconfig.defaults` 的 120 MHz 时序依赖真机 Zbit flash。覆盖 HPM 表见 `read_pico_flash_hpm.c`。/ 120 MHz timing needs the onboard Zbit part. HPM override is `read_pico_flash_hpm.c`.
- 不要改 `components/epdiy/**` 来“整理注释”。/ Do not touch `components/epdiy/**` for comment cleanup.
- 不要手改 `main/font/fallback.h`、`main/font/stb_truetype.h`、`components/e0470_epaper_waveform/waveforms/*.h`。/ Do not hand-edit generated or vendored tables.
- 不改 UI 字面量或日志字符串，除非任务明确要求。/ Do not change UI strings or log text unless asked.

## 注释规范 / Comment style

全仓中英双语。只改注释，不改逻辑。

Every comment is bilingual. Comments only; no logic changes.

文件头：

```
/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：这个文件负责什么、边界在哪。
 *
 * English: what this file owns and where it stops.
 *
 * 冻结：不得做的事。
 * Frozen: things this file must not do.
 */
```

- 头文件公开 API 与 struct 成员用 `///`。/ Public API and struct fields: `///`.
- `.c` 内部用 `//`。不要 `/** */`。/ `.c` internals: `//`. No `/** */`.
- 单行：`// 中文。/ English.`
- 超过一行：中文一行，英文紧跟一行。/ Multi-line: Chinese line, then English line.
- `enum` 成员短尾注：`///< 中文 / English`
- 大 `.c` 用 `/* ---- 区块名 / Section ---- */` 分区。/ Large `.c` files use section banners.
- 术语见表，不在注释里重复解释。/ Use the glossary; do not redefine terms.
