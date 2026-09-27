# 离线书名匹配 / Offline filename matching

`read_pico_search_match(filename, query)` 无文件访问、无堆分配，可重入。输入是 UTF-8 书名（最多255字节）与查询（最多64字节），不是路径。NULL、超长或非法UTF-8返回false。忽略最后的`.txt`/`.epub`扩展名；空查询及纯ASCII空白查询匹配全部。

The matcher is reentrant, allocation-free and performs no file access. Inputs are UTF-8 filenames (not paths, at most 255 bytes) and queries (at most 64 bytes). NULL, oversized or invalid UTF-8 inputs return false. Final `.txt`/`.epub` extensions are ignored. Empty or ASCII-whitespace-only queries match all.

三个独立匹配通道：原文子串（ASCII忽略大小写）、无声调拼音子串、中英首字母子串。拼音查询可用空格分隔，`v`或`ü`表示ü；英文首字母按ASCII单词边界，数字逐位保留。ASCII分隔符在拼音通道中忽略；未知非ASCII字符中断拼音匹配，但仍能按原文搜索。全拼支持从音节内部开始的子串，首字母和全拼不在同一通道混用。

Three independent channels match literal substrings (ASCII case-insensitive), toneless pinyin substrings and Chinese/English initials. Pinyin queries may have separating whitespace; `v` or `ü` means ü. English initials follow ASCII word boundaries; digits are retained individually. ASCII separators are skipped in pinyin channels. Unknown non-ASCII characters break those channels but remain searchable literally. Full pinyin substrings may begin inside syllables; full pinyin and initials are not mixed in one channel.

逐字尝试全部字典读音，不做分词或上下文消歧。因此重庆可匹配`chongqing`和`zhongqing`。64位状态集合合并多音分支，不生成读音笛卡尔积。只读字表283,381字节，覆盖41,923字符、4,479读音组合及425音节。

All dictionary readings are considered per character, without segmentation or contextual disambiguation: 重庆 matches both `chongqing` and `zhongqing`. A 64-bit state set merges branches without generating a Cartesian product. Read-only tables use 283,381 bytes for 41,923 characters, 4,479 reading groups and 425 syllables.

## 数据来源与再生成 / Provenance and regeneration

- 官方项目 / Official project: https://github.com/mozillazg/python-pinyin
- 固定版本 / Pinned release: `pypinyin==0.55.0`, official PyPI `pypinyin-0.55.0-py2.py3-none-any.whl`.
- 原始文件 / Source data: `pypinyin/pinyin_dict.json`.
- Wheel SHA-256: `d53b1e8ad2cdb815fb2cb604ed3123372f5a28c6f447571244aca36fc62a286f`.
- 字典采用原项目MIT许可，原文保存在[LICENSE.pypinyin](LICENSE.pypinyin)。匹配器与生成脚本为Apache-2.0。
- Dictionary data retains upstream MIT licensing in [LICENSE.pypinyin](LICENSE.pypinyin). Matcher and generator use Apache-2.0.

```sh
python -m pip download --no-deps pypinyin==0.55.0 -d build/search-deps
python tools/generate_search_table.py build/search-deps/pypinyin-0.55.0-py2.py3-none-any.whl
python tools/test_search.py build/search-deps/pypinyin-0.55.0-py2.py3-none-any.whl
```

生成只使用Python标准库并校验wheel摘要。无需安装或执行第三方包。固件编译直接使用已生成字表，不联网、不依赖Python。

Generation uses only the Python standard library and verifies the wheel digest; it neither installs nor executes third-party packages. Firmware builds use the checked-in table without Python or network access.
