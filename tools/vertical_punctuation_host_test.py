#!/usr/bin/env python3
"""竖排旋转判定回归：短横类符号必须立起来，正立字形不许被转。
Vertical rotation rule regression: dash-like marks must stand up, upright forms must not turn.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FONT = ROOT / 'main/font/ttf_font.c'


def function(path, name):
    source = path.read_text(encoding='utf-8')
    match = re.search(r'^(?:static )?[^\n]+\b' + name + r'\([^;{}]*\)\s*\{', source, re.M)
    assert match, name
    start, pos, depth = match.start(), match.end(), 1
    # 去掉注释但保留字节位置，再在字符串外数括号。/ Drop comments keeping positions, then count braces outside strings.
    cleaned = re.sub(r'/\*.*?\*/|//[^\n]*', lambda m: ' ' * len(m[0]), source, flags=re.S)
    quote, escaped = None, False
    while depth:
        c = cleaned[pos]
        if quote:
            if escaped: escaped = False
            elif c == '\\': escaped = True
            elif c == quote: quote = None
        elif c in '\"\'': quote = c
        elif c == '{': depth += 1
        elif c == '}': depth -= 1
        pos += 1
    return source[start:pos]


unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
''' + function(FONT, 'vertical_rotated_cp') + r'''
int main(void) {
    // 横排方向是"一道横线"的符号：连字符、短破折号、破折号、横杠、负号、长音符与半角长音符、
    // 小号破折号/连字符。竖排里它们必须转 90°，否则读者看到的是一道跟正文走向垂直的短线。
    // Marks that read as a horizontal stroke: hyphens, dashes, the horizontal bar, the minus sign,
    // the prolonged sound marks and the small dash/hyphen forms. They must turn 90° vertically.
    const uint32_t turns[] = {
        0x002d, 0x2010, 0x2011, 0x2012, 0x2013, 0x2014, 0x2015,
        0x2212, 0x2e3a, 0x2e3b, 0x30fc, 0xfe58, 0xfe63, 0xff0d, 0xff70,
        0x0041, 0x0031, 0x300c, 0x3014, 0xff08,
    };
    for (unsigned i = 0; i < sizeof(turns) / sizeof(turns[0]); ++i) {
        if (!vertical_rotated_cp(turns[i])) {
            printf("FAIL: U+%04X should turn in vertical setting\n", turns[i]);
            return 1;
        }
    }
    // 反例：汉字、假名、句读、竖排呈现形与已经竖着的破折号都保持正立；空白与 DEL 不是字形。
    // Counterexamples: ideographs, kana, CJK stops, vertical presentation forms and the already
    // vertical dashes stay upright; space and DEL are not glyphs at all.
    const uint32_t upright[] = {
        0x0020, 0x007f, 0x3001, 0x3002, 0x4e2d, 0x3042, 0xfe31, 0xfe32, 0xfe41,
    };
    for (unsigned i = 0; i < sizeof(upright) / sizeof(upright[0]); ++i) {
        if (vertical_rotated_cp(upright[i])) {
            printf("FAIL: U+%04X should stay upright in vertical setting\n", upright[i]);
            return 1;
        }
    }
    puts("PASS: dash-like marks turn in vertical setting, upright forms stay upright");
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='pico-vertical-test-') as directory:
    work = Path(directory)
    (work / 'test.c').write_text(unit, encoding='utf-8')
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    str(work / 'test.c'), '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)
