/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：有界 JPEG/PNG 解码与灰度缩放，调用方持有输出。
 * English: Bounded JPEG/PNG decoding and grayscale resizing; caller owns output.
 * 冻结：不访问文件和显示；失败交还空输出，不影响正文阅读。
 * Frozen: No file or display access; failure leaves empty output and must not block text reading.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/// 解码到不超过648×1000灰度图；输入至多2MiB；基线JPEG至多16M像素，其余至多1M像素、解码堆至多4MiB。
/// Decode at most 648x1000 grayscale pixels; input <=2MiB; baseline JPEG <=16M pixels, others <=1M pixels with a <=4MiB decoder heap.
bool book_image_decode(const uint8_t* data, size_t len, size_t budget,
                       uint8_t** pixels, uint16_t* width, uint16_t* height);
