#!/usr/bin/env python3
"""Scan UI strings and subset ChillDuanSans VF into main/assets/builtin.ttf."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CHARSET = ROOT / "main/font/charset.txt"
OUT = ROOT / "main/assets/builtin.ttf"
SCAN_DIRS = [
    ROOT / "main/ui",
    ROOT / "main/app",
    ROOT / "main/apps",
    ROOT / "main/factory",
    ROOT / "main/assets",
    ROOT / "main/app_main.c",
    ROOT / "main/display.c",
    ROOT / "main/sleep.c",
    ROOT / "main/settings.c",
    ROOT / "components/sy7636a",
    ROOT / "components/sc7a20h",
    ROOT / "components/read_pico",
    ROOT / "components/read_pico_pmu",
]
TEXT_SUFFIXES = {".md", ".txt"}
# 阅读正文补齐后 VF 子集会超过旧的 400KB；标题还要 wght=700，不能实例化成 Regular。
MAX_BYTES = 700 * 1024
STRING_RE = re.compile(r'"(?:\\.|[^"\\])*"')


def ascii_printable() -> str:
    return "".join(chr(i) for i in range(0x20, 0x7F))


def load_preset(path: Path) -> str:
    chars: list[str] = []
    if not path.is_file():
        return ""
    for line in path.read_text(encoding="utf-8").splitlines():
        s = line.strip()
        if not s or s.startswith("#"):
            continue
        chars.append(s)
    return "".join(chars)


def unescape(literal: str) -> str:
    body = literal[1:-1]
    return (
        body.replace("\\\\", "\0")
        .replace("\\n", "\n")
        .replace("\\t", "\t")
        .replace("\\\"", '"')
        .replace("\0", "\\")
    )


def scan_sources() -> str:
    texts: list[str] = []
    files: list[Path] = []
    for item in SCAN_DIRS:
        if item.is_file():
            files.append(item)
        elif item.is_dir():
            files.extend(item.rglob("*.c"))
            files.extend(item.rglob("*.h"))
            files.extend(item.rglob("*.md"))
            files.extend(item.rglob("*.txt"))
    for path in files:
        data = path.read_text(encoding="utf-8", errors="ignore")
        if path.suffix in TEXT_SUFFIXES:
            texts.append(data)
            continue
        for match in STRING_RE.finditer(data):
            texts.append(unescape(match.group(0)))
    return "".join(texts)


def unique_text(*parts: str) -> str:
    seen: set[str] = set()
    out: list[str] = []
    for part in parts:
        for ch in part:
            if ch in seen or ch in "\n\r\t":
                continue
            seen.add(ch)
            out.append(ch)
    return "".join(sorted(out, key=lambda c: ord(c)))


def subset_variable(src: Path, text: str, dest: Path) -> int:
    from fontTools.subset import Subsetter, Options
    from fontTools.ttLib import TTFont

    font = TTFont(src)
    options = Options()
    options.layout_features = ["*"]
    options.notdef_outline = True
    options.recommended_glyphs = True
    options.name_IDs = ["*"]
    options.name_legacy = True
    options.glyph_names = False
    options.ignore_missing_glyphs = True
    subsetter = Subsetter(options=options)
    subsetter.populate(text=text)
    subsetter.subset(font)
    dest.parent.mkdir(parents=True, exist_ok=True)
    font.save(dest)
    font.close()
    return dest.stat().st_size


def subset_regular(src: Path, text: str, dest: Path) -> int:
    from fontTools.subset import Subsetter, Options
    from fontTools.ttLib import TTFont
    from fontTools.varLib.instancer import instantiateVariableFont

    vf = TTFont(src)
    if "fvar" in vf:
        inst = instantiateVariableFont(vf, {"wght": 400}, inplace=False)
        vf.close()
        vf = inst
    options = Options()
    options.layout_features = ["*"]
    options.notdef_outline = True
    options.recommended_glyphs = True
    options.ignore_missing_glyphs = True
    subsetter = Subsetter(options=options)
    subsetter.populate(text=text)
    subsetter.subset(vf)
    dest.parent.mkdir(parents=True, exist_ok=True)
    vf.save(dest)
    vf.close()
    return dest.stat().st_size


def default_src() -> Path:
    for path in (
        ROOT / "ChillDuanSansVF.ttf",
        ROOT / "tools/fonts/ChillDuanSansVF.ttf",
    ):
        if path.is_file():
            return path
    return ROOT / "ChillDuanSansVF.ttf"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--src", type=Path, default=default_src())
    parser.add_argument("--out", type=Path, default=OUT)
    args = parser.parse_args()
    if not args.src.is_file():
        print(f"missing source font: {args.src}", file=sys.stderr)
        return 1

    text = unique_text(ascii_printable(), load_preset(CHARSET), scan_sources())
    cjk = sum(1 for ch in text if "\u4e00" <= ch <= "\u9fff")
    print(f"charset {len(text)} chars ({cjk} CJK)")

    size = subset_variable(args.src, text, args.out)
    mode = "VF"
    if size > MAX_BYTES:
        print(f"VF subset {size} bytes > {MAX_BYTES}, instantiate wght=400")
        size = subset_regular(args.src, text, args.out)
        mode = "Regular"
    print(f"wrote {args.out} ({size} bytes, {mode})")
    if size > MAX_BYTES:
        print("subset still too large", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
