# sy7636a

ESP-IDF 下的 **SY7636A** 墨水屏 PMIC C 驱动（矽力杰）：VCOM 设置、轨控制、故障解码，以及波形查找要用的片上温度传感器。

ESP-IDF C driver for the **SY7636A** e-paper PMIC (Silergy): VCOM setting, rail control, fault decode, and the on-chip temperature sensor the waveform lookup needs.

组件名 / Registry name: `mindreset/sy7636a`

## 能力 / Features

- Handle：调用方传入已有 `i2c_master` 总线；EN / VCOM_EN / PGOOD 可以是本机 GPIO 或回调，位于 IO 扩展上的信号线同样可用。/ Handle-based API: pass an existing `i2c_master` bus; EN / VCOM_EN / PGOOD can each be a native GPIO or a callback, so lines sitting on an IO expander work too.
- VCOM 以 mV 计，支持手动 / 自动 VCOM 控制，可回读已存设定。/ VCOM in mV, manual / auto VCOM control, readback of the stored setting.
- 电源轨开关并等待 PGOOD，VLDO 选择，各轨放电与上电延时寄存器。/ Rail on/off that waits for PGOOD, VLDO selection, per-rail discharge and power-up delay registers.
- 故障寄存器解码（各轨 UVP / SCP、OTP）为单一枚举。/ Fault register decode (UVP / SCP per rail, OTP) into a single enum.
- 温度以 °C 读出，用于选择波形温度档。/ Temperature in °C, used to pick the waveform temperature range.

## 硬件 / Hardware

| 项目 / Item | 规格 / Spec |
|---|---|
| 芯片 / Part | SY7636A（矽力杰）/ SY7636A (Silergy) |
| 总线 / Bus | I2C，400 kHz |
| 地址 / Address | `0x62`（`SY7636A_ADDR_DEFAULT`） |
| PGOOD | 开漏；小纸 Pico 上经 IO 扩展脚读回 / Open-drain; on Read Pico it is read back through an IO expander pin |

## 用法 / Usage

```c
sy7636a_config_t cfg = SY7636A_CONFIG_DEFAULT();
cfg.en_fn = board_sy_en;        // 本板的 EN 位于 FCA9555 上。/ EN sits on the FCA9555 on this board.
cfg.pgood_fn = board_pgood;

sy7636a_handle_t pmic;
sy7636a_init(bus, &cfg, &pmic);
sy7636a_set_vcom(pmic, 1290);   // -1.29 V
sy7636a_power_on(pmic);

sy7636a_status_t st;
sy7636a_read(pmic, &st);        // vcom_mv / temperature_c / fault / vldo
```

完整转储见 `examples/read_status`。

See `examples/read_status` for a full dump.

## 许可 / License

Apache-2.0，见 [LICENSE](LICENSE)。

Apache-2.0, see [LICENSE](LICENSE).
