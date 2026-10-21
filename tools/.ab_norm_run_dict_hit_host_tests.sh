#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# 点词命中回环：画一页 → 记住每个词实际画在哪 → 点它 → 要求命中测试返回同一个词。
# 插图用例额外钉死「图下留半个行高」，并覆盖「图高恰好等于页高」这个 rc85/rc89 间距算法的
# 唯一分歧点。/ Tap-to-lookup round trip: draw a page, remember where each word was painted, tap
# it and require the hit test to return the same word. The image cases also pin the half-line gap
# below an image and cover the one point where the rc85 and rc89 gap formulas differ (image
# height exactly equal to the page height).
# 在仓库根目录运行；产物仅在 build。/ Run from the repository root; artifacts stay in build.
set -uo pipefail
mkdir -p build/book-tests
flags=(-std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-omit-frame-pointer)
gcc "${flags[@]}" -Imain/book -Itools/book_layout_stubs -Itools/dict_hit_stubs \
    tools/dict_hit_test.c main/book/book_layout.c -o build/book-tests/dict_hit
build/book-tests/dict_hit

# 孪生块的静态守门：两个循环必须共用那组排版算术，不许再手抄。
# Static guard for the twin loops: both must share the layout arithmetic, no hand copies.
python3 tools/book_layout_twin_host_test.py

# 生词本落盘：转义、句子边界、去重、索引降级、体积上限。
# Vocabulary log writes: escaping, sentence bounds, de-duplication, index degradation, file cap.
rm -rf /tmp/rp-vocab-test
gcc "${flags[@]}" -Imain/dict -Imain/book -Itools/vocab_log_stubs -Itools/dict_hit_stubs \
    tools/vocab_log_host_test.c -o build/book-tests/vocab_log
build/book-tests/vocab_log

# 生词本接线的静态守门：跨文件的四处调用不许在 rebase 中悄悄丢掉。
# Static guard for the vocabulary-book wiring: the four cross-file calls must not
# quietly disappear during a rebase.
python3 tools/vocab_wiring_host_test.py

# 损坏词典的打开校验 + 长按带来的首尾标点剥离；真造一份 RPD1 落盘再开。
# Open validation for damaged dictionaries and punctuation stripping, against a real file.
gcc "${flags[@]}" -Imain/dict -Itools/dict_lookup_stubs -Itools/vocab_log_stubs -Itools/dict_hit_stubs \
    tools/dict_robust_host_test.c -o build/book-tests/dict_robust
build/book-tests/dict_robust

# 镜像守门：dict_probe.py 必须与固件做同样的校验与归一化。
# Mirror guard: the probe must validate and normalize exactly like the firmware.
python3 tools/dict_mirror_host_test.py
