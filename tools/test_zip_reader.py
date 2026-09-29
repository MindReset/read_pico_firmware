#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
"""生成正常/损坏 ZIP 并运行解析器。/ Generate valid/malformed ZIPs and exercise the parser."""
import io
from pathlib import Path
import struct
import subprocess
import tempfile
import warnings
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory(prefix="zip-reader-") as temp:
        work = Path(temp)
        exe = work / "test"
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        "-I" + str(ROOT / "tools/zip_host_stubs"), "-I" + str(ROOT / "main/book"),
                        str(ROOT / "tools/zip_reader_host_test.c"), str(ROOT / "main/book/zip_reader.c"),
                        "-lz", "-o", str(exe)], check=True)
        cases = 0

        def run(mode, data, name="entry.txt", expected=None):
            nonlocal cases
            path = work / "test.zip"
            path.write_bytes(data)
            command = [str(exe), mode, str(path), name]
            if expected is not None:
                payload = work / "expected"
                payload.write_bytes(expected)
                command.append(str(payload))
            subprocess.run(command, check=True)
            cases += 1

        def archive(content, method=zipfile.ZIP_DEFLATED, comment=b"", stream=False):
            class Streaming(io.BytesIO):
                def seekable(self): return False
                def seek(self, *args): raise OSError("stream")
            buffer = Streaming() if stream else io.BytesIO()
            with zipfile.ZipFile(buffer, "w", compression=method) as z:
                z.writestr("entry.txt", content)
                z.comment = comment
            return buffer.getvalue()

        payload = ("中文文本\n" * 1000).encode()
        for method in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
            for data in (b"", b"hello", payload, bytes(range(256)) * 100):
                run("read", archive(data, method), expected=data)
        run("read", archive(payload, stream=True), expected=payload)
        run("read", archive(payload, comment=b"x" * 65535), expected=payload)
        run("read", archive(b"x" * (2 * 1024 * 1024)), expected=b"x" * (2 * 1024 * 1024))
        run("reject-open", archive(b"x" * (4 * 1024 * 1024 + 1)))
        good = archive(payload)
        cd = good.index(b"PK\x01\x02")
        eocd = good.rindex(b"PK\x05\x06")

        def changed(offset, fmt, value):
            data = bytearray(good)
            struct.pack_into(fmt, data, offset, value)
            return data

        for offset, fmt, value in ((cd + 8, "<H", 1), (cd + 10, "<H", 99),
                                   (cd + 20, "<I", 0xffffffff), (cd + 24, "<I", 0xffffffff),
                                   (cd + 42, "<I", cd), (cd + 34, "<H", 1),
                                   (eocd + 4, "<H", 1), (eocd + 10, "<H", 513),
                                   (eocd + 16, "<I", 0xffffffff), (cd + 28, "<H", 1025)):
            run("reject-open", changed(offset, fmt, value))
        for offset, fmt, value in ((0, "<I", 0), (8, "<H", 0), (26, "<H", 500),
                                   (28, "<H", 65535), (14, "<I", 0), (30, "<B", 33)):
            run("reject-extract", changed(offset, fmt, value))
        damaged = bytearray(good)
        damaged[40] ^= 0xff
        run("reject-extract", damaged)
        wrong_crc = changed(cd + 16, "<I", 42)
        struct.pack_into("<I", wrong_crc, 14, 42)
        run("reject-extract", wrong_crc)
        too_small = changed(cd + 24, "<I", 1)
        struct.pack_into("<I", too_small, 22, 1)
        run("reject-extract", too_small)
        run("reject-extract", changed(18, "<I", 0xffffffff))
        run("reject-open", good[:-1])
        run("reject-open", b"not a ZIP")
        buffer = io.BytesIO()
        with zipfile.ZipFile(buffer, "w"): pass
        run("empty", buffer.getvalue())
        for count in (512, 513, 2048, 2049, 4096, 4097, 32768, 32769):
            buffer = io.BytesIO()
            with zipfile.ZipFile(buffer, "w") as z:
                for i in range(count): z.writestr(f"entry{i}.txt", b"")
            run("empty" if count <= 32768 else "reject-open", buffer.getvalue())
        # ZIP64 扩展不依赖哨兵值也必须拒绝。/ Reject ZIP64 extras even without sentinel sizes.
        buffer = io.BytesIO()
        with zipfile.ZipFile(buffer, "w") as z:
            info = zipfile.ZipInfo("entry.txt")
            info.extra = struct.pack("<HHQQ", 1, 16, 0, 0)
            z.writestr(info, b"")
        run("reject-open", buffer.getvalue())
        buffer = io.BytesIO()
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", UserWarning)
            with zipfile.ZipFile(buffer, "w") as z:
                z.writestr("entry.txt", b"one")
                z.writestr("entry.txt", b"two")
        run("reject-open", buffer.getvalue())
        # FNV-1a 冲突仍按完整路径区分。/ Distinguish full paths despite an FNV-1a collision.
        buffer = io.BytesIO()
        with zipfile.ZipFile(buffer, "w") as z:
            z.writestr("costarring", b"first")
            z.writestr("liquid", b"second")
        run("read", buffer.getvalue(), name="costarring", expected=b"first")
        run("read", buffer.getvalue(), name="liquid", expected=b"second")
        buffer = io.BytesIO()
        with zipfile.ZipFile(buffer, "w", compression=zipfile.ZIP_DEFLATED) as z:
            z.writestr("正文/第一章.xhtml", payload)
        run("read", buffer.getvalue(), name="正文/第一章.xhtml", expected=payload)
        print(f"ZIP parser: {cases} normal, limit and malformed cases passed (host zlib inflate adapter)")


if __name__ == "__main__":
    main()
