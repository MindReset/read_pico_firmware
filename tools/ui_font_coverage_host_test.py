"""Check real reader-setting and keyboard labels against the shipped UI font cmap.

SPDX-License-Identifier: Apache-2.0
中文：说明里缺一个字就会整行回退到阅读字体；直接检查固件内建字库。
English: One missing character sends the whole label to the reader face; inspect
the real embedded font instead of a mocked has-text result. Uses only stdlib.
"""
from pathlib import Path
import re
import ast
import struct

root = Path(__file__).resolve().parents[1]
font = (root / "main/assets/builtin.ttf").read_bytes()


def u16(offset):
    return struct.unpack_from(">H", font, offset)[0]


def u32(offset):
    return struct.unpack_from(">I", font, offset)[0]


tables = {}
for i in range(u16(4)):
    record = 12 + i * 16
    tables[font[record:record + 4]] = u32(record + 8)
base = tables[b"cmap"]
subtables = []
for i in range(u16(base + 2)):
    record = base + 4 + i * 8
    platform, encoding = u16(record), u16(record + 2)
    if platform == 0 or (platform == 3 and encoding in (1, 10)):
        offset = base + u32(record + 4)
        if u16(offset) in (4, 12):
            subtables.append(offset)
assert subtables, "No Unicode cmap in the embedded font"

# 系统字形同时来自TTF和内建汉字补充，和ui_font_has_text保持一致。
# Match ui_font_has_text: system glyphs come from both the TTF and the embedded Han supplement.
hanzi = (root / "main/assets/ui-hanzi.bin").read_bytes()
magic, count, px, block = struct.unpack_from("<4sIII", hanzi)
assert magic == b"PIF1" and count <= 6763 and px == 24 and block == 32
assert 16 + count * 2 + ((count + 31) // 32 + 1) * 4 <= len(hanzi)
hanzi_codepoints = set(struct.unpack_from("<" + "H" * count, hanzi, 16))


def has_glyph(codepoint):
    if codepoint in hanzi_codepoints:
        return True
    for offset in subtables:
        if u16(offset) == 12:
            for i in range(u32(offset + 12)):
                group = offset + 16 + i * 12
                first, last = u32(group), u32(group + 4)
                if first <= codepoint <= last:
                    return bool(u32(group + 8) + codepoint - first)
        elif codepoint <= 0xffff:
            count = u16(offset + 6) // 2
            ends = offset + 14
            starts = ends + 2 * count + 2
            deltas = starts + 2 * count
            ranges = deltas + 2 * count
            for i in range(count):
                first, last = u16(starts + i * 2), u16(ends + i * 2)
                if first <= codepoint <= last:
                    delta, distance = u16(deltas + i * 2), u16(ranges + i * 2)
                    if not distance:
                        return bool((codepoint + delta) & 0xffff)
                    glyph = u16(ranges + i * 2 + distance + 2 * (codepoint - first))
                    return bool(glyph and ((glyph + delta) & 0xffff))
    return False


source = (root / "main/apps/app_book.c").read_text(encoding="utf-8")
first = source.index("static void draw_reading_toggle(")
last = source.index("static void draw_font_picker(", first)
# Covers the reading sheet, its explanations, and the horizontal/vertical page
# diagrams. Dynamic book text and user font names intentionally use other paths.
section = source[first:last]
labels = re.findall(r'"([^"\n]*)"', section)
# 本地快刷细点阵说明也必须使用完整系统字形。/ The local fine-dot mode explanation also needs complete system glyphs.
settings = (root / "main/apps/app_device_settings.c").read_text()
first = settings.index("if (s_page == SETTINGS_MAIN_REFRESH) {")
last = settings.index("if (s_page == SETTINGS_SYSTEM_CONTRAST) {", first)
labels += re.findall(r'"([^"\n]*)"', settings[first:last])
# 共用键盘必须始终使用系统字形；退格图案由线段绘制。
# Shared keys must always use system glyphs; backspace is drawn with lines.
keyboard=(root/'main/ui/ui_keyboard.c').read_text()
labels += [ast.literal_eval(literal) for literal in re.findall(r'"(?:\\.|[^"\\])*"', keyboard)]
labels += ["抗锯齿", "左上一页 · 中工具栏 · 右下一页", "左回首页 · 中全屏 · 右工具栏", "缩进微调", "%+d px · 复位", "−"]
missing = {}
for label in labels:
    absent = "".join(dict.fromkeys(ch for ch in label if ch != "⌫" and ord(ch) >= 32 and not has_glyph(ord(ch))))
    if absent:
        missing[label] = absent
assert not missing, f"System UI labels fall back to the reading font: {missing}"
print(f"PASS: {len(labels)} real reader-setting and keyboard labels have complete system UI glyphs")
