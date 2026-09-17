# Read Pico Demo Firmware

**Languages:** [English](./README.md) | [简体中文](./README.zh-CN.md) | [日本語](./README.ja-JP.md)

[Contributing](CONTRIBUTING.md) · [Support](SUPPORT.md) · [Security](SECURITY.md) · [Code of Conduct](CODE_OF_CONDUCT.md)

[![License](https://img.shields.io/github/license/MindReset/read_pico_firmware?style=for-the-badge&logo=apache&logoColor=white)](LICENSE)
[![Build](https://img.shields.io/github/actions/workflow/status/MindReset/read_pico_firmware/build.yml?branch=main&style=for-the-badge&logo=githubactions&logoColor=white)](https://github.com/MindReset/read_pico_firmware/actions)
![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.1-E7352C?style=for-the-badge&logo=espressif&logoColor=white)
![Target](https://img.shields.io/badge/target-ESP32--S3-E7352C?style=for-the-badge&logo=espressif&logoColor=white)

Read Pico is an ESP32-S3 development board in the Read series with a 4.7" monochrome e-paper panel,
made by Shenzhen MindReset Technology Co., Ltd. for developers building open-source
e-paper firmware. This repository contains the factory firmware shipped with the board.

The firmware provides separate demo and diagnostic pages for the display, touch,
accelerometer, power, keys, TF card, fonts, sleep and wake. Use them to check the
hardware and as reference implementations for your own firmware.

The board support, PMU protocol host and chip drivers are independent components
that can be reused. This is a hardware demo, not a finished reader product.

Agent-facing layout, `app_desc_t` contract, glossary and comment style are in
[AGENTS.md](AGENTS.md).

## Documentation & More Devices

- [Official Read Pico documentation](https://dot.mindreset.tech/docs/read_0)
- [Dot Open Platform](https://github.com/MindReset/dot_open_platform): explore other Dot devices and projects, including Quote/0 hardware resources and Rand/0 local display integration, with firmware examples, pin maps, and enclosure files.

## Hardware

| Item | Specification |
| --- | --- |
| MCU | ESP32-S3, 16 MB flash and 8 MB Octal PSRAM, both at 120 MHz |
| Display | 4.7" monochrome e-paper, 1216 × 684, 16 gray levels, 16-bit parallel interface driven by the LCD peripheral |
| EPD power | SY7636A; PGOOD read through the IO expander |
| PMU | CW32L010 with a custom I2C protocol for battery, charging/discharging, indicator LED, RTC, alarms and power control |
| Touch | CST836U, two touch points, interrupt and deep-sleep wake |
| Accelerometer | SC7A20H, tap, orientation, free-fall and FIFO |
| IO expander | FCA9555, EPD control pins and card detection |
| Storage | TF card over 1-bit SDMMC; fonts loaded from the card |
| Other | Buzzer and three capacitive key zones |

## Build & Flash

Requires ESP-IDF v6.1. `components/read_pico/read_pico_flash_hpm.c` depends on
`esp_flash_chips/spi_flash_override.h`, which is available only in v6.

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem* flash monitor
```

The 120 MHz flash / PSRAM timing in `sdkconfig.defaults` depends on the flash part
installed on the board. CI uses `sdkconfig.ci` with default timing only to check
compilation; see [.github/workflows/build.yml](.github/workflows/build.yml).

If the device is not recognized after waking from sleep during development or
flashing:

1. Try a USB Type-A data cable.
2. Put the device to sleep and wake it again.
3. Reboot the board and retry.

The panel VCOM is calibrated at the factory and stored in the PMU. The firmware
reads it once at boot to configure the driver; it is neither stored locally nor
user-editable.

## Pages

The function menu lists pages in the order defined in
[main/app/app_registry.c](main/app/app_registry.c).

| Page | Content |
| --- | --- |
| Overview | Boot I2C scan, IDs, battery and charging status, build time |
| EPD Refresh | Full-screen GC16, partial DU, 16-gray and fast 8-gray ladders, each with measured refresh time |
| Reading | Built-in text, DU / GL16 / GC16 page turns, font size adjustment in the header |
| Touch | Two-point tracking with continuous DU, full-page settle on lift, deep sleep and automatic wake |
| Accel | Live three-axis readings and tilt, tap count, orientation |
| IMU Lab | Sampling parameters and self-test |
| Power | Battery voltage, level and charging status; EPD rails, temperature, faults and read-only SY7636A configuration |
| PMU | Protocol status, events, configuration and commands |
| Power Key | `key_raw_events`, press/release levels and DOWN/UP/SHORT/LONG events |
| Sleep | Light sleep with key or pickup wake, deep sleep and power off |
| Storage | TF card capacity and mount status, remount, format and buzzer |
| Font | Select TTF files from the card, preview typesetting and cycle font weight |
| IOE | Port-0 levels and interrupts, touch-reset pulse from the bottom bar |
| Device | Device probes and command ACKs; power-cut items are recorded in the backend only |

Three key zones: KEY1 / KEY3 page through multi-page screens; KEY2 always performs
a full GC16 redraw.

## Repository Layout

```text
main/
  app_main.c        Boot wiring, then app_loop
  app/              App interface (app.h), registry and event loop
  apps/             One demo per file, exporting only app_desc_t
  ui/               ui_kit drawing primitives and layout constants; ui_menu two-level menu
  font/             stb_truetype glyph cache
  factory/          Device self-test and factory VCOM calibration
components/
  read_pico/        Board BSP: I2C, EPD definition/timing, TF card, buzzer, flash HPM
  read_pico_pmu/    CW32L010 protocol host
  epdiy/            E-paper renderer, trimmed to the LCD peripheral path
  continuous_du/    Continuous DU with phases accumulated across scans for finger tracking
  cst836u/ sc7a20h/ fca9555/ sy7636a/    Chip drivers
  e0470_epaper_waveform/                 Panel waveform tables and trimming functions
  pwm_audio/        LEDC PWM audio, one buzzer backend
assets/             Image sources for main/assets/*.bin
tools/              Font and image conversion scripts
```

To add a demo page, create a file in `main/apps/`, implement the `app_desc_t`
callbacks you need and add it to the menu table in `main/app/app_registry.c`.
The main loop stays untouched.

## Pinout

| Function | GPIO |
| --- | --- |
| I2C SCL / SDA | 40 / 39 (400 kHz) |
| EPD data D0–D15 | 4–18, 45 |
| EPD XLE / XSTL / XCL / SPV / CKV | 3 / 46 / 21 / 47 / 48 |
| FCA9555 INT# | 41 (also a light-sleep wake source) |
| CST836U INT# | 43 |
| SC7A20H INT1 | 1 |
| TF card CLK / CMD / D0 | 38 / 42 / 44 |
| Buzzer | 2 |

EPD power enable, XOE, MODE, VCOM_EN, touch reset and card detection are on FCA9555
Port-0; see the pin table in [main/apps/app_ioe.c](main/apps/app_ioe.c).

## Acknowledgments & License

- Firmware: Apache-2.0, see [LICENSE](LICENSE).
- [epdiy](https://github.com/vroland/epdiy): e-paper timing and rendering. This is a
  trimmed fork for the board's LCD path, licensed under LGPL-3.0-or-later.
  Local changes are listed in [components/epdiy/LICENSE](components/epdiy/LICENSE).
- [stb_truetype](https://github.com/nothings/stb): glyph rasterization, public domain.
- [pwm_audio](https://github.com/espressif/esp-iot-solution/tree/master/components/audio/pwm_audio):
  Espressif LEDC PWM audio, trimmed copy, Apache-2.0. See
  [components/pwm_audio/LICENSE](components/pwm_audio/LICENSE).
- Panel waveform tables ship with the board as-is under Apache-2.0. See
  [components/e0470_epaper_waveform/LICENSE](components/e0470_epaper_waveform/LICENSE).
- The built-in font, `main/assets/builtin.ttf`, is a subset of the
  [ChillDuanSans](https://github.com/Warren2060/ChillDuanSans) variable font by
  Warren2060, generated by `tools/gen_builtin_font.py`. The full font is not
  committed; the subset remains licensed under SIL OFL-1.1.

Thank you for your patience and support.

Shenzhen MindReset Technology Co., Ltd.
