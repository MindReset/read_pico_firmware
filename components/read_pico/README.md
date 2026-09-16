# read_pico

小纸 Pico 墨水屏开发板（ESP32-S3 + 4.7 寸单色面板）的板级支持。只包含板级而非芯片级的内容：共享 I2C、epdiy 板定义、经 SY7636A 与 FCA9555 的屏电源轨、TF 卡、蜂鸣器、flash 高性能模式补丁。

Board support for the Read Pico e-paper dev board (ESP32-S3 + 4.7" mono panel). Owns
everything that is board-specific rather than chip-specific: the shared I2C bus, the
epdiy board definition, screen rails via SY7636A and an FCA9555 expander, the TF card,
the buzzer, and the flash high-performance-mode workaround.

组件名 / Registry name: `mindreset/read_pico`

## 做什么 / What it does

- `read_pico_init()`：初始化面板、探测 TF 卡、检查加速度计后使其掉电、启动 PMU 主机与触摸控制器。外设失败只清除对应的 `*_ready` 标志；仅面板失败视为致命。/ Brings up the panel, probes the card, checks the accelerometer and powers it back down, starts the PMU host and the touch controller. A failing peripheral only clears its `*_ready` flag; only the panel failing is fatal.
- `read_pico_get_status()` / `read_pico_get_ioe_status()`：扩展口引脚、电源轨状态与 SY7636A 寄存器的一份快照，供电源页和 IO 页使用。/ One snapshot of expander pins, rail state and the SY7636A registers, for the power and IO pages.
- 电源轨：`read_pico_rails_on()`、`read_pico_toggle_vcomctl()`，以及 epdiy 板级钩子。/ Rails: `read_pico_rails_on()`, `read_pico_toggle_vcomctl()`, plus the epdiy board hooks.
- 加速度计轴映射：芯片坐标 → 设备坐标（屏朝上为 +Z）。/ Accelerometer axis mapping: chip → device coordinates (screen up is +Z).
- `read_pico_sd_*`：非阻塞的探卡、挂载、格式化与容量查询。/ Non-blocking card probe, mount, format, capacity.
- `read_pico_buzzer_tone()`：LEDC 发声，按给定时长阻塞。/ LEDC tone, blocking for the given duration.
- `read_pico_flash_hpm.c`：本板的 Zbit（`0x5E`）flash 需要写状态寄存器才能运行于 120 MHz；IDF 自带的表只包含 0xC8 / 0x20 厂家。/ This board ships a Zbit (`0x5E`) flash whose status register must be written to reach 120 MHz; IDF's table only lists the 0xC8 / 0x20 vendors.

## 引脚 / Pins

可通过 `idf.py menuconfig` → *Read Pico board configuration* 配置：I2C 端口、SCL/SDA、总线频率、FCA9555 INT、CST836U INT。面板总线、TF 卡与蜂鸣器引脚固定在 `read_pico_board.c` / `read_pico_sd.c` / `read_pico_buzzer.c` 中。

Configurable through `idf.py menuconfig` → *Read Pico board configuration*: I2C port,
SCL/SDA, bus frequency, FCA9555 INT and CST836U INT. The panel bus, TF card and buzzer
pins are fixed in `read_pico_board.c` / `read_pico_sd.c` / `read_pico_buzzer.c`.

## 依赖 / Dependencies

`epdiy`、`sy7636a`、`fca9555`、`cst836u`、`sc7a20h`、`read_pico_pmu`、`e0470_epaper_waveform`。

`epdiy`, `sy7636a`, `fca9555`, `cst836u`, `sc7a20h`, `read_pico_pmu`,
`e0470_epaper_waveform`.

## 许可 / License

Apache-2.0，见 [LICENSE](LICENSE)。

Apache-2.0, see [LICENSE](LICENSE).
