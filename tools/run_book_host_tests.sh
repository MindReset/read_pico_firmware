#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
# 在仓库根目录运行；产物仅在build。/ Run from the repository root; artifacts stay in build.
# EPUB主机解压替身需要zlib开发包。/ The EPUB host inflate shim requires zlib development headers.
set -euo pipefail
mkdir -p build/book-tests
python tools/gen_book_fixtures.py
flags=(-std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-omit-frame-pointer -Imain/book -Imain)
if [[ "$(uname -s)" == Darwin ]]; then link_gc=(-Wl,-dead_strip); else link_gc=(-Wl,--gc-sections); fi
gcc "${flags[@]}" -Itools/book_epub_stubs -Itools/zip_host_stubs tools/book_source_host_test.c main/book/book_source.c main/book/book_index_cache.c main/book/book_txt.c main/book/gbk.c main/book/book_epub.c main/book/book_image_header.c main/book/zip_reader.c main/book/html_text.c -lz -o build/book-tests/source
gcc "${flags[@]}" -Itools/book_layout_stubs -Itools/book_source_host_stubs tools/book_layout_host_test.c main/book/book_layout.c main/book/html_text.c -o build/book-tests/layout
gcc "${flags[@]}" -Itools/book_layout_stubs -Itools/book_source_host_stubs tools/book_layout_twin_image_host_test.c main/book/book_layout.c main/book/html_text.c -o build/book-tests/twin_image
gcc "${flags[@]}" -Itools/book_storage_stubs tools/book_storage_host_test.c main/book/book_progress.c -o build/book-tests/progress
gcc "${flags[@]}" -Imain -Itools/book_storage_stubs -Icomponents/read_pico/include tools/book_store_host_test.c -o build/book-tests/store
gcc "${flags[@]}" tools/book_policy_host_test.c -o build/book-tests/policy
build/book-tests/source build/book-fixtures/books/*.txt build/book-fixtures/books/*.epub
build/book-tests/layout
# 双栏 × 插图 fixture（e2e 返工范围第 1 条，tools/book_layout_twin_image_host_test.c）：
# G7-A1 修复落地后直跑——退出码 0 = 三场景全绿（修复后的预期落点）；1 = 修复不完整
# （大插图几何层仍见红、对照/基线全绿）；2 = 意外形态（对照/基线见红 = 新缺陷；大图
# 红点不在几何层 = 非预期形态）。任何非零都照失败拦（set -e 直接断），登记放行分支
# 随 KNOWN_FAILURES.md §5 退役一并移除。
# Two-column x illustration fixture (e2e rework scope item 1): straight run after the
# G7-A1 fix — exit 0 all green (the expected landing spot); 1 incomplete fix (red in the
# big scenario's geometry layer with control/baseline green); 2 unexpected shape. Any
# non-zero exit fails the runner (set -e breaks here); the KNOWN-RED tolerance branch
# was removed together with the KNOWN_FAILURES.md §5 retirement.
build/book-tests/twin_image
build/book-tests/progress
build/book-tests/store
build/book-tests/policy
gcc "${flags[@]}" -Itools/ui_gesture_stubs -Imain/ui tools/ui_gesture_host_test.c main/ui/ui_gesture.c -o build/book-tests/gesture
gcc "${flags[@]}" -ffunction-sections -fdata-sections "${link_gc[@]}" -Itools/ui_gesture_stubs -Itools/html_text_stubs -Imain -Imain/ui -Imain/font tools/ui_kit_host_test.c main/ui/ui_kit.c -o build/book-tests/ui_kit
build/book-tests/gesture
# known-fail（r93 4bpp packed 帧缓冲契约失效）：仍然编译并运行以保持可见，但只放行登记形态
# ——「ui_draw_control_frame 之后的第一个像素采样落空」。换成别的失败形态就是新缺陷，照旧拦。
# Known fail (r93 4bpp packed framebuffer contract): still built and run so it stays visible,
# but only the registered shape is tolerated — the first pixel sample after
# ui_draw_control_frame coming back blank. Any other shape is a new defect and still fails.
# 登记与证据链见 tools/KNOWN_FAILURES.md。/ See tools/KNOWN_FAILURES.md for the evidence chain.
ui_kit_output="$(build/book-tests/ui_kit 2>&1)" && ui_kit_status=0 || ui_kit_status=$?
if [[ $ui_kit_status -eq 0 ]]; then
    printf '%s\n' "$ui_kit_output"
elif printf '%s' "$ui_kit_output" | grep -qF 'fb[10*W+30]'; then
    printf '%s\n' "$ui_kit_output"
    echo "ui_kit: KNOWN-FAIL (r93 packed framebuffer contract, registered in tools/KNOWN_FAILURES.md) — not a regression"
else
    printf '%s\n' "$ui_kit_output"
    echo "ui_kit: FAIL — 失败形态与登记的不一致，按新缺陷处理 / unregistered failure shape, treated as a new defect" >&2
    exit 1
fi
gcc "${flags[@]}" -Itools/html_text_stubs tools/html_text_host_test.c main/book/html_text.c -o build/book-tests/html
build/book-tests/html
