#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
"""中文：校验真实 TTF 与损坏目录，确认仓库部署字体未修改。
English: Validate real TTFs and malformed directories; verify the bundled font is unmodified.
"""
from pathlib import Path
import hashlib
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    font = ROOT / 'sdcard/fonts/ChillDuanSansVF.ttf'
    assert hashlib.sha256(font.read_bytes()).hexdigest() == '966286fc44907d4123528e1efb98c08e51fff013cd083140fdad1bf6b593555c'
    assert 'SIL OPEN FONT LICENSE Version 1.1' in (font.parent / 'OFL.txt').read_text(encoding='utf-8')
    source = (ROOT / 'main/assets/builtin.ttf').read_bytes()
    count = struct.unpack_from('>H', source, 4)[0]
    tables = {}
    for i in range(count):
        pos = 12 + i * 16
        tag, _, offset, length = struct.unpack_from('>4sIII', source, pos)
        tables[tag] = (pos, offset, length)
    with tempfile.TemporaryDirectory(prefix='transfer-font-') as tmp:
        work = Path(tmp)
        c = work / 'test.c'
        c.write_text('#include "transfer_font.h"\n#include <assert.h>\nint main(int argc,char **argv){assert(argc==3);assert(valid_ttf(argv[2])==!strcmp(argv[1],"good"));}\n')
        exe = work / 'test'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                        '-I' + str(ROOT / 'components/read_pico_transfer'), str(c), '-o', str(exe)], check=True)
        for path in (font, ROOT / 'main/assets/builtin.ttf'):
            subprocess.run([str(exe), 'good', str(path)], check=True)
        cases = [b'', b'not a font', b'OTTO' + source[4:], b'ttcf' + source[4:], b'wOFF' + source[4:], source[:-100]]

        def corrupt(offset, fmt, value):
            data = bytearray(source)
            struct.pack_into(fmt, data, offset, value)
            cases.append(data)

        corrupt(4, '>H', 129)
        corrupt(tables[b'glyf'][0], '>4s', b'xxxx')
        corrupt(tables[b'glyf'][0], '>4s', b'head')
        corrupt(tables[b'glyf'][0] + 8, '>I', len(source) + 1)
        corrupt(tables[b'glyf'][0] + 12, '>I', 0xffffffff)
        corrupt(tables[b'head'][0] + 12, '>I', 1)
        corrupt(tables[b'head'][1] + 12, '>I', 0)
        corrupt(tables[b'head'][1] + 18, '>H', 0)
        corrupt(tables[b'head'][1] + 50, '>H', 2)
        corrupt(tables[b'maxp'][1] + 4, '>H', 0)
        corrupt(tables[b'hhea'][1] + 34, '>H', 0xffff)
        corrupt(tables[b'hmtx'][0] + 12, '>I', 1)
        corrupt(tables[b'loca'][0] + 12, '>I', 1)
        corrupt(tables[b'loca'][1], '>I', 0xffffffff)
        corrupt(tables[b'cmap'][1] + 2, '>H', 0xffff)
        for index, content in enumerate(cases):
            path = work / f'bad{index}.ttf'
            path.write_bytes(content)
            subprocess.run([str(exe), 'bad', str(path)], check=True)
        oversized = work / 'oversized.ttf'
        with oversized.open('wb') as f:
            f.truncate(32 * 1024 * 1024 + 1)
        subprocess.run([str(exe), 'bad', str(oversized)], check=True)
        print(f'TTF preflight: 2 real fonts, {len(cases) + 1} malformed/oversized fonts, deployment SHA-256 and license passed')


if __name__ == '__main__':
    main()
