#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
"""真实 IDF FatFs 主机回归。/ Host regression using the installed IDF FatFs sources."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
IDF = Path(os.environ.get("IDF_PATH", "/opt/esp/idf"))


def main():
    for name in ("sdkconfig.defaults", "sdkconfig.ci"):
        defaults = (ROOT / name).read_text(encoding="utf-8")
        assert "CONFIG_FATFS_LFN_HEAP=y" in defaults
        assert "CONFIG_FATFS_API_ENCODING_UTF_8=y" in defaults
        assert "CONFIG_FATFS_API_ENCODING_ANSI_OEM=y" not in defaults
    src = IDF / "components/fatfs/src"
    with tempfile.TemporaryDirectory(prefix="fatfs-name-") as temp:
        work = Path(temp)
        (work / "freertos").mkdir()
        (work / "freertos/FreeRTOS.h").write_text("#define portTICK_PERIOD_MS 1\n")
        (work / "freertos/semphr.h").write_text("")
        # 仅替身块设备/锁；生产 FatFs 和配置映射原样编译。
        # Stub only the disk and locks; compile production FatFs and its config mapping unchanged.
        config = """
#define CONFIG_FATFS_CODEPAGE 437
#define CONFIG_FATFS_LFN_HEAP 1
#define CONFIG_FATFS_MAX_LFN 255
#define CONFIG_FATFS_VOLUME_COUNT 2
#define CONFIG_WL_SECTOR_SIZE 4096
#define CONFIG_FATFS_PER_FILE_CACHE 1
#define CONFIG_FATFS_FS_LOCK 0
#define CONFIG_FATFS_TIMEOUT_MS 10000
#define CONFIG_FATFS_USE_DYN_BUFFERS 1
"""
        for mode in ("utf8", "oem"):
            encoding = "#define CONFIG_FATFS_API_ENCODING_UTF_8 1\n" if mode == "utf8" else ""
            (work / "sdkconfig.h").write_text(config + encoding)
            subprocess.run([
                "cc", "-std=gnu17", "-g", "-fsanitize=address,undefined",
                "-I" + str(work), "-I" + str(src),
                str(ROOT / "tools/fatfs_filename_host_test.c"),
                str(src / "ff.c"), str(src / "ffunicode.c"),
                "-o", str(work / mode),
            ], check=True)
        image = str(work / "disk.img")
        subprocess.run([str(work / "utf8"), "create", image], check=True)
        subprocess.run([str(work / "oem"), "read", image], check=True)
        subprocess.run([str(work / "utf8"), "read", image], check=True)


if __name__ == "__main__":
    main()
