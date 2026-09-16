# epdiy（裁剪 fork / trimmed fork）

小纸 Pico 使用的墨水屏渲染器。本目录是 [epdiy](https://github.com/vroland/epdiy) v2.0.0 的 fork，按本板需要裁剪到 ESP32-S3 的 LCD 外设输出路径与对应的显示定义。

E-paper renderer for the Read Pico board. This directory is a fork of [epdiy](https://github.com/vroland/epdiy) v2.0.0, trimmed to the ESP32-S3 LCD peripheral output path and the display definition this board needs.

上游 / Upstream: https://github.com/vroland/epdiy
许可 / License: LGPL-3.0-or-later

## 与上游的差异 / What changed from upstream

- 删除 ESP32 的 I2S 输出路径（`src/output_i2s/`）、社区波形表与示例图片。/ The ESP32 I2S output path (`src/output_i2s/`), community waveform tables and example images are removed.
- `src/board/` 换成本板定义；引脚在 `components/read_pico/read_pico_board.c` 中注册。/ `src/board/` is replaced by this board's definition; pins are registered from `components/read_pico/read_pico_board.c`.
- `src/displays.c` 只保留 4.7 寸 1216 × 684 单色面板（`E0470_DISPLAY`）。/ `src/displays.c` keeps only the 4.7" 1216 × 684 monochrome panel (`E0470_DISPLAY`).
- 波形表改为从 `components/e0470_epaper_waveform` 获取，不再使用内置表。/ Waveform tables come from `components/e0470_epaper_waveform` instead of the built-in set.
- `src/render.c` / `src/highlevel.c`：差分按列裁剪，按 (from, to) 直方图无损跳过前导空相位，并记录各阶段耗时。/ Diff is cropped by column, leading empty phases are skipped losslessly using a (from, to) histogram, and per-stage timing is recorded.
- `src/output_lcd/render_lcd.c`：每帧前的预填行数可配置。/ Configurable number of pre-fill lines before each frame.

完整清单同时保留在 [LICENSE](LICENSE) 中。

The full list is also kept in [LICENSE](LICENSE).

## 公开头文件 / Public headers

- `include/epd_lcd.h`：LCD 总线配置与像素时钟控制。/ LCD bus configuration and pixel-clock control.
- `include/epd_waveform.h`：波形组件使用的 `EpdWaveform` / `EpdWaveformPhases` 类型。/ `EpdWaveform` / `EpdWaveformPhases` types used by the waveform component.
- `src/epdiy.h`、`src/epd_highlevel.h`：epdiy 的绘制与高层刷新接口，形态与上游一致。/ The epdiy drawing and high-level update API, unchanged in shape from upstream.

## 说明 / Notes

本 fork 只维护到小纸 Pico 固件需要的程度。仓库其它部分的注释整理与风格统一不涉及本目录；上游文件头保持原样。

This fork is maintained only as far as the Read Pico firmware needs it. Comment cleanup and style passes in the rest of the repository do not touch this directory; upstream file headers are kept as they were.

## 许可 / License

LGPL-3.0-or-later。完整文本见 https://www.gnu.org/licenses/lgpl-3.0.txt，适用于本目录全部文件，除非某文件另有说明。

LGPL-3.0-or-later. The full text is at https://www.gnu.org/licenses/lgpl-3.0.txt and applies to every file in this directory unless a file states otherwise.
