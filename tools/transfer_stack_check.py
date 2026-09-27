"""Compile only transfer.c to temporary output and report target stack frames.

只编译传书组件到临时目录并输出目标栈帧；不触碰固件构建对象。
"""
import json
import pathlib
import shlex
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parent.parent
commands = json.loads((root / "build/compile_commands.json").read_text())
entry = next(item for item in commands if item["file"].endswith("/read_pico_transfer.c"))
with tempfile.TemporaryDirectory(prefix="transfer-stack-") as directory:
    command = shlex.split(entry["command"])
    output = pathlib.Path(directory) / "transfer.o"
    command[command.index("-o") + 1] = str(output)
    command.append("-fstack-usage")
    subprocess.run(command, cwd=entry["directory"], check=True)
    print(output.with_suffix(".su").read_text())
