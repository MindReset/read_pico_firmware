#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
# 在仓库根目录运行；产物仅在build。/ Run from the repository root; artifacts stay in build.
# EPUB主机解压替身需要zlib开发包。/ The EPUB host inflate shim requires zlib development headers.
set -euo pipefail
mkdir -p build/book-tests
python tools/gen_book_fixtures.py
flags=(-std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-omit-frame-pointer -Imain/book)
gcc "${flags[@]}" -Itools/book_epub_stubs -Itools/zip_host_stubs tools/book_source_host_test.c main/book/book_source.c main/book/book_txt.c main/book/gbk.c main/book/book_epub.c main/book/zip_reader.c main/book/html_text.c main/book/book_image.c main/book/vendor/tjpgd.c -lz -o build/book-tests/source
gcc "${flags[@]}" -Itools/book_layout_stubs -Itools/book_source_host_stubs tools/book_layout_host_test.c main/book/book_layout.c -o build/book-tests/layout
gcc "${flags[@]}" -Itools/book_storage_stubs tools/book_storage_host_test.c main/book/book_progress.c -o build/book-tests/progress
gcc "${flags[@]}" -Itools/book_storage_stubs -Icomponents/read_pico/include tools/book_store_host_test.c -o build/book-tests/store
gcc "${flags[@]}" tools/book_policy_host_test.c -o build/book-tests/policy
build/book-tests/source build/book-fixtures/books/*.txt build/book-fixtures/books/*.epub
build/book-tests/layout
build/book-tests/progress
build/book-tests/store
build/book-tests/policy
gcc "${flags[@]}" -Itools/ui_gesture_stubs -Imain/ui tools/ui_gesture_host_test.c main/ui/ui_gesture.c -o build/book-tests/gesture
gcc "${flags[@]}" -ffunction-sections -fdata-sections -Wl,--gc-sections -Itools/ui_gesture_stubs -Imain/ui tools/ui_kit_host_test.c main/ui/ui_kit.c -o build/book-tests/ui_kit
build/book-tests/gesture
build/book-tests/ui_kit
gcc "${flags[@]}" -Itools/html_text_stubs tools/html_text_host_test.c main/book/html_text.c -o build/book-tests/html
build/book-tests/html
