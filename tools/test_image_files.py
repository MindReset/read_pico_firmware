#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Real streaming JPEG/PNG tests. Requires Pillow and libpng (pkg-config or --png-prefix)."""
import argparse
from pathlib import Path
import random
import shlex
import subprocess
import sys
import tempfile
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]

def main():
    args = argparse.ArgumentParser()
    args.add_argument("--png-prefix", type=Path)
    options = args.parse_args()
    if options.png_prefix:
        png_cflags = ["-I" + str(options.png_prefix / "include/libpng16")]
        png_link = ["-L" + str(options.png_prefix / "lib"), "-lpng16", "-lz",
                    "-Wl,-rpath," + str(options.png_prefix / "lib")]
    else:
        png_cflags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "libpng"], text=True))
        png_link = shlex.split(subprocess.check_output(["pkg-config", "--libs", "libpng"], text=True))
    with tempfile.TemporaryDirectory(prefix="pico-image-stream-") as temp:
        work = Path(temp)
        (work / "sdkconfig.h").write_text("\n".join([
            "#define CONFIG_JD_SZBUF 512", "#define CONFIG_JD_FORMAT 0", "#define CONFIG_JD_USE_SCALE 1",
            "#define CONFIG_JD_TBLCLIP 1", "#define CONFIG_JD_FASTDECODE 0"]) + "\n")
        includes = ["tools/book_epub_stubs", "main/book", "components/jpegdec/src",
                    "managed_components/espressif__esp_jpeg/tjpgd", "managed_components/espressif__esp_jpeg/include"]
        flags = ["-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                 "-ffunction-sections", "-fdata-sections"]
        flags += ["-I" + str(ROOT / path) for path in includes] + ["-I" + str(work)] + png_cflags
        sources = ["tools/book_image_file_host_test.c", "main/book/book_cover.c", "main/book/book_image_header.c",
                   "managed_components/espressif__esp_jpeg/tjpgd/tjpgd.c",
                   "components/jpegdec/src/JPEGDEC.cpp", "components/jpegdec/src/jpegdec_shim.cpp"]
        objects = []
        for index, source in enumerate(sources):
            obj = work / f"part{index}.o"
            cpp = source.endswith(".cpp")
            # Upstream JPEGDEC intentionally uses unaligned loads and signed shifts;
            # retain ASan there, and full UB checks in the new file/stream wrappers.
            upstream_flags = ["-fno-sanitize=alignment,shift"] if source.endswith("JPEGDEC.cpp") else []
            subprocess.run(["c++" if cpp else "cc", "-std=c++17" if cpp else "-std=c11", *flags, *upstream_flags,
                            "-c", str(ROOT / source), "-o", str(obj)], check=True)
            objects.append(str(obj))
        gc = "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"
        exe = work / "test"
        subprocess.run(["c++", *flags, gc, *objects, *png_link, "-o", str(exe)], check=True)
        image = Image.frombytes("RGB", (1536, 1536), random.Random(82).randbytes(1536 * 1536 * 3))
        image.save(work / "large.jpg", quality=100, subsampling=0)
        image.save(work / "large.png")
        image.save(work / "progressive.jpg", quality=95, progressive=True)
        Image.new("RGB", (829, 731), (160, 160, 160)).save(work / "odd.jpg")
        Image.new("RGB", (53, 41), (160, 160, 160)).save(work / "small.jpg", progressive=True)
        files = [work / name for name in ("large.jpg", "large.png", "progressive.jpg", "odd.jpg", "small.jpg")]
        assert all(path.stat().st_size > 2 * 1024 * 1024 for path in files[:3])
        subprocess.run([str(exe), "read", *map(str, files)], check=True)
        (work / "bad.jpg").write_bytes(b"not an image")
        (work / "bad.png").write_bytes((work / "large.png").read_bytes()[:400])
        with (work / "overlimit.jpg").open("wb") as file:
            file.write((work / "odd.jpg").read_bytes())
            file.truncate(50 * 1024 * 1024 + 1)
        subprocess.run([str(exe), "reject", *map(str, [work / "bad.jpg", work / "bad.png", work / "overlimit.jpg"])], check=True)
        print("PASS: streaming baseline/progressive JPEG and PNG over 2 MiB; odd/small dimensions; corruption/limit bounds")

if __name__ == "__main__":
    main()
