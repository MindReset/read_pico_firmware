# stb_image

`stb_image.h` v2.30 is an unmodified upstream file from
[nothings/stb](https://github.com/nothings/stb/blob/master/stb_image.h).
Its MIT/public-domain dual-license notice is retained at the end of the header.
`book_image.c` enables JPEG/PNG only and supplies a bounded PSRAM allocator.

`stb_image.h` v2.30 为未修改的上游文件，文件末尾保留 MIT/公共领域双许可声明。
`book_image.c` 仅启用 JPEG/PNG，并提供有总量限制的 PSRAM 分配器。

`tjpgd.c` and `tjpgd.h` are unmodified TJpgDec R0.03 sources from
[Espressif esp_jpeg](https://github.com/espressif/idf-extra-components/tree/master/esp_jpeg/tjpgd).
ChaN's original redistribution notice is retained in `tjpgd.c`.
`tjpgdcnf.h` is the local configuration: RGB output, scaling enabled,
512-byte input buffer, optimization level 1, no default Huffman tables.

`tjpgd.c` 与 `tjpgd.h` 保留上游原文和 ChaN 的再分发声明。
`tjpgdcnf.h` 是本地配置：RGB输出、启用缩小、512字节输入缓冲、一级优化、不补缺失的霍夫曼表。

The caller converts saturated RGB blocks to grayscale. This avoids luminance
wraparound in the upstream grayscale path with optimization level 1.
调用方将已饱和的RGB块转灰度，避免上游一级优化灰度路径中的亮度溢出回绕。
