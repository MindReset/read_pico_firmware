# fca9555

ESP-IDF 下的 **FCA9555** 16 位 I2C GPIO 扩展 C 驱动（徕飞）。寄存器和读写时序与 PCA9555 / TCA9555 / xCA9555 兼容。

ESP-IDF C driver for the **FCA9555** 16-bit I2C GPIO expander (Nyfea). Register map and I2C framing match PCA9555 / TCA9555 / xCA9555.

组件名 / Registry name: `mindreset/fca9555`

## 能力 / Features

- Handle：调用方传入已有 `i2c_master` 总线、7 位地址、可选 INT# 脚。/ Handle-based API: pass an existing `i2c_master` bus, 7-bit address, optional INT# GPIO.
- 顺序读取 8 个寄存器，`fca9555_map_t` 覆盖 IN / OUT / INV / CFG。/ Sequential register read; `fca9555_map_t` overlays IN / OUT / INV / CFG.
- 缓存 CFG / INV，轮询时可只读 IN / OUT。/ Cached CFG / INV so polling can read IN / OUT only.
- INT# 配置为输入上拉；开漏、**不锁存**，读取 Input 即释放。/ INT# as input with pull-up; open-drain, **not latched**; read Input to release.
- 方向与极性写入只保留在驱动内，不建议暴露到产品界面。/ Direction and polarity writes stay in the driver; do not expose them on the product UI.

## 硬件 / Hardware

| 项目 / Item | 规格 / Spec |
|---|---|
| 芯片 / Part | FCA9555（徕飞），兼容 PCA9555 / FCA9555 (Nyfea), PCA9555-compatible |
| 总线 / Bus | I2C，400 kHz。读：先写 pointer，再 Repeated START / Read: write pointer, then Repeated START |
| 地址 / Address | A2/A1/A0 决定 `0x20`…`0x27`。`FCA9555_ADDR_FROM_A(1,0,0)` → `0x24` / `0x20`…`0x27` from A2/A1/A0 |
| INT# | 任意 GPIO；开漏低有效。GPIO41 不是 RTC 脚，只能浅睡唤醒 / Any GPIO; open-drain, active low. GPIO41 is not RTC, light sleep only |
| 上电 / Power-on | 全部输入（`CFG=0xFFFF`），输出 `0xFFFF`，极性 `0x0000` / All pins input (`CFG=0xFFFF`), outputs `0xFFFF`, invert `0x0000` |

需要先 STOP 再读的芯片（例如 SY7636A）不能使用这种读法。本芯片要求 Repeated START。

Do not use Repeated START on chips that require STOP-then-read (for example SY7636A). This part requires Repeated START.

## 安装 / Install

```yaml
dependencies:
  mindreset/fca9555: "^1.0.0"
```

或：/ Or:

```bash
idf.py add-dependency "mindreset/fca9555^1.0.0"
```

I2C 超时在 `idf.py menuconfig` → Component config → FCA9555 I/O Expander（默认 100 ms）。

I2C timeout is in `idf.py menuconfig` → Component config → FCA9555 I/O Expander (default 100 ms).

## 用法 / Quick start

```c
#include "fca9555.h"

fca9555_handle_t ioe = NULL;
fca9555_hw_t hw = FCA9555_HW_DEFAULT();
hw.bus = i2c_bus;
hw.i2c_addr = FCA9555_ADDR_FROM_A(1, 0, 0);
hw.int_gpio = GPIO_NUM_41;

if (fca9555_create(&hw, &ioe) == ESP_OK) {
    fca9555_set_config(ioe, 0, 0x64);   // P0.2 / P0.5 / P0.6 输入。/ Inputs on P0.2 / P0.5 / P0.6.
    fca9555_set_output(ioe, 0, 0x81);

    fca9555_map_t map;
    fca9555_read_io(ioe, &map);         // IN/OUT 实时读取，CFG/INV 取缓存。/ IN/OUT live, CFG/INV cached.
    if (fca9555_int_asserted(ioe)) {
        fca9555_clear_int(ioe);
    }
}
```

## 接口分组 / API groups

| 分组 / Group | 函数 / Functions |
|---|---|
| 生命周期 / Lifecycle | `fca9555_create`, `fca9555_del` |
| 读数 / Read | `fca9555_read_reg`, `fca9555_read_n`, `fca9555_read_all`, `fca9555_read_io`, `fca9555_read_input` |
| 输出 / Output | `fca9555_set_output` |
| INT# | `fca9555_int_asserted`, `fca9555_int_level`, `fca9555_int_gpio`, `fca9555_clear_int` |
| 方向缓存 / Direction cache | `fca9555_dir_cached`, `fca9555_get_dir_cache` |
| 危险 / Dangerous | `fca9555_set_config`, `fca9555_set_inversion` |

详见 [`include/fca9555.h`](include/fca9555.h)。

See [`include/fca9555.h`](include/fca9555.h).

## 示例 / Example

[`examples/read_ports`](examples/read_ports) 初始化 I2C、输出 8 个寄存器并轮询 INT#。引脚在 `idf.py menuconfig` 中配置。

[`examples/read_ports`](examples/read_ports) brings up I2C, dumps the eight registers, and polls INT#. Pins are in `idf.py menuconfig`.

## 许可 / License

Apache-2.0
