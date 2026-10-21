/* SPDX-License-Identifier: Apache-2.0 */
#include "book_cover.h"
#include <string.h>

bool book_jpeg_frame(const uint8_t *data, size_t size, uint8_t *sof,
                       unsigned *width, unsigned *height) {
    if (!data || size < 4 || data[0] != 0xFF || data[1] != 0xD8) return false;
    size_t at = 2;
    while (at + 1 < size) {
        if (data[at] != 0xFF) { ++at; continue; }   // 段间的填充字节 / fill bytes between segments
        uint8_t marker = data[at + 1];
        if (marker == 0xFF) { ++at; continue; }
        // 无长度字段的标记。/ Markers without a length field.
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD9)) { at += 2; continue; }
        if (at + 4 > size) return false;
        size_t length = ((size_t)data[at + 2] << 8) | data[at + 3];
        if (length < 2) return false;
        // SOF0..SOF15，跳过 DHT(0xC4)、JPG(0xC8)、DAC(0xCC)。
        // SOF0..SOF15, skipping DHT (0xC4), JPG (0xC8) and DAC (0xCC).
        if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
            if (at + 9 > size) return false;
            if (height) *height = ((unsigned)data[at + 5] << 8) | data[at + 6];
            if (width) *width = ((unsigned)data[at + 7] << 8) | data[at + 8];
            if (sof) *sof = marker;
            return true;
        }
        at += 2 + length;
    }
    return false;
}

bool book_image_dimensions(const uint8_t *data, size_t size, bool png,
                           unsigned *width, unsigned *height) {
    if (!data || !width || !height) return false;
    *width = *height = 0;
    if (png) {
        static const uint8_t signature[8] = {137,80,78,71,13,10,26,10};
        if (size < 24 || memcmp(data, signature, 8) || memcmp(data + 12, "IHDR", 4)) return false;
        *width = (uint32_t)data[16] << 24 | (uint32_t)data[17] << 16 | (uint32_t)data[18] << 8 | data[19];
        *height = (uint32_t)data[20] << 24 | (uint32_t)data[21] << 16 | (uint32_t)data[22] << 8 | data[23];
    } else {
        unsigned frame_width = 0, frame_height = 0;
        // 帧头对基线、渐进式都能给出尺寸，而 esp_jpeg 只认基线。
        // The frame header yields the size for baseline and progressive alike, where esp_jpeg
        // only handles baseline.
        if (!book_jpeg_frame(data, size, NULL, &frame_width, &frame_height)) return false;
        *width = frame_width; *height = frame_height;
    }
    return *width > 0 && *height > 0 && *width <= 8192 && *height <= 8192;
}
