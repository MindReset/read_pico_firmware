# 小纸 Pico 官方演示固件

**语言:** [English](./README.md) | [简体中文](./README.zh-CN.md) | [日本語](./README.ja-JP.md)

[贡献指南](CONTRIBUTING.md) · [支持](SUPPORT.md) · [安全政策](SECURITY.md) · [行为准则](CODE_OF_CONDUCT.md)

[![License](https://img.shields.io/github/license/MindReset/read_pico_firmware?style=for-the-badge&logo=apache&logoColor=white)](LICENSE)
[![Build](https://img.shields.io/github/actions/workflow/status/MindReset/read_pico_firmware/build.yml?branch=main&style=for-the-badge&logo=githubactions&logoColor=white)](https://github.com/MindReset/read_pico_firmware/actions)
![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.1-E7352C?style=for-the-badge&logo=espressif&logoColor=white)
![Target](https://img.shields.io/badge/target-ESP32--S3-E7352C?style=for-the-badge&logo=espressif&logoColor=white)

小纸 Pico 是深圳思维重置科技有限公司旗下小纸 Read 系列的开发板，面向墨水屏开源固件开发者，
搭载 ESP32-S3 和 4.7 寸单色墨水屏。本仓库是随板发布的出厂固件。

固件为显示、触摸、加速度计、电源、按键、TF 卡、字体、睡眠与唤醒提供独立的演示页和诊断页，
用于逐项确认硬件状态，并为自有固件提供刷新策略、功耗处理和交互方式的参考实现。

板级支持、PMU 协议主机端和芯片驱动均为可独立复用的组件。这是一份硬件演示固件，并非完整的阅读器产品。

面向 AI agent 的目录职责、`app_desc_t` 契约、术语表和注释规范见 [AGENTS.md](AGENTS.md)。

## 官方文档与更多设备

- [小纸 Pico 官方文档](https://dot.mindreset.tech/docs/read_0)
- [Dot Open Platform](https://github.com/MindReset/dot_open_platform)：探索更多可以动手玩的 Dot 设备与项目，包括 Quote/0 硬件资源和 Rand/0 本地显示集成，以及固件示例、引脚表和外壳文件。

## 硬件

| 项目 | 规格 |
| --- | --- |
| 主控 | ESP32-S3，16 MB flash，8 MB Octal PSRAM，二者均运行于 120 MHz |
| 屏幕 | 4.7 寸单色墨水屏，1216 × 684，16 级灰阶，16 bit 并口经 LCD 外设驱动 |
| 屏电源 | SY7636A，PGOOD 经 IO 扩展读回 |
| 电源管理 | CW32L010，自定义 I2C 协议：电池、充放电、指示灯、RTC、闹钟、开关机 |
| 触摸 | CST836U，两点触摸、中断与深睡唤醒 |
| 加速度计 | SC7A20H，敲击、朝向、自由落体、FIFO |
| IO 扩展 | FCA9555，屏控制脚与卡检测 |
| 存储 | TF 卡（1 bit SDMMC），字体从卡上加载 |
| 其它 | 蜂鸣器、三个电容按键区 |

## 编译与烧写

需要 ESP-IDF v6.1。`components/read_pico/read_pico_flash_hpm.c` 依赖 v6 才提供的
`esp_flash_chips/spi_flash_override.h`。

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem* flash monitor
```

`sdkconfig.defaults` 中的 120 MHz flash / PSRAM 时序依赖板上实际的 flash 型号。
CI 使用 `sdkconfig.ci` 换回默认时序，仅验证能否编译通过，见
[.github/workflows/build.yml](.github/workflows/build.yml)。

如果在固件开发或烧录时，设备从睡眠状态唤醒后无法被识别，请依次尝试：

1. 更换 USB Type-A（标准 USB）数据线。
2. 对设备重新执行一次睡眠和唤醒操作。
3. 重启开发板后重试。

面板公共电压（VCOM）在出厂时标定并写入 PMU。固件开机读取一次用于配置驱动，
不在本地保存，也不提供修改入口。

## 页面

功能菜单按 [main/app/app_registry.c](main/app/app_registry.c) 中的顺序列出全部页面。

| 页面 | 内容 |
| --- | --- |
| 概览 | 开机 I2C 在线检测、识别码、电池与充电状态、构建时间 |
| 墨水屏刷新 | 整屏 GC16、局部 DU、16 灰阶与快速 8 灰阶梯图，各自附实测耗时 |
| 阅读测试 | 内置正文，DU / GL16 / GC16 翻页，页眉调字号 |
| 触摸 | 两点连续 DU 跟手，抬手整页定稿；深睡与自动唤醒 |
| 加速度计 | 实时三轴与倾角、敲击计数、朝向判定 |
| 加速度计诊断 | 采样参数与自测 |
| 电源与电池 | 电池电压、电量与充电状态；屏电源轨、温度与故障；只读的 SY7636A 配置 |
| 电源管理协议 | 协议状态、事件、配置与命令 |
| 电源按键 | `key_raw_events`、按下与抬起电平、DOWN/UP/SHORT/LONG 事件 |
| 睡眠与唤醒 | 浅睡（按键或拿起唤醒）、深睡、关机 |
| TF 卡与蜂鸣器 | 卡容量与挂载状态、重挂、格式化、蜂鸣 |
| 字体 | 列出并切换卡上 TTF，同页排版示例，循环字重 |
| 扩展口 | Port-0 电平与中断，底栏脉冲触摸复位 |
| 设备功能自检 | 探活与命令 ACK；需要断电的项目只在后台记录结果 |
| 图书 | TF 卡或内置存储的 UTF-8 / GBK TXT 与 EPUB，目录、字号与逐书进度；滑动翻页、长按正文进目录、长按书架条目查看详情、清进度或确认删除；书架支持来源筛选、名称/最近阅读排序及拼音/首字母/英文搜索；单本用弹窗管理，管理页支持批量选择、清进度/删除与重扫；实验晃动翻页默认关。EPUB 支持 NCX 与 nav 目录。 |
| 传书 | 设备热点或已有 WiFi，浏览器上传 TXT/EPUB；热点可扫码连接；已有 WiFi 可在触屏扫描选网并输入密码，也保留网页配网。优先 TF 卡，内置存储单文件 ≤ 1 MB；网页可列出和搜索当前上传目标中的图书，确认替换/删除，取消上传和重试；设备可确认遗忘已保存网络。离页断网。 |

图书阅读三键 KEY1 / KEY2 / KEY3 对应上一页 / 工具条 / 下一页；工具条提供强刷。长按中键约500 ms可打开演示菜单。其他页面仍为 KEY2 整屏 GC16、KEY3 菜单。菜单项抬起提交，滑出可取消。

## 目录

```text
main/
  app_main.c        开机装配，随后交给 app_loop
  app/              app 接口（app.h）、注册表、事件循环
  apps/             每个演示页一个文件，只导出 app_desc_t
  ui/               ui_kit 绘制原语与布局常量、ui_menu 两层菜单
  font/             stb_truetype 字形缓存
  factory/          设备功能自检与出厂 VCOM 标定
components/
  read_pico/        板级 BSP：I2C、EPD 板定义与扫描时序、TF 卡、蜂鸣器、flash HPM
  read_pico_pmu/    CW32L010 协议主机端
  epdiy/            墨水屏渲染，裁剪至 LCD 外设路径
  continuous_du/    连续 DU：跨多轮累积相位，用于跟手
  cst836u/ sc7a20h/ fca9555/ sy7636a/    芯片驱动
  e0470_epaper_waveform/                 面板波形表与裁剪函数
  pwm_audio/        LEDC PWM 音频，蜂鸣器底层之一
assets/             图片素材（main/assets/*.bin 的来源）
tools/              字体与图片转换脚本
```

新增演示页：在 `main/apps/` 新建文件，实现 `app_desc_t` 中需要的回调，
再加入 `main/app/app_registry.c` 的菜单表。主循环不需要改动。

## 引脚

| 功能 | GPIO |
| --- | --- |
| I2C SCL / SDA | 40 / 39（400 kHz） |
| EPD 数据 D0–D15 | 4–18, 45 |
| EPD XLE / XSTL / XCL / SPV / CKV | 3 / 46 / 21 / 47 / 48 |
| FCA9555 INT# | 41（同时作为浅睡唤醒源） |
| CST836U INT# | 43 |
| SC7A20H INT1 | 1 |
| TF 卡 CLK / CMD / D0 | 38 / 42 / 44 |
| 蜂鸣器 | 2 |

屏电源开关、XOE、MODE、VCOM_EN、触摸复位和卡检测位于 FCA9555 的 Port-0，
见 [main/apps/app_ioe.c](main/apps/app_ioe.c) 中的引脚表。

## 传书与使用限制

AP 与已有 WiFi 均提供传书网页二维码；热点页可切换连接 WiFi 与打开网页二维码。设备显示的构建时间统一标注 UTC。

传书服务仅在传书页运行，离页停止。它使用局域网 HTTP，没有独立登录或 TLS；同网设备可管理当前上传存储中的图书，请使用可信网络。保存的 WiFi 凭据位于设备 NVS，公开接口和日志不返回密码。本版未启用 NVS/flash 加密，不以此提供物理访问防护。

停止传书后返回进入前的页面或菜单位置。检测到已挂载TF卡失效时，设备停止相关阅读/传书并回退字体；插回后需在TF页显式重新挂载。写入时拔卡可能损坏文件系统。EPUB仅支持纯文本；外部换书或换卡产生同路径、同大小文件时，旧阅读进度可能仍被匹配。保存失败的待重试状态不能保证在断电后保留。

功能变化见 [版本变更](docs/CHANGELOG.md)。离线拼音字表来自 pypinyin（MIT），见 [组件许可与再生成说明](components/read_pico_search/README.md)。

## 致谢与许可

- 固件本体：Apache-2.0，见 [LICENSE](LICENSE)。
- [epdiy](https://github.com/vroland/epdiy)：墨水屏时序与渲染。本仓库为按本板
  LCD 路径裁剪的 fork，LGPL-3.0-or-later，改动清单见
  [components/epdiy/LICENSE](components/epdiy/LICENSE)。
- [stb_truetype](https://github.com/nothings/stb)：字形光栅化，公共领域。
- [pwm_audio](https://github.com/espressif/esp-iot-solution/tree/master/components/audio/pwm_audio)：
  Espressif LEDC PWM 音频。本仓库为裁剪副本，Apache-2.0，见
  [components/pwm_audio/LICENSE](components/pwm_audio/LICENSE)。
- 面板波形表随本板附带，按现状提供，Apache-2.0，见
  [components/e0470_epaper_waveform/LICENSE](components/e0470_epaper_waveform/LICENSE)。
- 内置字体 `main/assets/builtin.ttf` 由 `tools/gen_builtin_font.py` 从
  [ChillDuanSans](https://github.com/Warren2060/ChillDuanSans)（寒蝉端黑体，
  Warren2060，SIL OFL-1.1）可变字体子集化生成。完整字体文件不入库；子集仍适用 OFL-1.1。

感谢各位开发者的耐心与支持。

深圳思维重置科技有限公司
