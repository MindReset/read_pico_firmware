#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
# 在仓库根运行管理功能回归；网页脚本另用 Node 验证。
# Run management regressions from the repository root; validate web interactions separately with Node.
set -euo pipefail
bash tools/run_book_host_tests.sh
python3 tools/test_book_images.py
python3 tools/test_font_metrics.py
python3 tools/book_ui_host_test.py
python3 tools/test_search.py
flags=(-std=gnu11 -Wall -Wextra -Werror -g -fsanitize=address,undefined)
gcc "${flags[@]}" -Icomponents/read_pico_search/include tools/transfer_host_test.c components/read_pico_search/read_pico_search.c -o build/book-tests/transfer
build/book-tests/transfer
python3 tools/test_transfer_font.py
gcc "${flags[@]}" -Imanaged_components/espressif__cjson/cJSON tools/transfer_wifi_host_test.c managed_components/espressif__cjson/cJSON/cJSON.c -lm -o build/book-tests/transfer-wifi
build/book-tests/transfer-wifi
gcc "${flags[@]}" -Itools/transfer_ui_stubs -Icomponents/read_pico_transfer/include tools/transfer_ui_host_test.c -o build/book-tests/transfer-ui
build/book-tests/transfer-ui
bash tools/run_app_loop_host_tests.sh
bash tools/run_display_host_test.sh
python3 tools/test_lcd_frame_lifecycle.py
python3 tools/test_sd_media_guard.py
