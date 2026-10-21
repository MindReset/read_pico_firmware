/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdint.h>

typedef struct {
    unsigned x, y, width, height;
} ui_wallpaper_crop_t;

// 在已按原比例解码的图片中取居中区域，填满目标且不拉伸。
// Select a centered region in the aspect-preserved image to cover the target without stretching.
static inline ui_wallpaper_crop_t ui_wallpaper_center_crop(unsigned width, unsigned height,
                                                            unsigned target_width, unsigned target_height) {
    ui_wallpaper_crop_t crop = {0, 0, width, height};
    if (!width || !height || !target_width || !target_height) return crop;
    if ((uint64_t)width * target_height > (uint64_t)height * target_width) {
        crop.width = (unsigned)((uint64_t)height * target_width / target_height);
        if (!crop.width) crop.width = 1;
        crop.x = (width - crop.width) / 2;
    } else {
        crop.height = (unsigned)((uint64_t)width * target_height / target_width);
        if (!crop.height) crop.height = 1;
        crop.y = (height - crop.height) / 2;
    }
    return crop;
}
