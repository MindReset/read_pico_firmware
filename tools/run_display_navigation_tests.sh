#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# 中文：验证主页面黑白/灰阶分步、横条局部移动与底栏边界。/ English: Verify staged black/white and gray main updates, locally moving markers and navigation bounds.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/book-tests
link_gc=(-Wl,--gc-sections)
if [[ "$(uname -s)" == "Darwin" ]]; then link_gc=(-Wl,-dead_strip); fi
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -ffunction-sections -fdata-sections "${link_gc[@]}" \
    -Itools/display_host_stubs -Icomponents/continuous_du/include -Icomponents/e0470_page_turn/include -Imain -Imain/app \
    tools/display_navigation_host_test.c main/display.c main/ui/ui_image_dither.c -o build/book-tests/display-navigation
build/book-tests/display-navigation
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -Imain -Imain/app \
    -Itools/waveform_host_stubs -Icomponents/epdiy/include -Icomponents/epdiy/src \
    -Icomponents/e0470_epaper_waveform/include -Icomponents/e0470_epaper_waveform/waveforms \
    tools/navigation_waveform_host_test.c \
    components/e0470_epaper_waveform/e0470_epaper_waveform.c \
    components/e0470_epaper_waveform/e0470_waveform_trim.c \
    main/display_main_waveform.c main/ui/ui_image_dither.c -o build/book-tests/navigation-waveform
build/book-tests/navigation-waveform

cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -Imain -Itools/waveform_host_stubs -Icomponents/epdiy/include -Icomponents/epdiy/src \
    -Icomponents/e0470_epaper_waveform/include -Icomponents/e0470_epaper_waveform/waveforms \
    tools/reader_waveform_host_test.c main/display_reader_waveform.c \
    components/e0470_epaper_waveform/e0470_epaper_waveform.c \
    components/e0470_epaper_waveform/e0470_waveform_trim.c -o build/book-tests/reader-waveform
build/book-tests/reader-waveform
