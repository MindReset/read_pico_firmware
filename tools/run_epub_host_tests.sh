#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
# 在已有 zlib 开发包的主机或测试容器运行；本脚本不安装包。
# Run on a host or test container with zlib development headers; this script installs no packages.
# 可复用 read-pico-epub-host:local 镜像，仓库绑定到 /project 后用 bash 执行本文件。
# Reuse read-pico-epub-host:local with the repository mounted at /project and run this file with bash.
# 冻结：仅验证解析与平台替身，不替代 ROM 或真机验收。
# Frozen: Test parsing and platform shims only, never substitute for ROM or device acceptance.
set -euo pipefail
export UBSAN_OPTIONS=halt_on_error=1
cd "$(dirname "$0")/.."
mkdir -p build/book-tests

python tools/gen_book_fixtures.py
python tools/test_zip_reader.py
python tools/test_book_epub.py

flags=(-std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-omit-frame-pointer)
includes=(-Itools/book_epub_stubs -Itools/zip_host_stubs -Imain/book)
cc "${flags[@]}" -Itools/html_text_stubs -Imain/book tools/html_text_host_test.c main/book/html_text.c -o build/book-tests/epub-html
build/book-tests/epub-html

# 同时链接 TXT 与 EPUB，防止分派变更破坏旧书源。
# Link TXT and EPUB together so dispatch changes retain the existing source behavior.
cc "${flags[@]}" "${includes[@]}" tools/book_source_host_test.c \
    main/book/book_source.c main/book/book_txt.c main/book/gbk.c \
    main/book/book_epub.c main/book/zip_reader.c main/book/html_text.c main/book/book_image.c main/book/vendor/tjpgd.c \
    -lz -o build/book-tests/epub-source
shopt -s nullglob
fixtures=(build/book-fixtures/books/*.txt build/book-fixtures/books/*.epub)
build/book-tests/epub-source "${fixtures[@]}"
