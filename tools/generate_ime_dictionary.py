#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# 从已有官方拼音表生成反向索引；不改变搜索字表。/ Index the existing official table without changing search readings.
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def values(source, name):
    match = re.search(rf"\b{name}\[\] = \{{(.*?)\}};", source, re.S)
    return [int(n) for n in re.findall(r"\d+", match[1])]


def array(name, kind, data):
    rows = [f"static const {kind} {name}[] = {{"]
    rows += ["    " + ", ".join(map(str, data[i:i + 16])) + "," for i in range(0, len(data), 16)]
    return "\n".join(rows + ["};", ""])


def main():
    component = ROOT / "components/read_pico_search"
    source = (component / "search_table.h").read_text()
    cps, groups, offsets, readings, syllables = [values(source, n) for n in (
        "search_codepoints", "search_groups", "search_group_offsets", "search_readings", "search_syllable_offsets")]
    common = "的一是不了人我在有他这中大来上国个到说们为子和你地出道也时年得就那要下以生会自着去之过家学对可她里后小心多天而能好都然没日于起还发成只如事把无明看本面知现所同手时方女新前想最太见被高用开么将行长身三间加由其从两情进已又些点样意力第话走实定才爱亲当问比很世书水名作每海边信安静远山慢读清晨章客花月风雨空云春夏秋冬你我他她它孩文字篇页故事"
    priority = {ord(ch): i for i, ch in reversed(list(enumerate(common)))}
    # GB2312 一级字排在常用字后，二级字再后；其余基本汉字仍可完整翻页。
    # Rank GB2312 level one/two after common glyphs; retain every other basic Han character.
    level = {}
    for hi in range(0xb0, 0xf8):
        for lo in range(0xa1, 0xff):
            try:
                level[ord(bytes([hi, lo]).decode("gb2312"))] = 0 if hi < 0xd8 else 1
            except UnicodeDecodeError:
                pass
    reverse = [[] for _ in syllables]
    for cp, group in zip(cps, groups):
        if 0x4e00 <= cp <= 0x9fff:
            for index in readings[offsets[group]:offsets[group + 1]]:
                reverse[index].append(cp)
    flat, starts = [], [0]
    for chars in reverse:
        chars.sort(key=lambda cp: (0, priority[cp]) if cp in priority else (1 + level.get(cp, 2), cp))
        flat += chars
        starts.append(len(flat))
    header = """/* SPDX-License-Identifier: MIT
 * 由 tools/generate_ime_dictionary.py 生成，禁止手改。/ Generated; do not edit by hand.
 * 来自 search_table.h 的 pypinyin 0.55.0；许可见 LICENSE.pypinyin。/ Same pinned pypinyin data and license.
 */
"""
    header += array("ime_character_offsets", "uint32_t", starts)
    header += array("ime_characters", "uint16_t", flat)
    (component / "search_candidates.h").write_text(header)
    print(f"IME reverse index: {len(set(flat))} characters, {len(flat)} readings, {len(flat)*2 + len(starts)*4} bytes")


if __name__ == "__main__":
    main()
