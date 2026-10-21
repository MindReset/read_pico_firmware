# bitbank2__jpegdec — 本地裁剪 fork / local trimmed fork

中文：这是 [JPEGDEC](https://github.com/bitbank2/JPEGDEC) 的本地 fork，只为一件事存在：
ESP32 的 ROM TJpgDec（`espressif/esp_jpeg`）**只解基线 JPEG**，而电子书封面和插图里
存在渐进式（SOF2）JPEG，那些图片目前只能退回自动生成封面。JPEGDEC 能解渐进式
（DC-only，内部强制 1/8 缩放），代价是约 20 KB 常驻内存。

English: A local fork of [JPEGDEC](https://github.com/bitbank2/JPEGDEC) that exists for one
reason: the ESP32 ROM TJpgDec behind `espressif/esp_jpeg` decodes baseline JPEG only, while
ebook covers and illustrations do contain progressive (SOF2) JPEG, which currently fall back to
a generated cover. JPEGDEC decodes progressive JPEG from the DC scan at 1/8 scale for roughly
20 KB of constant memory.

- 上游版本 / Upstream version: 1.8.4
- 许可 / License: Apache-2.0，全文见 [LICENSE](LICENSE)
- 引用方式 / Wired in with: `main/idf_component.yml` 的 `override_path`

## 改动清单 / Change list

1. **只注册可移植路径。** `CMakeLists.txt` 只编译 `src/JPEGDEC.cpp`（`jpeg.inl` 由它 include）。
   移除了 `src/*.S`（ESP32-S3 SIMD，纯 ESP-IDF 下本来就被 `ARDUINO_ARCH_ESP32` 关掉）与
   未被引用的 `src/JPEGDisplay.*`，并去掉上游对 `esp-dsp` 和 `arduino-esp32` 的依赖。

   **Portable path only.** Only `src/JPEGDEC.cpp` is compiled. The `src/*.S` SIMD kernels
   (already disabled under plain ESP-IDF by the `ARDUINO_ARCH_ESP32` guard) and the unreferenced
   `src/JPEGDisplay.*` are gone, along with the upstream `esp-dsp` and `arduino-esp32`
   requirements.

2. **灰度渐进式的三个修复**（均落在 `src/jpeg.inl`，与 CrossMux 所用的一致）。
   灰度输出时色度块走 `MCU_SKIP`，这条路径上游有缺陷：

   **Three fixes for grayscale progressive** (all in `src/jpeg.inl`), matching what CrossMux
   carries. With grayscale output the chroma blocks take the `MCU_SKIP` path, which upstream
   gets wrong:

   | 位置 / Site | 问题 / Problem |
   | --- | --- |
   | `JPEGDecodeMCU_P` `iMCU` 指针 | `MCU_SKIP` 时 `&sMCUs[iMCU & 0xffffff]` 是越界约 33 MB 的野指针，AC 写入会 store-fault。改为 `iMCU < 0` 时指向 `sMCUs[0]`。 / A wild pointer ~33 MB past `sMCUs` that store-faults on AC writes; redirect to `sMCUs[0]` when `iMCU < 0`. |
   | `JPEGDecodeMCU_P` DC 写入 | 上一步的重定向让解引用安全了，但写入会覆盖刚解出的 Y DC；加 `iMCU >= 0` 保护。 / The redirect makes the dereference safe, but the write would clobber the just-decoded luma DC; guard with `iMCU >= 0`. |
   | `DecodeJPEG` 灰度渐进分支 | 一个渐进式扫描可以只含亮度、不含色度，必须按 `JPCI[n].component_needed` 决定是否消费熵数据，否则后续扫描错位。 / A progressive scan may carry luma without chroma, so entropy data must only be consumed for components the scan declares, or later scans desynchronize. |

   前两项来自 CrossMux 的 `scripts/jpegdec_patches/0001`、`0002`；第三项是同一批补丁里的
   `0003`（上游贡献者 Leopoldo Pla）。上游 master 均未合并，registry 上也只有 2025-01 的
   1.6.2，所以这里直接把改好的源码留在树里。

   The first two come from CrossMux's `0001`/`0002`; the third is `0003` from the same set.
   None are in upstream master and the registry only has 1.6.2 from 2025-01, so the patched
   sources stay in the tree instead.

## 升级上游时 / When updating upstream

`src/jpegdec_shim.cpp` 还提供 `FILE*` 回调包装，渐进式 TF 图片按块读取和输出，
不把整个压缩文件读入内存。基线 TF JPEG 使用 ROM TJpgDec 的流式接口。
The C wrapper also accepts `FILE*` callbacks for streaming progressive TF images;
baseline TF JPEG uses the ROM TJpgDec streaming interface.

用新版本覆盖 `src/`，重新套用上面三项改动，并保持 `CMakeLists.txt` 只注册可移植路径。
Re-copy `src/`, re-apply the three changes above, and keep `CMakeLists.txt` on the portable
path only.
