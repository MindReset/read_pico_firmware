#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
# 二维码生产编码/绘制回归。/ Production QR encoding/drawing regression.
set -euo pipefail
mkdir -p build
qr=managed_components/espressif__qrcode
gcc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Itools/wifi_qr_stubs -Imain/ui -I"$qr/include" -I"$qr" tools/wifi_qr_host_test.c main/ui/ui_wifi_qr.c "$qr/esp_qrcode_main.c" "$qr/esp_qrcode_wrapper.c" "$qr/qrcodegen.c" -o /tmp/wifi_qr_test
/tmp/wifi_qr_test
