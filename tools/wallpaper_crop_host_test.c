/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "ui_wallpaper_crop.h"

static void check(unsigned source_w, unsigned source_h, unsigned target_w, unsigned target_h) {
    ui_wallpaper_crop_t crop = ui_wallpaper_center_crop(source_w, source_h, target_w, target_h);
    assert(crop.width > 0 && crop.height > 0);
    assert(crop.x + crop.width <= source_w && crop.y + crop.height <= source_h);
    assert(crop.x == (source_w - crop.width) / 2);
    assert(crop.y == (source_h - crop.height) / 2);
    assert(crop.width == source_w || crop.height == source_h);
    for (unsigned y = 0; y < target_h; ++y) {
        for (unsigned x = 0; x < target_w; ++x) {
            unsigned mapped_x = crop.x + (uint64_t)x * crop.width / target_w;
            unsigned mapped_y = crop.y + (uint64_t)y * crop.height / target_h;
            assert(mapped_x < source_w && mapped_y < source_h);
        }
    }
}

int main(void) {
    check(684, 1216, 684, 1216);
    check(912, 684, 684, 1216);
    check(1024, 1024, 684, 1216);
    check(500, 1100, 352, 626);
    check(1216, 1, 684, 1216);
    puts("wallpaper crop: ok");
    return 0;
}
