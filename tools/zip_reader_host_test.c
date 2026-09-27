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
