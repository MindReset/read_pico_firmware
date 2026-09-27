#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
# 中文：用 Python GBK codec 生成紧凑表。/ English: Generate a compact table using Python's GBK codec.
# 冻结：生成产物勿手改。/ Frozen: Do not edit generated output by hand.
from pathlib import Path

out = Path(__file__).resolve().parents[1] / 'main/book/gbk_table.h'
values = []
for lead in range(0x81, 0xff):
    for trail in list(range(0x40, 0x7f)) + list(range(0x80, 0xff)):
        try:
            values.append(ord(bytes([lead, trail]).decode('gbk')))
        except UnicodeDecodeError:
            values.append(0xfffd)
assert len(values) * 2 <= 48 * 1024
header = '''/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：生成的 GBK 表，运行 tools/gen_gbk_table.py。
 * English: Generated GBK table; run tools/gen_gbk_table.py.
 * 冻结：勿手改。/ Frozen: Do not edit by hand.
 */
#pragma once
#include <stdint.h>
static const uint16_t s_gbk_table[23940] = {
'''
out.write_text(header + ''.join('    ' + ','.join(f'0x{x:04x}' for x in values[i:i+16]) + ',\n' for i in range(0, len(values), 16)) + '};\n', encoding='utf-8')
print(f'{out}: {len(values) * 2} table bytes')
