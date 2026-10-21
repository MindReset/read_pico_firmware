/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 图片专用的稳定 16 级有序抖动；文字和界面线条不要经过这里。
 * Stable 16-level ordered dither for images only; UI text and rules bypass it.
 */
#pragma once

#include <stdint.h>

/// 把 8 位灰度映射到相邻的两个 4 位物理灰阶，坐标决定稳定的 4x4 Bayer 相位。
/// Map 8-bit gray to adjacent physical 4-bit levels with a stable 4x4 Bayer phase.
uint8_t ui_image_dither_gray(uint8_t gray, int x, int y);
/// 主页面黑白图片点阵；不会对阅读插图启用。/ Black/white image dots for main screens; not enabled for reader illustrations.
uint8_t ui_image_dither_bw(uint8_t gray, int x, int y);
/// 快刷封面黑白点阵保留1/16白色混入，略增黑点；不影响文字、图标或阅读插图。/ Fast cover BW dots retain a 1/16 white lift with slightly denser blacks; text, icons and reader illustrations remain unaffected.
uint8_t ui_image_dither_cover_bw(uint8_t gray, int x, int y);
/// 亚克力用一像素规则棋盘格点阵，固定坐标且保持黑白端点，无额外内存。/ One-pixel regular checkerboard dots for acrylic, coordinate-stable and endpoint-preserving, with no extra memory.
uint8_t ui_image_dither_acrylic_bw(uint8_t gray, int x, int y);
