#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
# 编译真实显示出口，只替换硬件边界。/ Compile the production display path, replacing hardware boundaries only.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/book-tests
link_gc=(-Wl,--gc-sections)
if [[ "$(uname -s)" == "Darwin" ]]; then link_gc=(-Wl,-dead_strip); fi
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -ffunction-sections -fdata-sections "${link_gc[@]}" \
    -Itools/display_host_stubs -Icomponents/continuous_du/include -Icomponents/e0470_page_turn/include -Imain -Imain/app \
    tools/display_host_test.c main/display.c -o build/book-tests/display
build/book-tests/display
