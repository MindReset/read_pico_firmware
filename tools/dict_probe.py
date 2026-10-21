#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 read-pico-reader contributors
# SPDX-License-Identifier: MIT
"""
dict_probe.py — Verify/inspect an RPD1 dictionary using the same algorithm as
main/dict/dict_lookup.c: RAM sparse index -> one key window read -> linear
window scan -> definition read. Includes the same light morphological
fallback (plural/verb forms) so a passing probe means the device logic agrees.

Usage:
  python3 tools/dict_probe.py dict.bin word [word ...]   # query words
  python3 tools/dict_probe.py dict.bin --check           # full consistency check
  python3 tools/dict_probe.py dict.bin                   # interactive
"""
import os
import struct
import sys

HEADER_SIZE = 64
REC_BYTES = 6

# 与 dict_lookup.c 的 dict_word_char() 同一套字符：小写字母、数字、词内的撇号与连字符。
# Same set as dict_word_char() in dict_lookup.c: lowercase letters, digits, apostrophe, hyphen.
WORD_CHARS = set("abcdefghijklmnopqrstuvwxyz0123456789'-")


def normalize(word):
    """Mirror of dict_lookup_word(): ASCII lowercase plus leading/trailing punctuation strip."""
    s = str(word).strip().lower()
    a, b = 0, len(s)
    while a < b and s[a] not in WORD_CHARS:
        a += 1
    while b > a and s[b - 1] not in WORD_CHARS:
        b -= 1
    return s[a:b]


class Rpd1:
    def __init__(self, path):
        self.f = open(path, "rb")
        hdr = self.f.read(HEADER_SIZE)
        if hdr[:8] != b"RPD1\0\0\0\0":
            raise ValueError("bad magic")
        self.version, self.entries, self.block_log2 = struct.unpack_from("<III", hdr, 8)
        self.block = 1 << self.block_log2
        (self.keys_off, self.keys_len, self.rec_off, self.rec_len,
         self.defs_off, self.defs_len, self.sparse_off, self.sparse_len
         ) = struct.unpack_from("<8I", hdr, 0x14)
        # 与 dict_lookup.c 的 try_open() 同一套布局校验：rec_len 自洽、区段顺序、各段落在文件内。
        # Same layout checks as try_open() in dict_lookup.c.
        self.size = os.path.getsize(path)
        if not (self.version == 1 and self.entries and 4 <= self.block_log2 <= 16):
            raise ValueError("bad header")
        if self.rec_len != self.entries * REC_BYTES:
            raise ValueError("rec_len disagrees with entries")
        if not (self.keys_off >= HEADER_SIZE and self.keys_off < self.rec_off
                and self.rec_off < self.defs_off and self.defs_off < self.sparse_off):
            raise ValueError("section order is not keys < rec < defs < sparse")
        for name, off, length in (("keys", self.keys_off, self.keys_len),
                                  ("rec", self.rec_off, self.rec_len),
                                  ("defs", self.defs_off, self.defs_len),
                                  ("sparse", self.sparse_off, self.sparse_len)):
            if off + length > self.size:
                raise ValueError("%s section runs past the end of the file" % name)
        self.f.seek(self.sparse_off)
        blob = self.f.read(self.sparse_len)
        self.sparse = []  # (key_off, key_bytes)
        pos = 0
        while pos < len(blob):
            off, klen = struct.unpack_from("<IH", blob, pos)
            pos += 6
            self.sparse.append((off, blob[pos:pos + klen]))
            pos += klen

    def _window_for(self, slot):
        start = self.sparse[slot][0]
        end = self.sparse[slot + 1][0] if slot + 1 < len(self.sparse) else self.keys_len
        self.f.seek(self.keys_off + start)
        keys_win = self.f.read(end - start)
        first = slot * self.block
        count = min(self.block, self.entries - first)
        self.f.seek(self.rec_off + first * REC_BYTES)
        recs = self.f.read(count * REC_BYTES)
        return keys_win, recs

    @staticmethod
    def _scan_window(keys_win, recs, key):
        """Same linear walk the device uses; returns (def_off, def_len) or None."""
        pos = rpos = 0
        best = None
        while pos + 2 <= len(keys_win):
            klen, = struct.unpack_from("<H", keys_win, pos)
            kb = keys_win[pos + 2:pos + 2 + klen]
            if len(kb) < klen:
                break
            if kb == key:
                d_off, d_len = struct.unpack_from("<IH", recs, rpos)
                best = (d_off, d_len)
            elif kb > key:
                break
            pos += 2 + klen
            rpos += REC_BYTES
        return best

    def lookup(self, word):
        key = normalize(word).encode("utf-8")
        if not key:
            return None
        lo, hi = 0, len(self.sparse)
        slot = -1
        while lo + 1 < hi:
            mid = (lo + hi) // 2
            if self.sparse[mid][1] <= key:
                lo = mid
            else:
                hi = mid
        if self.sparse and self.sparse[lo][1] <= key:
            slot = lo
        if slot < 0:
            return None
        keys_win, recs = self._window_for(slot)
        hit = self._scan_window(keys_win, recs, key)
        if not hit:
            return None
        d_off, d_len = hit
        self.f.seek(self.defs_off + d_off)
        return self.f.read(d_len).decode("utf-8", "replace")

    @staticmethod
    def _candidates(w):
        """Mirror of dict_lookup.c morphological candidates, in try order."""
        out = []

        def add(c):
            if c and c != w and c not in out:
                out.append(c)

        def dbl_stem(suffix):
            # doubled final consonant: stopped -> stop, running -> run
            if len(w) > len(suffix) + 2:
                last = w[-(len(suffix) + 1)]
                if last.isalpha() and last not in "aeiouy" and last == w[-(len(suffix) + 2)]:
                    add(w[:-(len(suffix) + 1)])
        if w.endswith("'s"):
            add(w[:-2])
        if w.endswith("ies") and len(w) > 4:
            add(w[:-3] + "y")
        if w.endswith("es") and len(w) > 3:
            add(w[:-2])
        if w.endswith("s") and not w.endswith("ss") and len(w) > 2:
            add(w[:-1])
        if w.endswith("ied") and len(w) > 4:
            add(w[:-3] + "y")
        if w.endswith("ed") and len(w) > 3:
            add(w[:-2])            # walked -> walk
            add(w[:-1])            # loved  -> love
            dbl_stem("ed")         # stopped -> stop
        if w.endswith("ing") and len(w) > 4:
            add(w[:-3])            # doing -> do
            add(w[:-3] + "e")      # writing -> write
            dbl_stem("ing")        # running -> run
        for suf in ("er", "est"):
            if w.endswith(suf) and len(w) > len(suf) + 2:
                add(w[:-len(suf)])         # taller -> tall
                add(w[:-len(suf) - 1])     # bigger -> big (doubled)
                if w.endswith(("ier", "iest")):
                    add(w[:-(len(suf) + 1)] + "y")  # happier -> happy
        if w.endswith("ily") and len(w) > 4:
            add(w[:-3] + "y")      # happily -> happy
        elif w.endswith("ly") and len(w) > 4:
            add(w[:-2])            # quickly -> quick
        return out

    def lookup_fallback(self, word):
        w = normalize(word)
        for c in self._candidates(w):
            hit = self.lookup(c)
            if hit:
                return ("[" + c + "] " + hit).encode("utf-8", "replace")
        return None


def show(rpd, word):
    hit = rpd.lookup(word)
    tag = "hit"
    if hit is None:
        fb = rpd.lookup_fallback(word)
        if fb:
            hit = fb.decode("utf-8", "replace")
            tag = "fallback"
    if hit is None:
        print("%-16s MISS" % word)
        return
    lines = hit.split("\n")
    print("%-16s [%s] %s | %s" % (word, tag, lines[0], " ⏎ ".join(lines[1:])[:110]))


def check(rpd):
    """Stream every entry and confirm key order + rec/defs consistency."""
    prev = b""
    bad = 0
    checked = 0
    self_recs = 0
    self_defs = 0
    for slot in range(len(rpd.sparse)):
        keys_win, recs = rpd._window_for(slot)
        pos = rpos = 0
        while pos + 2 <= len(keys_win) and rpos + REC_BYTES <= len(recs):
            klen, = struct.unpack_from("<H", keys_win, pos)
            kb = keys_win[pos + 2:pos + 2 + klen]
            if len(kb) < klen:
                break
            if kb <= prev:
                print("ORDER VIOLATION at entry %d: %r after %r" % (checked, kb, prev))
                bad += 1
            prev = kb
            d_off, d_len = struct.unpack_from("<IH", recs, rpos)
            if d_off + d_len > rpd.defs_len:
                print("DEF RANGE VIOLATION for %r" % kb)
                bad += 1
            self_recs += 1
            self_defs += d_len
            pos += 2 + klen
            rpos += REC_BYTES
            checked += 1
    print("check: entries=%d records=%d def_bytes=%d violations=%d" % (
        rpd.entries, self_recs, self_defs, bad))
    ok = checked == rpd.entries and bad == 0 and self_defs == rpd.defs_len
    print("PASS" if ok else "FAIL")
    return ok


def main():
    path = sys.argv[1]
    rpd = Rpd1(path)
    print("entries=%d block=%d keys=%dB recs=%dB defs=%.1fMB sparse=%dB" % (
        rpd.entries, rpd.block, rpd.keys_len, rpd.rec_len,
        rpd.defs_len / 1e6, rpd.sparse_len))
    args = sys.argv[2:]
    if args and args[0] == "--check":
        sys.exit(0 if check(rpd) else 1)
    if args:
        for w in args:
            show(rpd, w)
        return
    for line in sys.stdin:
        w = line.strip()
        if w:
            show(rpd, w)


if __name__ == "__main__":
    main()
