"""中文：dict_probe.py 是固件的镜像实现，两边必须做同样的校验与归一化。
English: dict_probe.py mirrors the firmware, so both sides must validate and normalize alike.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
firmware = (root / "main/dict/dict_lookup.c").read_text()
probe = (root / "tools/dict_probe.py").read_text()
failures = []

# 1. 两边都要校验词典文件的布局，而且判据一一对应。
# Both sides must validate the file layout, with matching criteria.
pairs = [
    ("文件大小 / real file size", "fseek(f, 0, SEEK_END)", "os.path.getsize"),
    ("rec_len 与 entries 自洽 / rec_len agrees with entries",
     "rec_len == (uint64_t)entries * DICT_REC_BYTES", "self.rec_len != self.entries * REC_BYTES"),
    ("区段顺序 / section order",
     "keys_off < rec_off &&\n        rec_off < defs_off && defs_off < sparse_off",
     "self.keys_off < self.rec_off\n                and self.rec_off < self.defs_off"),
    ("各段落在文件内 / sections inside the file",
     "sparse_off + (uint64_t)sparse_len <= size", 'if off + length > self.size'),
]
for name, in_c, in_py in pairs:
    if in_c not in firmware:
        failures.append("dict_lookup.c: missing the %s check" % name)
    if in_py not in probe:
        failures.append("dict_probe.py: missing the %s check" % name)

# 2. 两边都要剥首尾标点，且词字符集一致（撇号与连字符必须都算词字符）。
# Both sides must strip leading/trailing punctuation with one shared character set.
if "dict_word_char" not in firmware:
    failures.append("dict_lookup.c: dict_word_char() disappeared")
if "def normalize(" not in probe:
    failures.append("dict_probe.py: normalize() disappeared")
# C 端写作 '\'' 与 '-'；Python 端那个集合用双引号包着，撇号不转义。
# C spells them '\'' and '-'; the Python set is double quoted, so the apostrophe is bare.
for in_c, label in ((r"c == '\''", "apostrophe"), (r"c == '-'", "hyphen")):
    if in_c not in firmware.split("static bool dict_word_char")[1][:220]:
        failures.append("dict_lookup.c: %s must stay a word character" % label)
if "0123456789'-" not in probe:
    failures.append("dict_probe.py: WORD_CHARS must keep digits, apostrophe and hyphen")
if 'normalize(word).encode("utf-8")' not in probe:
    failures.append("dict_probe.py: lookup() must normalize its key the way the firmware does")

if failures:
    raise SystemExit("\n".join("FAIL: " + item for item in failures))
print("PASS: the probe mirrors the firmware's layout validation and punctuation normalization")
