#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "ui_image_dither.h"

static void test_dither(void) {
    for (int gray = 0; gray <= 255; ++gray) {
        uint8_t first[16];
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
            uint8_t out = ui_image_dither_gray((uint8_t)gray, x, y);
            first[y * 4 + x] = out;
            assert(out % 17 == 0);
            int lo = gray / 17 * 17;
            int hi = lo < 255 ? lo + 17 : 255;
            assert(out == lo || out == hi);
            assert(out == ui_image_dither_gray((uint8_t)gray, x, y));
        }
        for (int i = 0; i < 16; ++i)
            assert(first[i] == ui_image_dither_gray((uint8_t)gray, i & 3, i >> 2));
    }
    for (int level = 0; level < 16; ++level)
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x)
            assert(ui_image_dither_gray((uint8_t)(level * 17), x, y) == level * 17);
}

int main(void) {
    test_dither();
    puts("image dither host tests passed");
    return 0;
}
