/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "book_cover.h"
int main(int argc, char **argv) {
    assert(argc > 2);
    for (int i = 2; i < argc; ++i) {
        bool png = strstr(argv[i], ".png") != NULL;
        unsigned w = 0, h = 0;
        const unsigned ow = 176, oh = 240;
        uint8_t *pixels = malloc((size_t)ow * oh + 16);
        assert(pixels);
        memset(pixels, 0xcc, (size_t)ow * oh + 16);
        if (!strcmp(argv[1], "reject")) {
            assert(!book_image_file_grayscale(argv[i], png, ow, oh, pixels));
        } else {
            assert(book_image_file_dimensions(argv[i], png, &w, &h));
            assert(book_image_file_grayscale(argv[i], png, ow, oh, pixels));
            unsigned sum = 0;
            for (unsigned j = 0; j < ow * oh; ++j) sum += pixels[j];
            assert(sum > 30u * ow * oh && sum < 230u * ow * oh);
            printf("decoded: %s (%ux%u)\n", argv[i], w, h);
        }
        for (unsigned j = 0; j < 16; ++j) assert(pixels[(size_t)ow * oh + j] == 0xcc);
        free(pixels);
    }
}
