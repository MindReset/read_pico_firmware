#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 read-pico-reader contributors
# SPDX-License-Identifier: MIT
"""
build_dict.py — Convert the ECDICT CSV (https://github.com/skywind3000/ECDICT, MIT)
into the compact RPD1 binary dictionary consumed by main/dict/dict_lookup.c.

Usage:
  python3 tools/build_dict.py ecdict.csv [out.bin] [--limit N]
                              [--level slim|standard|full] [--full]

Format (all integers little-endian):

  Header (64 bytes):
    0x00  magic   "RPD1\0\0\0\0"
    0x08  u32     version = 1
    0x0C  u32     entries
    0x10  u32     block_log2 (records per sparse entry, as log2; default 10)
    0x14  u32     keys_off / 0x18 u32 keys_len
    0x1C  u32     rec_off  / 0x20 u32 rec_len   (entries * 6)
    0x24  u32     defs_off / 0x28 u32 defs_len
    0x2C  u32     sparse_off / 0x30 u32 sparse_len
    0x34..0x3F    reserved (zero)

  keys   : entries x [u16 len][utf-8 lowercase headword], sorted by memcmp order.
  rec    : entries x [u32 def_off (relative to defs)][u16 def_len], same order.
  defs   : utf-8 text per entry, '\n' separated lines:
             line 1: phonetic  (may be empty)
             line 2: part-of-speech summary (may be empty)
             line 3+: Chinese translations, one per line
  sparse : one entry per 2**block_log2 records, variable length:
             [u32 key_off][u16 key_len][key bytes]
           First headword of the block; lets the device binary-search in RAM
           and stream exactly one key window from the SD card per lookup.
"""
import argparse
import csv
import re
import struct
import sys
import time

MAGIC = b"RPD1\0\0\0\0"
VERSION = 1
BLOCK_LOG2 = 10
BLOCK = 1 << BLOCK_LOG2
HEADER_SIZE = 64
MAX_DEF_BYTES = 1024

EXAM_TAGS = {"cet4", "cet6", "ky", "toefl", "ielts", "gre", "gaozhong",
             "gk", "zk", "cjk", "pem4", "pem6", "tem4", "tem8", "bep"}
FREQ_LIMIT = 60000  # keep words ranked within the top N of BNC / COCA
# 只收可被点词命中的单词：字母数字开头，允许撇号与连字符，长度 <= 20。
# Only single tokens the hit-test can select: alnum start, apostrophe/hyphen allowed.
WORD_RE = re.compile(r"^[0-9A-Za-z][0-9A-Za-z'\-]{0,19}$")

POS_NAMES = ("n.", "v.", "vt.", "vi.", "adj.", "adv.", "prep.", "pron.",
             "conj.", "interj.", "int.", "art.", "aux.", "num.", "pl.",
             "abbr.", "modal.", "det.")


def _rank(v):
    v = (v or "").strip()
    return int(v) if v.isdigit() and int(v) > 0 else 0


def within_freq(row):
    bnc, frq = _rank(row.get("bnc")), _rank(row.get("frq"))
    return (0 < frq <= FREQ_LIMIT) or (0 < bnc <= FREQ_LIMIT)


def has_exchange(row):
    """词形变化数据：有屈折变化的实词，用来补足没有词频的中频词。

    Inflection data marks real lexemes and adds mid-frequency words that
    carry no BNC/COCA rank.
    """
    return bool((row.get("exchange") or "").strip())


def keep(row, level):
    """slim: 词频 + 考试标签。standard: 再加有词形变化的词。full: 全部单词。"""
    if level == "full":
        return True
    tags = set((row.get("tag") or "").split())
    if tags & EXAM_TAGS or _rank(row.get("collins")) or _rank(row.get("oxford")):
        return True
    if within_freq(row):
        return True
    return level == "standard" and has_exchange(row)


# ECDICT 把形状相似的西里尔/希腊字母写进了音标（ә 出现约 15 万次），应按拉丁 IPA 取字形。
# ECDICT wrote visually similar Cyrillic/Greek letters into phonetics (~150k uses of ә);
# take the Latin IPA codepoints instead so a font with IPA coverage can actually draw them.
LOOKALIKES = {
    "\u04d9": "\u0259",  # ә -> ə  schwa
    "\u0454": "\u025b",  # є -> ɛ  open e
    "\u03b5": "\u025b",  # ε -> ɛ  Greek epsilon used as IPA open e
}

# 释义里残留的 HTML 标记（<br /> 与个别 <X>、<DDX> 残片）。
# Leftover HTML in translations: <br /> plus occasional <X>/<DDX> fragments.
TAG_RE = re.compile(r"<[A-Za-z/!][^>]*>")
BR_RE = re.compile(r"<\s*br\s*/?\s*>", re.IGNORECASE)


def normalize(text):
    """ECDICT stores literal backslash-n escapes inside quoted fields."""
    if not text:
        return ""
    text = text.replace("\\r\\n", "\n").replace("\\n", "\n").replace("\\r", "\n")
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    text = text.replace("\t", " ")
    text = BR_RE.sub("\n", text)   # <br /> 本来就是换行 / a real line break
    text = TAG_RE.sub("", text)    # 其余 ASCII 标记是数据残片 / other ASCII tags are junk
    for bad, good in LOOKALIKES.items():
        text = text.replace(bad, good)
    return text


def compose(phonetic, translation):
    """Return (phonetic_line, pos_line, body_lines) already normalized."""
    phon = normalize(phonetic).strip().replace("\n", " / ")
    poses = []
    for raw in normalize(translation).split("\n"):
        line = raw.strip()
        if not line:
            continue
        token = line.split(" ", 1)[0]
        if token in POS_NAMES and token not in poses:
            poses.append(token)
    # 孤立成行的词性标签（整行只有 "n." 之类）只进摘要，不进正文，
    # 否则摘要被去重后正文里还会再显示一遍。
    # A bare part-of-speech line belongs to the summary only; keeping it in the
    # body too means it survives after the summary is dropped.
    body = [ln.strip() for ln in normalize(translation).split("\n")
            if ln.strip() and ln.strip() not in POS_NAMES]
    # 正文里已经带着同一个词性时，不再单独输出词性行：否则卡片上会连着显示两遍
    # （旧版约 55% 的词条如此）。多词性摘要（"n. | v."）仍保留，它含正文没有的信息。
    # When the body already carries the same part of speech, drop the standalone
    # summary line or the card shows it twice (~55% of entries did in the old
    # build). Keep multi-POS summaries; they carry information the body lacks.
    if len(poses) == 1 and body:
        tok = poses[0]
        if any(ln == tok or ln.startswith(tok + " ") for ln in body):
            poses = []
    return phon, " | ".join(poses), body


def record_bytes(word, phonetic, translation):
    phon, pos, body = compose(phonetic, translation)
    lines = []
    if phon:
        lines.append("[%s]" % phon)
    if pos:
        lines.append(pos)
    lines.extend(body)
    text = "\n".join(lines).encode("utf-8")
    if len(text) > MAX_DEF_BYTES:  # drop body lines until it fits
        budget = MAX_DEF_BYTES
        kept = []
        for ln in lines:
            b = ln.encode("utf-8")
            extra = len(b) + (1 if kept else 0)
            if budget - extra < 0 and kept:
                break
            if extra <= budget:
                kept.append(ln)
                budget -= extra
        text = "\n".join(kept).encode("utf-8")
    return text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv_path")
    ap.add_argument("out_path", nargs="?")
    ap.add_argument("--limit", type=int, default=0, help="cap entries (smoke test)")
    ap.add_argument("--level", choices=("slim", "standard", "full"), default="standard",
                    help="slim ~58k / standard ~124k (default) / full ~400k entries")
    ap.add_argument("--full", action="store_true", help="shorthand for --level full")
    args = ap.parse_args()
    level = "full" if args.full else args.level
    out_path = args.out_path or "dict.bin"

    t0 = time.time()
    best = {}
    scanned = 0
    with open(args.csv_path, newline="", encoding="utf-8") as fh:
        reader = csv.DictReader(fh)
        for row in reader:
            scanned += 1
            word = (row.get("word") or "").strip()
            if not word or not word.isascii() or not WORD_RE.match(word):
                continue
            if not (row.get("translation") or "").strip():
                continue
            key = word.lower()
            if not keep(row, level):
                continue
            if args.limit and len(best) >= args.limit and key not in best:
                continue
            def _best_rank(v):
                v = (v or "").strip()
                return int(v) if v.isdigit() and int(v) > 0 else 1 << 30
            rank = min(_best_rank(row.get("frq")), _best_rank(row.get("bnc")))
            prev = best.get(key)
            if prev is None or rank < prev[0]:
                best[key] = (rank, word, row.get("phonetic") or "", row.get("translation") or "")
    keys = sorted(k for k in best.keys()
                  if len(k.encode("utf-8")) <= 63)  # sparse slot stores full heads

    # ---- serialize ----
    keys_blob = bytearray()
    recs = []
    defs_blob = bytearray()
    for k in keys:
        _, word, phon, trans = best[k]
        kb = k.encode("utf-8")
        keys_blob += struct.pack("<H", len(kb)) + kb
        def_text = record_bytes(word, phon, trans)
        recs.append((len(defs_blob), len(def_text)))
        defs_blob += def_text
    rec_blob = b"".join(struct.pack("<IH", off, ln) for off, ln in recs)

    sparse = bytearray()
    off = 0
    for i, k in enumerate(keys):
        if i % BLOCK == 0:
            kb = k.encode("utf-8")
            sparse += struct.pack("<IH", off, len(kb)) + kb
        off += 2 + len(k.encode("utf-8"))

    keys_off = HEADER_SIZE
    rec_off = keys_off + len(keys_blob)
    defs_off = rec_off + len(rec_blob)
    sparse_off = defs_off + len(defs_blob)

    header = MAGIC + struct.pack("<II", VERSION, len(keys)) + struct.pack(
        "<I", BLOCK_LOG2)
    header += struct.pack("<II", keys_off, len(keys_blob))
    header += struct.pack("<II", rec_off, len(rec_blob))
    header += struct.pack("<II", defs_off, len(defs_blob))
    header += struct.pack("<II", sparse_off, len(sparse))
    header += b"\0" * (HEADER_SIZE - len(header))
    assert len(header) == HEADER_SIZE

    with open(out_path, "wb") as fh:
        fh.write(header)
        fh.write(keys_blob)
        fh.write(rec_blob)
        fh.write(defs_blob)
        fh.write(sparse)

    size = sparse_off + len(sparse)
    print("scanned=%d kept=%d size=%.1fMB defs=%.1fMB sparse=%dB time=%.1fs" % (
        scanned, len(keys), size / 1e6, len(defs_blob) / 1e6, len(sparse),
        time.time() - t0))


if __name__ == "__main__":
    sys.exit(main())
