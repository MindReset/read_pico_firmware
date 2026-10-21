/* SPDX-License-Identifier: Apache-2.0
 * 中文：内建黑体的常用汉字位图补充，不依赖 TF 卡。/ English: Built-in common Han glyph supplement, independent of the card.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define UI_HANZI_BASE_PX 24
#define UI_HANZI_RECORD 77
/// 只检查索引，不分配缓存。/ Index lookup without allocating a cache.
bool ui_hanzi_has(uint32_t cp);
/// 记录包含 x/y 偏移、宽高、字宽和 24×24 单色位图；只解压所在小块。
/// Record: x/y offsets, width/height, advance and a 24x24 mono bitmap; decompress its small block only.
bool ui_hanzi_get(uint32_t cp, uint8_t record[UI_HANZI_RECORD]);
