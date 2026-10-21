#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# 配网/传书界面回归。上游把 transfer_ui 挂在 run_book_management_tests.sh 里，但那个脚本会更早地
# 被 EPUB 用例的 ASan 泄漏中止，所以这个测试实际上从来没跑起来过。这里单独跑它。
# Provisioning/transfer UI regression. Upstream hangs transfer_ui off
# run_book_management_tests.sh, but that script aborts earlier on the EPUB ASan leak, so this
# test never actually runs. Run it here on its own.
#
# 唯一放宽的一条告警：上游测试文件 transfer_ui_host_test.c 第 220 行自己有个
# -Wmisleading-indentation（for 与后续 assert 写在同一行），在 -Werror 下直接编译失败。
# 那是上游的问题，不是本补丁的；这里只降级这一条，其它告警仍然致命。
# The single relaxed warning: upstream's transfer_ui_host_test.c has its own
# -Wmisleading-indentation at line 220 (a for and a following assert on one line), which fails
# under -Werror. That is upstream's, not this patch's; only that one warning is downgraded here.
# 在仓库根目录运行；产物仅在build。/ Run from the repository root; artifacts stay in build.
set -euo pipefail
mkdir -p build/book-tests
flags=(-std=gnu11 -Wall -Wextra -Werror -Wno-deprecated-declarations -Wno-error=misleading-indentation -g -fsanitize=address,undefined)
gcc "${flags[@]}" -Itools/transfer_ui_stubs -Icomponents/read_pico_transfer/include \
    -Icomponents/read_pico_search/include -Imain/ui \
    tools/transfer_ui_host_test.c main/ui/ui_text_edit.c main/ui/ui_text_input.c \
    main/ui/ui_keyboard.c main/ui/ui_ime.c components/read_pico_search/read_pico_search.c \
    -o build/book-tests/transfer-ui
build/book-tests/transfer-ui
