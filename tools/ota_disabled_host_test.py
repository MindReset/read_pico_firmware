"""中文：联网 OTA 的停用开关必须保持默认开启，且两个入口在停用时不得建任务。
English: the online-OTA kill switch must stay on by default and both entries must refuse without a worker.
SPDX-License-Identifier: Apache-2.0
原由：官方清单指向原厂固件，装上会覆盖本地词典、生词本和飞行模式。
Why: the upstream feed points at the stock image, which overwrites the local dictionary, vocabulary
book and flight mode.
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
header = (root / "main/ota_online.h").read_text()
source = (root / "main/ota_online.c").read_text()
failures = []

# 1. 默认值必须是 1；用 #ifndef 包住，测试才能用 -D 覆盖成 0 继续验证管线本身。
# Must default to 1 behind #ifndef so tests can override it with -D and keep the pipeline covered.
if not re.search(r"#ifndef PICO_ONLINE_UPDATES_DISABLED\s*\n"
                 r"#define PICO_ONLINE_UPDATES_DISABLED 1\s*\n#endif", header):
    failures.append("ota_online.h: PICO_ONLINE_UPDATES_DISABLED must default to 1 behind an #ifndef guard")
# 2. 两个入口在停用时必须直接拒绝，而不是去 start() 建任务。
# Both entries must refuse outright instead of calling start().
# 按大括号配平取函数体：正则会被「一行写完的函数」骗过去，跨到下一个函数上。
# Take the body by brace matching: a regex spills into the next function when the body is one line.
def entry_body(name):
    match = re.search(r"^esp_err_t " + name + r"\(void\)\s*\{", source, re.M)
    if not match:
        return None
    at, depth = match.end(), 1
    while depth and at < len(source):
        if source[at] == "{":
            depth += 1
        elif source[at] == "}":
            depth -= 1
        at += 1
    return source[match.end():at] if not depth else None


for name in ("pico_online_check", "pico_online_download"):
    body = entry_body(name)
    if body is None:
        failures.append("ota_online.c: %s() disappeared or lost its braces" % name)
        continue
    if "PICO_ONLINE_UPDATES_DISABLED" not in body or "ESP_ERR_NOT_SUPPORTED" not in body:
        failures.append("ota_online.c: %s() must refuse with ESP_ERR_NOT_SUPPORTED while disabled" % name)
# 3. 除升级页外不得再冒出新的联网任务发起者（升级页那条链路已被后端拒绝兜住）。
# No new caller outside the upgrade page; the upgrade page is already refused at the backend.
for path in sorted((root / "main").rglob("*.c")):
    text = path.read_text()
    if "pico_online_check(" not in text and "pico_online_download(" not in text:
        continue
    if path.name in ("ota_online.c", "app_device_settings.c"):
        continue
    failures.append("%s: a new caller of pico_online_check/download appeared" % path.relative_to(root))

if failures:
    raise SystemExit("\n".join("FAIL: " + item for item in failures))
print("PASS: online OTA stays disabled by default; both entries refuse without a worker; no new callers")
