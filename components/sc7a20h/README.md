# sc7a20h

ESP-IDF 下的 **SC7A20H** 三轴加速度计 C 驱动（士兰）。

ESP-IDF C driver for the **SC7A20H** 3-axis accelerometer (Silan Microelectronics).

面向手持与墨水屏设备：读取加速度、配置拿起唤醒、经 INT1 浅睡或深睡唤醒。敲击、6D、FIFO、自检等实验功能在 `sc7a20h_lab.h`。

Intended for handheld / e-paper devices: read acceleration, arm pickup-to-wake, and enter light or deep sleep on INT1. Lab helpers (click, 6D, FIFO, self-test) are in `sc7a20h_lab.h`.

组件名 / Registry name: `mindreset/sc7a20h`

## 能力 / Features

- Handle：调用方传入已有 `i2c_master` 总线、7 位地址、可选 INT1 脚。/ Handle-based API: pass an existing `i2c_master` bus, 7-bit address, and optional INT1 GPIO.
- 士兰上电流程：软复位 `0x68=0xA5`，WHO_AM_I `0x11` / VERSION，CTRL5.BOOT。/ Silan bring-up: soft reset `0x68=0xA5`, WHO_AM_I `0x11` / VERSION, CTRL5.BOOT.
- 默认低功耗 12.5 Hz。`power_down` 将 ODR 写为 0000（约 0.5 µA）。/ Low-power 12.5 Hz idle by default. `power_down` sets ODR=0000 (~0.5 µA).
- 拿起唤醒：AOI1 高事件 + HPIS1（滤除重力）+ INT1 锁存。/ Pickup-to-wake: AOI1 high-event + HPIS1 (gravity filtered) + INT1 latch.
- 浅睡 GPIO 唤醒（高有效）和 EXT1 深睡唤醒。/ Light-sleep GPIO wakeup (active high) and EXT1 deep-sleep wakeup.
- 实验室头文件：敲击、6D、自由落体、活动、FIFO、自检、寄存器转储。/ Lab header: click, 6D, freefall, activity, FIFO, self-test, register dump.

## 硬件 / Hardware

| 项目 / Item | 规格 / Spec |
|---|---|
| 芯片 / Part | SC7A20H（士兰）/ SC7A20H (Silan) |
| 总线 / Bus | I2C，400 kHz |
| 地址 / Address | `0x18`（SA0=GND）或 `0x19`（SA0=VCC，默认）/ `0x18` (SA0=GND) or `0x19` (SA0=VCC, default) |
| INT1 | 任意 GPIO；EXT1 深睡必须是 RTC 脚 / Any GPIO; RTC-capable pin required for EXT1 wakeup |
| 极性 / Polarity | 高有效（下拉 + 上升沿 / 高电平唤醒）/ Active high (pulldown + rising edge / high-level wakeup) |

## 安装 / Install

```yaml
dependencies:
  mindreset/sc7a20h: "^1.0.0"
```

或：/ Or:

```bash
idf.py add-dependency "mindreset/sc7a20h^1.0.0"
```

## 用法 / Quick start

```c
#include "sc7a20h.h"

sc7a20h_handle_t acc = NULL;
sc7a20h_hw_t hw = SC7A20H_HW_DEFAULT();
hw.bus = i2c_bus;
hw.int1_gpio = GPIO_NUM_1;

if (sc7a20h_create(&hw, &acc) == ESP_OK) {
    sc7a20h_sample_t s;
    sc7a20h_read(acc, &s);
    sc7a20h_power_down(acc);   // 界面不需要采样时掉电。/ Until the UI needs samples.
}

// 配置拿起唤醒，然后浅睡。/ Pickup-to-wake, then light sleep.
sc7a20h_arm_pickup_wake(acc, NULL);
sc7a20h_config_light_sleep_wakeup(acc);
esp_light_sleep_start();
sc7a20h_power_down(acc);
```

自定义动作阈值：

Custom motion threshold:

```c
sc7a20h_motion_cfg_t motion = SC7A20H_MOTION_DEFAULT();
motion.ths_mg = 400;
motion.duration = 3;
motion.axis_mask = SC7A20H_AXIS_X | SC7A20H_AXIS_Y;
sc7a20h_arm_pickup_wake(acc, &motion);
```

深睡（INT1 变高后 MCU 复位）：

Deep sleep (MCU resets on INT1 high):

```c
sc7a20h_arm_pickup_wake(acc, NULL);
sc7a20h_config_ext1_wakeup(acc);
esp_deep_sleep_start();
```

## 接口分组 / API groups

| 分组 / Group | 函数 / Functions |
|---|---|
| 生命周期 / Lifecycle | `sc7a20h_create`, `sc7a20h_del` |
| 电源 / Power | `sc7a20h_power_down`, `sc7a20h_power_up`, `sc7a20h_powered` |
| 读数 / Read | `sc7a20h_read` |
| 拿起 / Pickup | `sc7a20h_arm_pickup_wake`, `sc7a20h_config_light_sleep_wakeup`, `sc7a20h_config_ext1_wakeup` |
| 实验室 / Lab | [`sc7a20h_lab.h`](include/sc7a20h_lab.h) |

详见 [`include/sc7a20h.h`](include/sc7a20h.h)。

See [`include/sc7a20h.h`](include/sc7a20h.h).

## 示例 / Example

[`examples/pickup_wake`](examples/pickup_wake) 初始化 I2C、读取若干样本、配置拿起唤醒并进入浅睡。引脚在 `idf.py menuconfig` 中配置。

[`examples/pickup_wake`](examples/pickup_wake) brings up I2C, reads a few samples, arms pickup-to-wake, and light-sleeps. Pins are in `idf.py menuconfig`.

## 许可 / License

Apache-2.0
