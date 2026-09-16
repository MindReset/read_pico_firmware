# 小纸 Pico 官方演示固件
# Read Pico Demo Firmware

[![License](https://img.shields.io/github/license/MindReset/read_pico_firmware?style=for-the-badge&logo=apache&logoColor=white)](LICENSE)
[![Build](https://img.shields.io/github/actions/workflow/status/MindReset/read_pico_firmware/build.yml?branch=main&style=for-the-badge&logo=githubactions&logoColor=white)](https://github.com/MindReset/read_pico_firmware/actions)
![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.1-E7352C?style=for-the-badge&logo=espressif&logoColor=white)
![Target](https://img.shields.io/badge/target-ESP32--S3-E7352C?style=for-the-badge&logo=espressif&logoColor=white)

小纸 Pico 是一块 ESP32-S3 加 4.7 寸单色墨水屏的开发板，
该设备面向为墨水屏设备编写开源固件的开发者。本仓库是它的出厂固件，随板子一起发布。

Read Pico is an ESP32-S3 development board with a 4.7" monochrome e-paper panel,
made by Shenzhen MindReset Technology Co., Ltd. for developers who want to write
open-source firmware for e-paper devices. This repository is the factory firmware
that ships with the board.

固件以功能菜单的形式组织：显示、触摸、加速度计、电源、按键、TF 卡、字体、睡眠与唤醒，
以及面向调试的寄存器级页面，各自独立成页，可逐项确认硬件状态。

The firmware is organised as a function menu: display, touch, accelerometer, power,
keys, TF card, fonts, sleep and wake, plus register-level diagnostic pages. Each is a
separate page, so the hardware can be checked item by item.

需要说明的是，这份固件的目标是验证硬件与提供参考实现，不是完整的阅读器产品。
各页面的刷新策略、功耗处理和交互方式，均可作为编写自有固件的起点。板级支持、
PMU 协议主机端和各芯片驱动都拆成了独立组件，可以单独引用。

This firmware is meant to verify the hardware and serve as a reference
implementation; it is not a finished reader. Its refresh strategy, power handling
and interaction model are intended as a starting point for your own firmware. The
board support, the PMU protocol host and the chip drivers are separate components
and can be used on their own.

面向 AI agent 的目录职责、`app_desc_t` 契约、术语表和注释规范见 [AGENTS.md](AGENTS.md)。

Agent-facing layout, `app_desc_t` contract, glossary and comment style are in
[AGENTS.md](AGENTS.md).

## 硬件 / Hardware

| 项目 / Item | 规格 / Spec |
| --- | --- |
| 主控 / MCU | ESP32-S3，16 MB flash，8 MB Octal PSRAM，二者均运行于 120 MHz / both at 120 MHz |
| 屏幕 / Display | 4.7" 单色墨水屏，1216 × 684，16 级灰阶，16 bit 并口经 LCD 外设驱动 / 16-bit parallel via the LCD peripheral |
| 屏电源 / EPD power | SY7636A，PGOOD 经 IO 扩展读回 / PGOOD read back through the IO expander |
| 电源管理 / PMU | CW32L010，自定义 I2C 协议 / custom I2C protocol：电池、充放电、指示灯、RTC、闹钟、开关机 |
| 触摸 / Touch | CST836U，两点，中断与深睡唤醒 / 2-point, IRQ and deep-sleep wake |
| 加速度计 / IMU | SC7A20H，敲击、朝向、自由落体、FIFO / tap, orientation, free-fall, FIFO |
| IO 扩展 / IO expander | FCA9555，屏控制脚与卡检测 / EPD control pins and card detect |
| 存储 / Storage | TF 卡（1 bit SDMMC），字体从卡上加载 / fonts are loaded from the card |
| 其它 / Misc | 蜂鸣器，三个电容按键区 / buzzer, three capacitive key zones |

## 编译与烧写 / Build & Flash

需要 ESP-IDF v6.1。`components/read_pico/read_pico_flash_hpm.c` 依赖 v6 才提供的
`esp_flash_chips/spi_flash_override.h`。

Requires ESP-IDF v6.1. `components/read_pico/read_pico_flash_hpm.c` depends on
`esp_flash_chips/spi_flash_override.h`, which only exists in v6.

```
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem* flash monitor
```

需要说明的是，`sdkconfig.defaults` 中的 120 MHz flash / PSRAM 时序依赖板上实际的
flash 型号。CI 使用 `sdkconfig.ci` 换回默认时序，只验证能否编译通过
（见 `.github/workflows/build.yml`）。

The 120 MHz flash / PSRAM timing in `sdkconfig.defaults` depends on the exact
flash part on the board. CI builds with `sdkconfig.ci` (default timing) only to
check that the tree compiles (see `.github/workflows/build.yml`).

如果在固件开发或烧录时，设备在从睡眠状态恢复唤醒后无法被识别，请按以下步骤排查：  
1. 更换 USB Type-A（标准 USB）数据线后重试。  
2. 对设备执行一次重新睡眠/唤醒操作后重试。  
3. 重启该开发板并重新进行尝试。

If during firmware flashing, the device cannot be recognized after waking from sleep, please try the following:

1. Replace the USB Type-A (standard USB) cable and try again.
2. Attempt a new sleep/wake cycle for the device.
3. Reboot the development board and retry.

面板的公共电压（VCOM）在出厂时与该块屏一起写入 PMU。固件开机读取一次用于配置驱动，
不在本地保存，也不提供修改入口。

The panel VCOM is paired with the panel at the factory and stored in the PMU. The
firmware reads it once at boot; it is neither stored locally nor user-editable.

## 页面 / Pages

功能菜单分页列出全部页面，顺序与 `main/app/app_registry.c` 一致。

One paginated function menu, in the same order as `main/app/app_registry.c`.

| 页面 / Page | 内容 / Content |
| --- | --- |
| 概览 / Overview | 开机 I2C 在线检测、识别码、电池与充电、构建时间 / boot I2C census, IDs, battery, build time |
| 墨水屏刷新 / EPD Refresh | 整屏 GC16、局部 DU、16 灰阶与快速 8 灰阶梯图，各自附实测耗时 / full GC16, partial DU, 16-gray and fast 8-gray ladders, each with measured time |
| 阅读测试 / Reading | 内置正文，DU / GL16 / GC16 翻页，页眉调字号 / built-in text, page-turn modes, type size |
| 触摸 / Touch | 两点连续 DU 跟手，抬手整页定稿；深睡与自动唤醒 / 2-point tracking with continuous DU, settle on lift; deep sleep and auto wake |
| 加速度计 / Accel | 实时三轴与倾角、敲击计数、朝向判定 / live axes and tilt, tap count, orientation |
| 加速度计诊断 / IMU Lab | 采样参数与自测 / sample config and self-test |
| 电源与电池 / Power | 电池电压、电量与充电状态；屏电源轨、温度与故障；只读的 SY7636A 配置 / battery, EPD rails, temperature, faults, read-only SY7636A config |
| 电源管理协议 / PMU | 协议状态、事件、配置与命令 / protocol status, events, config, commands |
| 电源按键 / Power Key | `key_raw_events`、按下与抬起电平、DOWN/UP/SHORT/LONG 事件 / raw events, level, key event log |
| 睡眠与唤醒 / Sleep | 浅睡（按键或拿起唤醒）、深睡、关机 / light sleep (key or pickup), deep sleep, power off |
| TF 卡与蜂鸣器 / Storage | 卡容量与挂载状态、重挂、格式化、蜂鸣 / capacity, mount, remount, format, buzzer |
| 字体 / Font | 列出卡上 TTF 并切换，同页排版示例，循环字重 / list TTFs, typesetting, cycle weight |
| 扩展口 / IOE | Port-0 电平与中断，底栏脉冲触摸复位 / Port-0 levels and IRQ, pulse TP reset |
| 设备功能自检 / Device | 探活与命令 ACK；需要断电的项目只在后台记录结果 / probe and command ACK; power-cut items are recorded in the backend only |

三个实体按键区：KEY1 / KEY3 在分页页面中翻页，KEY2 在任何时候执行整屏 GC16 重刷。

Three key zones: KEY1 / KEY3 page through multi-page screens; KEY2 always performs
a full GC16 redraw.

## 目录 / Layout

```
main/
  app_main.c        开机装配，随后交给 app_loop / boot wiring, then app_loop
  app/              app 接口（app.h）、注册表、事件循环 / app.h, registry, event loop
  apps/             每个 demo 一个文件，只导出 app_desc_t / one demo per file
  ui/               ui_kit 绘制原语与布局常量、ui_menu 两层菜单 / drawing primitives, menus
  font/             stb_truetype 字形缓存 / glyph cache
  factory/          设备功能自检与出厂 VCOM 标定 / device self-test and factory VCOM
components/
  read_pico/        板级 BSP：I2C、EPD 板定义与扫描时序、TF 卡、蜂鸣器、flash HPM / board BSP
  read_pico_pmu/    CW32L010 协议主机端 / PMU protocol, host side
  epdiy/            墨水屏渲染，裁剪至 LCD 外设路径 / e-paper renderer, LCD path only
  continuous_du/    连续 DU：跨多轮累积相位，用于跟手 / continuous DU for finger tracking
  cst836u/ sc7a20h/ fca9555/ sy7636a/    芯片驱动 / chip drivers
  e0470_epaper_waveform/                 面板波形表与裁剪函数 / panel waveforms and trimmer
  pwm_audio/        LEDC PWM 音频，蜂鸣器底层之一 / LEDC PWM audio, one buzzer backend
assets/  图片素材（main/assets/*.bin 的来源）/ image sources
tools/   字体与图片转换脚本 / font and image conversion scripts
```

新增一个 demo 页：在 `main/apps/` 新建文件，实现 `app_desc_t` 中需要的回调，
再加入 `main/app/app_registry.c` 的菜单表。主循环不需要改动。

To add a demo page: create one file in `main/apps/`, implement the `app_desc_t`
callbacks you need, and add it to the menu table in `main/app/app_registry.c`.
The main loop stays untouched.

## 引脚 / Pinout

| 功能 / Function | GPIO |
| --- | --- |
| I2C SCL / SDA | 40 / 39（400 kHz） |
| EPD 数据 / data D0–D15 | 4–18, 45 |
| EPD XLE / XSTL / XCL / SPV / CKV | 3 / 46 / 21 / 47 / 48 |
| FCA9555 INT# | 41（同时作为浅睡唤醒源 / also light-sleep wake source） |
| CST836U INT# | 43 |
| SC7A20H INT1 | 1 |
| TF 卡 / card CLK / CMD / D0 | 38 / 42 / 44 |
| 蜂鸣器 / Buzzer | 2 |

屏电源开关、XOE、MODE、VCOM_EN、触摸复位和卡检测位于 FCA9555 的 Port-0，
见 `main/apps/app_ioe.c` 中的引脚表。

EPD power enable, XOE, MODE, VCOM_EN, touch reset and card detect sit on FCA9555
Port-0; see the pin table in `main/apps/app_ioe.c`.

## 致谢与许可 / Acknowledgments & License

- 固件本体：Apache-2.0，见 [LICENSE](LICENSE)。/ Firmware: Apache-2.0, see [LICENSE](LICENSE).
- [epdiy](https://github.com/vroland/epdiy)：墨水屏时序与渲染。本仓库为按本板
  LCD 路径裁剪的 fork，LGPL-3.0-or-later，改动清单见 `components/epdiy/LICENSE`。
  / Trimmed fork (LCD path only), LGPL-3.0-or-later; local changes are listed in
  `components/epdiy/LICENSE`.
- [stb_truetype](https://github.com/nothings/stb)：字形光栅化，public domain。/ Glyph rasterizer, public domain.
- [pwm_audio](https://github.com/espressif/esp-iot-solution/tree/master/components/audio/pwm_audio)：
  Espressif LEDC PWM 音频。本仓库为裁剪副本，Apache-2.0，见
  `components/pwm_audio/LICENSE`。
  / Espressif LEDC PWM audio; trimmed copy, Apache-2.0, see
  `components/pwm_audio/LICENSE`.
- 面板波形表随本板附带，按现状提供，Apache-2.0，见
  `components/e0470_epaper_waveform/LICENSE`。
  / Panel waveform tables ship with the board as-is, Apache-2.0, see
  `components/e0470_epaper_waveform/LICENSE`.
- 内置字体 `main/assets/builtin.ttf` 由 `tools/gen_builtin_font.py` 从
  [ChillDuanSans](https://github.com/Warren2060/ChillDuanSans)（寒蝉端黑体，
  Warren2060，SIL OFL-1.1）可变字体子集化生成。完整字体文件不入库；子集仍适用
  OFL-1.1。/ The built-in font is an OFL-1.1 subset of ChillDuanSans generated
  by `tools/gen_builtin_font.py`; the full font is not committed.

感谢各位开发者的耐心与支持。

Thank you for your patience and support.

深圳思维重置科技有限公司 / Shenzhen MindReset Technology Co., Ltd.
