# cst836u

ESP-IDF 下的 **CST836U** 自电容触摸 C 驱动（海栎创）。

ESP-IDF C driver for the **CST836U** self-cap touch controller (Hynitron).

面向手持与墨水屏设备：两点报点、轮询 INT、深度睡眠。手册中的待机手势唤醒依赖对应固件，本驱动不实现。

Intended for handheld / e-paper devices: 2-point report, IRQ poll, and deep sleep. Datasheet standby (gesture wake) needs matching firmware and is not in this driver.

组件名 / Registry name: `mindreset/cst836u`

## 能力 / Features

- Handle：调用方传入已有 `i2c_master` 总线、7 位地址、可选 INT / RST 脚。/ Handle-based API: pass an existing `i2c_master` bus, 7-bit address, optional INT / RST GPIO.
- 上电读取 `0xA6`：固件 / 编号 / 模组 / 项目 / 型号。/ Read FW / ID / module / project / type at `0xA6`.
- 从 `0x00` 解析两点（按下 / 抬起 / 移动）。/ Two-point parse from register `0x00` (down / up / move).
- 本驱动实现的手册工作模式：/ Work modes from the datasheet that this firmware actually implements:
  - 动态报点 `0xFE00`，典型 **&lt; 5 mA**。/ Dynamic report `0xFE00`, typical **&lt; 5 mA**.
  - 深度睡眠 `0xA503`，典型 **10 µA**；不再应答 I2C，RST 后重新写入 `0xFE00`。/ Deep sleep `0xA503`, typical **10 µA**; I2C stops, wake with RST then `0xFE00`.
- RST 可以是本机 GPIO，也可以是 `reset_fn`（位于 IO 扩展上）。/ RST via a native GPIO or a `reset_fn` (IO expander).
- IRQ 开漏、低有效。/ IRQ is open-drain, active low.

## 硬件 / Hardware

| 项目 / Item | 规格 / Spec |
|---|---|
| 芯片 / Part | CST836U（海栎创）/ CST836U (Hynitron) |
| 总线 / Bus | I2C，400 kHz |
| 地址 / Address | `0x15`（写 `0x2A`，读 `0x2B`）/ `0x15` (write `0x2A`, read `0x2B`) |
| INT | 任意 GPIO；开漏低有效，10 kΩ 上拉 / Any GPIO; open-drain, active low, 10 kΩ pull-up |
| RST | 任意 GPIO；在扩展口上则用 `reset_fn` / Any GPIO, or `reset_fn` if the line sits on an expander |
| 供电 / Supply | 2.8 V ~ 3.6 V |

手册还定义了待机模式（约 1.5 mA，50 Hz 扫描 + 手势 IRQ）。许多模组的出厂固件并未实现。

Datasheet standby (~1.5 mA, 50 Hz scan + gesture IRQ) is a third mode. Stock firmware on many modules does not implement it.

## 安装 / Install

```yaml
dependencies:
  mindreset/cst836u: "^1.0.0"
```

或：/ Or:

```bash
idf.py add-dependency "mindreset/cst836u^1.0.0"
```

## 用法 / Quick start

```c
#include "cst836u.h"

cst836u_handle_t tp = NULL;
cst836u_hw_t hw = CST836U_HW_DEFAULT();
hw.bus = i2c_bus;
hw.int_gpio = GPIO_NUM_43;
hw.rst_gpio = GPIO_NUM_NC;   // 或本机 RST 脚。/ Or a native RST pin.
hw.reset_fn = board_tp_reset; // rst_gpio 为 NC 时用。/ Optional, used when rst_gpio is NC.

if (cst836u_create(&hw, &tp) == ESP_OK) {
    cst836u_touch_t t;
    cst836u_read(tp, &t);
}

// 主机休眠：约 10 µA，直到 RST。/ Host suspend: ~10 µA until RST.
cst836u_set_mode(tp, CST836U_MODE_DEEPSLEEP);
// ...
cst836u_wake(tp);
```

扩展口复位：

IO-expander reset:

```c
static esp_err_t board_tp_reset(void) {
    expander_set_rst(0);
    vTaskDelay(pdMS_TO_TICKS(10));
    return expander_set_rst(1);
}
```

## 接口分组 / API groups

| 分组 / Group | 函数 / Functions |
|---|---|
| 生命周期 / Lifecycle | `cst836u_create`, `cst836u_del` |
| 读数 / Read | `cst836u_read`, `cst836u_get_info` |
| 电源 / Power | `cst836u_set_mode`, `cst836u_get_mode`, `cst836u_reset`, `cst836u_wake` |
| 中断 / IRQ | `cst836u_int_asserted`, `cst836u_int_level`, `cst836u_int_gpio` |

详见 [`include/cst836u.h`](include/cst836u.h)。

See [`include/cst836u.h`](include/cst836u.h).

## 示例 / Example

[`examples/touch_read`](examples/touch_read) 初始化 I2C 并输出触点坐标；接有 RST 时会演示睡眠与唤醒。引脚在 `idf.py menuconfig` 中配置。

[`examples/touch_read`](examples/touch_read) brings up I2C, logs points, and (if RST is wired) sleeps then wakes. Pins are in `idf.py menuconfig`.

## 许可 / License

Apache-2.0
