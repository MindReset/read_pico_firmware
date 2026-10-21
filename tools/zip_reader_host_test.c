/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 运行真实 ZIP 解析与边界校验，inflate 在主机适配到 zlib。
 * Run real ZIP parsing and boundary checks, adapting host inflate to zlib.
 * 冻结：只读取测试夹具。/ Frozen: Read test fixtures only.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zip_reader.h"

int main(int argc, char** argv) {
    assert(argc >= 3);
    zip_reader_t* zip = NULL;
    esp_err_t err = zip_open(argv[2], &zip);
    if (!strcmp(argv[1], "reject-open")) {
        assert(err != ESP_OK && zip == NULL);
        return 0;
    }
    assert(err == ESP_OK && zip);
    assert(zip_find(zip, "not-present") == -1);
    assert(zip_find(NULL, "x") == -1);
    assert(zip_entry_size(zip, -1) == 0);
    if (!strcmp(argv[1], "empty")) { zip_close(zip); return 0; }
    assert(argc >= 4);
    int index = zip_find(zip, argv[3]);
    assert(index >= 0);
    size_t size = zip_entry_size(zip, index);
    if (!strcmp(argv[1], "prefix")) {
        unsigned char bounded[257 + 16];
        memset(bounded, 0xcc, sizeof(bounded));
        const size_t written = size < 257 ? size : 257;
        assert(zip_extract_prefix(zip, index, bounded, 257) == ESP_OK);
        assert(argc == 5);
        FILE *expected = fopen(argv[4], "rb");
        assert(expected);
        for (size_t i = 0; i < written; ++i) assert(fgetc(expected) == bounded[i]);
        for (size_t i = written; i < sizeof(bounded); ++i) assert(bounded[i] == 0xcc);
        fclose(expected);
        zip_close(zip);
        return 0;
    }
    unsigned char* out = malloc(size ? size : 1);
    assert(out);
    if (size) assert(zip_extract(zip, index, out, size - 1) != ESP_OK);
    err = zip_extract(zip, index, out, size);
    if (!strcmp(argv[1], "reject-extract")) assert(err != ESP_OK);
    else {
        assert(err == ESP_OK && argc == 5);
        FILE* expected = fopen(argv[4], "rb");
        assert(expected);
        for (size_t i = 0; i < size; ++i) assert(fgetc(expected) == out[i]);
        assert(fgetc(expected) == EOF);
        fclose(expected);
        if (!size) assert(zip_extract(zip, index, NULL, 0) == ESP_OK);
    }
    free(out);
    zip_close(zip);
    zip_close(NULL);
}
