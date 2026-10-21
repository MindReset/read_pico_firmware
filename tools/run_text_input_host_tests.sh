#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# 中文：测试编辑、光标及各输入页面的真实回调。/ English: Test editing, carets and actual page callbacks.
set -euo pipefail
mkdir -p build/book-tests
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Itools/ui_gesture_stubs -Imain/ui -Imain/font \
    tools/text_input_host_test.c main/ui/ui_text_edit.c main/ui/ui_text_input.c \
    -o build/book-tests/text-input
build/book-tests/text-input
cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -Itools/ui_gesture_stubs -Itools/html_text_stubs -Imain/ui -Imain/font \
    -Icomponents/read_pico_search/include tools/keyboard_host_test.c \
    main/ui/ui_ime.c main/ui/ui_text_edit.c components/read_pico_search/read_pico_search.c \
    -o build/book-tests/keyboard
build/book-tests/keyboard
python3 tools/input_editors_host_test.py
cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -Itools/ui_gesture_stubs -Itools/html_text_stubs -Imain/ui -Imain/font \
    -Icomponents/read_pico_search/include tools/keyboard_refresh_host_test.c \
    main/ui/ui_ime.c main/ui/ui_text_edit.c main/ui/ui_text_input.c \
    components/read_pico_search/read_pico_search.c -o build/book-tests/keyboard-refresh
build/book-tests/keyboard-refresh
