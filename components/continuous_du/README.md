# continuous_du

面向 epdiy LCD 输出路径的逐像素连续 DU：每次更新不再执行完整的 DU 波形，而是每轮扫描只输出 **一个** 相位，由每个像素各自记录剩余相位。标记形状只对计数累加，因此手指划过时，新位置在几轮扫描中逐渐压黑，上一位置同时仍在擦白。

Per-pixel continuous DU for the epdiy LCD output path: instead of running a whole
DU waveform per update, each scan pushes **one** phase and every pixel keeps its own
remaining-phase counter. Marking a shape only adds to those counters, so a finger
being dragged across the screen leaves a dot that darkens over several scans while
the previous position is still being erased.

组件名 / Registry name: `mindreset/continuous_du`

## 能力 / Features

- `continuous_du_mark_circle()` / `continuous_du_mark_rect()`：正相位压黑，负相位擦白；标记是累加而不是覆盖。/ Positive phases push toward black, negative toward white; marks accumulate instead of replacing.
- `continuous_du_scan()`：对整块已标记区域扫一个相位，1216 × 684 面板大约 50 ms，和标记了多少像素无关。/ One phase for the whole marked area, ~50 ms on a 1216 × 684 panel, independent of how many pixels are marked.
- `continuous_du_busy()`：是否还有像素没走完相位。/ Whether any pixel still owes phases.
- `continuous_du_reset()`：清除全部计数；调用方须先将 epdiy 参考 framebuffer 与物理屏对齐，再进行下一次 diff。/ Drop all counters; the caller must then align the epdiy reference framebuffer with what is physically on the panel before the next diff.
- 旋转 framebuffer 的逻辑坐标与面板坐标换算。/ Logical ↔ panel coordinate helpers for rotated framebuffers.

## 为什么 / Why

若每个触摸报点都发送完整 DU，跟手速率会受限于波形长度。将波形拆分到多次扫描中，「这个点有多黑」与「多久扫描一次」即可解耦，本仓库的触摸演示才能做到跟手。

Sending a full DU per touch report caps the follow rate at the waveform length.
Splitting the waveform across scans decouples "how dark is this dot" from "how often
can I scan", which is what makes the touch demo in this repo track a finger.

## 用法 / Usage

见本仓库固件的 `main/apps/app_touch.c`：标记新圆点、擦除旧圆点，busy 期间调用 `continuous_du_scan()`，抬手时先对齐 `hl.back_fb`，再回到普通 GC16 刷新。

See `main/apps/app_touch.c` in the Read Pico firmware: mark the new dot positions, unmark
the old ones, call `continuous_du_scan()` while busy, and on finger-up fix up
`hl.back_fb` before falling back to a normal GC16 update.

## 许可 / License

Apache-2.0，见 [LICENSE](LICENSE)。

Apache-2.0, see [LICENSE](LICENSE).
