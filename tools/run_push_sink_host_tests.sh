#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
# 手机 BLE 推流纯核回归：帧编解码 + 会话状态机。/ Host regression for the phone BLE push pure core: frame codec + session machine.
set -euo pipefail
cd "$(dirname "$0")/.."
# 纯核不依赖 ESP-IDF：-std=c11 严格模式不暴露 strnlen，宿主显式开 POSIX 层级。
# The pure core has no ESP-IDF dependency: -std=c11 hides strnlen, so the host opts into POSIX.
gcc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -Icomponents/push_sink/include \
    tools/push_sink_host_test.c \
    components/push_sink/src/push_proto.c \
    components/push_sink/src/push_session.c \
    -o /tmp/push_sink_test
/tmp/push_sink_test
