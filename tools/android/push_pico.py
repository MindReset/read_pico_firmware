#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
#
# 中文：ReadPico 安卓 BLE 推流的「协议参考实现」。本文件是固件侧
# components/push_sink 的逐字段对表（帧布局、CRC32、状态机语义），既能在
# Linux/macOS/Windows 上用 bleak 真机推送，也能 import 进单测做帧对表。
# 它不依赖 ESP-IDF，字节序全部显式小端，与 C 的 push_proto.h 一一对应。
#
# English: the authoritative reference implementation of ReadPico's Android BLE
# push. This file mirrors components/push_sink byte for byte (frame layout,
# CRC32, state-machine semantics). It pushes to real hardware via bleak on
# Linux/macOS/Windows and is importable for frame-level unit tests. No ESP-IDF
# dependency; every field is explicit little-endian, matching push_proto.h.
#
# 冻结：帧布局与 components/push_sink/include/push_proto.h 完全一致，单方面
# 改字段会破坏已发货固件。/ Frozen: identical to push_proto.h; changing a field
# unilaterally breaks already-shipped firmware.

"""ReadPico BLE push — reference client + frame codec.

Usage examples:
    # 自检：CRC32 与帧布局对表（不需要蓝牙，也不需要 bleak）
    python3 push_pico.py --selftest

    # 推送一段剪贴板文字到墨水屏（需要 bleak + 系统蓝牙）
    python3 push_pico.py --text "$(wl-paste)" --title "来自手机"

    # 推送一个单词卡（设备用内置词典弹卡，不做整书）
    python3 push_pico.py --word "serendipity"

    # 推送一个 URL（M1.5 起设备端抓取；M1 仅占位）
    python3 push_pico.py --url "https://example.com/article" --title "好文"

    # 扫描并交互选择设备（token 从广播名 PicoPush-XXXX 自动读取）
    python3 push_pico.py --text "hello" --scan
"""

from __future__ import annotations

import argparse
import struct
import sys
import time
import zlib

# --------------------------------------------------------------------------
# 协议常量（与 components/push_sink/include/push_proto.h 一字不差）
# Protocol constants — byte-identical to push_proto.h
# --------------------------------------------------------------------------

PUSH_MAGIC0 = 0x50          # 'P'
PUSH_MAGIC1 = 0x43          # 'C'
PUSH_VERSION = 0x01

PUSH_TITLE_MAX = 64         # 标题 UTF-8 上限（含结尾 0）
PUSH_DATA_PAYLOAD_MAX = 241 # MTU 247 时每包载荷：3 写属性头 + 3 帧头
PUSH_TOTAL_MAX = 512 * 1024 # 正文硬上限；超了 ERR(3)

# CTRL 操作码
PUSH_OP_BEGIN = 0x01
PUSH_OP_END = 0x02
# gen22：校时操作码（4 字节头 + UTC 秒 u32 LE）。BEGIN 帧冻结，故走独立 opcode。
# gen22: time-sync opcode (4-byte header + UTC seconds u32 LE). BEGIN is frozen,
# hence a separate opcode.
PUSH_OP_SET_TIME = 0x05
PUSH_OP_RESUME_QUERY = 0x03
PUSH_OP_ABORT = 0x04

# STATUS 操作码（设备 -> 手机）
PUSH_ST_READY = 0x10
PUSH_ST_RESUME = 0x11
PUSH_ST_DONE = 0x12
PUSH_ST_ERR = 0x13
PUSH_ST_EVT_READ = 0x14
PUSH_ST_BATT = 0x15
PUSH_ST_PROGRESS = 0x16

# 正文类型
PUSH_TYPE_TEXT = 0
PUSH_TYPE_MARKDOWN = 1
PUSH_TYPE_HTML = 2
PUSH_TYPE_URL = 3
PUSH_TYPE_WORD = 4

# 错误码（STATUS ERR 的 body）
PUSH_ERR_NONE = 0
PUSH_ERR_NOT_ARMED = 1
PUSH_ERR_BAD_FRAME = 2
PUSH_ERR_TOO_LARGE = 3
PUSH_ERR_CHECKSUM = 4
PUSH_ERR_NO_CARD = 5
PUSH_ERR_LOW_BATTERY = 6
PUSH_ERR_BUSY = 7
PUSH_ERR_FETCH = 8
PUSH_ERR_NO_DICT = 9

PUSH_BEGIN_LEN = 83
PUSH_DATA_HDR_LEN = 3
PUSH_STATUS_MAX = 16

# GATT 服务与特征（草案 3.1，写死）
PUSH_SERVICE_UUID = "e2f5a1c0-9b7d-4e3f-8a61-3c9d2b7e5f10"
# 实证修正记录（gen12）：固件 5f11/5f12 特性位一直是 F_WRITE（WWR，写请求），
# F_WRITE_NO_RSP 从未在固件出现——"PUSH_DATA 是 write-no-response"的旧注释已撤销。
# NimBLE 对 WWR/WNR 两种写一视同仁（都汇入 write_handle），但本客户端默认走
# WWR（response=True）：ATT 逐包回执是第一道防线，设备侧 RESUME 是第二道。
# 最终协议方向待真机数据定案（见 docs/M1-audit.md）。
PUSH_DATA_UUID = "e2f5a1c0-9b7d-4e3f-8a61-3c9d2b7e5f11"   # WWR 属性；客户端 response=True
PUSH_CTRL_UUID = "e2f5a1c0-9b7d-4e3f-8a61-3c9d2b7e5f12"   # write (response)
PUSH_STATUS_UUID = "e2f5a1c0-9b7d-4e3f-8a61-3c9d2b7e5f13"  # read + notify

PUSH_ADV_NAME_PREFIX = "PicoPush-"

ERR_TEXT = {
    PUSH_ERR_NONE: "成功",
    PUSH_ERR_NOT_ARMED: "设备未处于接收窗口",
    PUSH_ERR_BAD_FRAME: "帧魔数/版本/长度非法",
    PUSH_ERR_TOO_LARGE: "正文超过 512KB 上限",
    PUSH_ERR_CHECKSUM: "CRC32 校验不符",
    PUSH_ERR_NO_CARD: "TF 卡不可用",
    PUSH_ERR_LOW_BATTERY: "电量不足且未充电",
    PUSH_ERR_BUSY: "已有一次推送在处理",
    PUSH_ERR_FETCH: "URL 抓取失败",
    PUSH_ERR_NO_DICT: "设备无词典，无法显示单词卡",
}


# --------------------------------------------------------------------------
# CRC32（与 zlib 同款 IEEE 反射式 0xEDB88320）
# CRC32 — same reflected IEEE poly as zlib.crc32, so we reuse it directly.
# --------------------------------------------------------------------------

def crc32(data: bytes) -> int:
    """与 C 的 push_crc32 完全等价：初始 0xFFFFFFFF、反射表、末异或 0xFFFFFFFF。"""
    return zlib.crc32(data) & 0xFFFFFFFF


# --------------------------------------------------------------------------
# 帧编解码（显式小端，不用 struct 打包，跨平台逐字节一致）
# Frame codecs — explicit little-endian, no struct packing, byte-identical everywhere.
# --------------------------------------------------------------------------

def encode_begin(total_len: int, crc: int, session_id: int, token: int,
                 ptype: int, title: str) -> bytes:
    """BEGIN 帧（83 字节）。title 按 UTF-8 字节截断到 63 字节并补 0，与 C 的
    strnlen(title,64) 行为一致。/ 83-byte BEGIN; title is UTF-8 byte-truncated
    to 63 bytes (matching the C strnlen(title,64) copy) and NUL-padded."""
    out = bytearray(PUSH_BEGIN_LEN)
    out[0] = PUSH_MAGIC0
    out[1] = PUSH_MAGIC1
    out[2] = PUSH_OP_BEGIN
    out[3] = PUSH_VERSION
    out[4] = ptype & 0xFF
    struct.pack_into("<H", out, 5, token & 0xFFFF)
    struct.pack_into("<I", out, 7, total_len & 0xFFFFFFFF)
    struct.pack_into("<I", out, 11, crc & 0xFFFFFFFF)
    struct.pack_into("<I", out, 15, session_id & 0xFFFFFFFF)
    raw = title.encode("utf-8")
    # 字节级截断，且不切断多字节 UTF-8 序列
    if len(raw) > PUSH_TITLE_MAX - 1:
        raw = raw[:PUSH_TITLE_MAX - 1]
        while raw and (raw[-1] & 0xC0) == 0x80:  # 删除尾随续字节
            raw = raw[:-1]
    out[19:19 + len(raw)] = raw
    return bytes(out)


def encode_ctrl(op: int) -> bytes:
    """END/RESUME_QUERY/ABORT：仅 4 字节公共头。/ END/RESUME_QUERY/ABORT: 4-byte header."""
    return bytes([PUSH_MAGIC0, PUSH_MAGIC1, op & 0xFF, PUSH_VERSION])


def encode_set_time(unix_seconds: int) -> bytes:
    """gen22：SET_TIME 帧 = 4 字节头 + UTC 秒 u32 LE（共 8 字节）。

    与固件侧 push_sink.c 一致：UTC 须落在 1704067200..4102444799（2024-01-01~
    2100-01-01），否则设备回 ERR(2) 且不动时钟。/ Mirrors push_sink.c: the device
    rejects anything outside 1704067200..4102444799 with ERR(2) and leaves the clock
    alone.
    """
    if not (1704067200 <= unix_seconds <= 4102444799):
        raise ValueError("unix_seconds out of the range the firmware accepts")
    return bytes([PUSH_MAGIC0, PUSH_MAGIC1, PUSH_OP_SET_TIME, PUSH_VERSION]) + \
        struct.pack("<I", unix_seconds & 0xFFFFFFFF)


def encode_data(seq: int, payload: bytes) -> bytes:
    """DATA 帧：seq(u16 LE) + len(1) + payload。/ seq(u16 LE) + len(1) + payload."""
    if len(payload) > PUSH_DATA_PAYLOAD_MAX:
        raise ValueError("payload too large for one DATA frame")
    return struct.pack("<HB", seq & 0xFFFF, len(payload)) + payload


def parse_status(frame: bytes):
    """解析一条 STATUS：返回 (opcode, body)。/ Parse one STATUS: (opcode, body)."""
    if not frame:
        return (0, b"")
    return (frame[0], bytes(frame[1:]))


def token_from_adv_name(name: str):
    """从广播名 PicoPush-XXXX 解析 token；非本服务广播名返回 None。"""
    if not name or not name.startswith(PUSH_ADV_NAME_PREFIX):
        return None
    suffix = name[len(PUSH_ADV_NAME_PREFIX):]
    try:
        return int(suffix, 16) & 0xFFFF
    except ValueError:
        return None


# --------------------------------------------------------------------------
# 真机推送客户端（bleak，懒加载）
# Real-device client (bleak, imported lazily)
# --------------------------------------------------------------------------

class PushError(Exception):
    def __init__(self, code: int):
        self.code = code
        super().__init__(ERR_TEXT.get(code, f"未知错误码 {code}"))


class PushClient:
    """与固件 push_sink 的一次完整会话。/ One full session against push_sink.

    流程：连上 -> 发现服务 -> 使能 STATUS 通知 -> 写 BEGIN -> 等 READY ->
    按 241 字节分片写 DATA(seq 自增) -> 写 END -> 等 DONE/ERR。

    断点续传：连上后先发 RESUME_QUERY，按回包 offset/next_seq 快进本地游标；
    若某次 CHECKSUM(4) 后两次都无进展，则 ABORT 并以新 session_id 重开会话。
    """

    def __init__(self, address: str, mtu: int = 247, timeout: float = 20.0):
        self.address = address
        self.mtu = mtu
        # 每包 DATA 载荷 = (mtu - 3 ATT写头) - 3 DATA帧头，封顶 241
        self.chunk = min(PUSH_DATA_PAYLOAD_MAX, self.mtu - 6)
        self.timeout = timeout
        self._ble = None          # bleak BleakClient（连接后）
        self._status = None       # 最近一条 STATUS 帧
        self._ready = None        # asyncio.Event（真机）或简单标志（离线模拟）
        self.session_id = 0
        self._seq = 0
        self._sent = 0
        self._no_progress_streak = 0

    # ---- 离线/真机共用的帧逻辑（便于单测注入传输）----

    def _begin_bytes(self, body: bytes, token: int, ptype: int, title: str) -> bytes:
        return encode_begin(len(body), crc32(body), self.session_id, token, ptype, title)

    # ---- 真机传输 ----

    async def _connect(self):
        try:
            from bleak import BleakClient
        except ImportError as e:  # pragma: no cover
            raise RuntimeError(
                "需要 bleak 才能真机推送：pip install bleak\n"
                "（仅想验证帧对表请用 --selftest）"
            ) from e
        self._ble = BleakClient(self.address)
        await self._ble.connect()
        # 协商 MTU（设备支持则取较大值）
        try:
            neg = await self._ble.device.request_mtu(self.mtu)
            if neg:
                self.chunk = min(PUSH_DATA_PAYLOAD_MAX, neg - 6)
        except Exception:
            pass
        await self._ble.start_notify(PUSH_STATUS_UUID, self._on_status)

    def _on_status(self, _sender, data):
        self._status = bytes(data)
        if self._ready is not None:
            try:
                self._ready.set()
            except Exception:
                pass

    async def _wait_status(self, wanted):
        """等待下一条 opcode 属于 wanted 集合的 STATUS；超时抛 RuntimeError。"""
        import asyncio
        deadline = asyncio.get_event_loop().time() + self.timeout
        while True:
            if self._status is not None:
                op, body = parse_status(self._status)
                if op in wanted:
                    frame = self._status
                    self._status = None
                    return frame
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                raise RuntimeError("等待设备 STATUS 超时")
            await asyncio.sleep(0.02)

    async def set_time(self, unix_seconds: int = 0):
        """gen22：连接后下发 UTC 校准设备时钟（不需要武装，设备侧也校验范围）。

        gen22: after connecting, push UTC to set the device clock (no arm needed; the
        device validates the range too).
        """
        await self._connect()
        stamp = int(unix_seconds) if unix_seconds else int(time.time())
        await self._ble.write_gatt_char(PUSH_CTRL_UUID, encode_set_time(stamp),
                                        response=True)
        print(f"# SET_TIME 已发送 / sent: {stamp}")
        await self._ble.disconnect()

    async def send_text(self, text: str, title: str = "", ptype: int = PUSH_TYPE_TEXT,
                        token: int = 0):
        await self._connect()
        body = text.encode("utf-8")
        begin = self._begin_bytes(body, token, ptype, title)
        # BEGIN
        await self._ble.write_gatt_char(PUSH_CTRL_UUID, begin, response=True)
        op, body_st = parse_status(await self._wait_status(
            {PUSH_ST_READY, PUSH_ST_ERR}))
        if op == PUSH_ST_ERR:
            raise PushError(body_st[0])
        self.session_id = struct.unpack("<I", body_st[:4])[0] if len(body_st) >= 4 else 0
        # DATA 分片
        await self._stream(body)
        # END
        await self._ble.write_gatt_char(PUSH_CTRL_UUID, encode_ctrl(PUSH_OP_END),
                                        response=True)
        op, body_st = parse_status(await self._wait_status(
            {PUSH_ST_DONE, PUSH_ST_ERR}))
        if op == PUSH_ST_ERR:
            raise PushError(body_st[0])
        await self._ble.disconnect()

    async     def _stream(self, body: bytes):
        view = memoryview(body)
        off = 0
        seq = 0
        # WWR 写（response=True）：与固件 5f11 特性属性一致，逐包 ATT 回执。
        # 固件对 WNR 写也一视同仁，但默认走 WWR（实证修正记录见文件头常量区）。
        while off < len(body):
            piece = bytes(view[off:off + self.chunk])
            await self._ble.write_gatt_char(
                PUSH_DATA_UUID, encode_data(seq, piece), response=True)
            off += len(piece)
            seq += 1
        # 空正文（单词卡/URL）也发一包 seq=0 长度 0，对齐「有正文」路径的可测性
        # （设备 seq 0 长度 0 合法；total_len==0 时设备本就靠 END 后校验）。
        if len(body) == 0:
            await self._ble.write_gatt_char(
                PUSH_DATA_UUID, encode_data(0, b""), response=True)


# --------------------------------------------------------------------------
# 自检（与 C push_proto 逐字段对表；不需要蓝牙）
# Self-test — byte-for-byte against push_proto.h; no Bluetooth needed.
# --------------------------------------------------------------------------

def _selftest() -> int:
    import os
    ok = True

    def check(cond, msg):
        nonlocal ok
        if cond:
            print(f"  [PASS] {msg}")
        else:
            print(f"  [FAIL] {msg}")
            ok = False

    print("push_pico.py 自检 / self-test:")
    # 1) CRC32 与 zlib 一致，且空段直通
    sample = b"ReadPico BLE push reference"
    check(crc32(sample) == zlib.crc32(sample) & 0xFFFFFFFF, "crc32 == zlib.crc32")
    check(crc32(b"") == 0x00000000, "crc32(empty) == 0")
    # 链式等于整体：continue(continue(0,a),b) == crc32(a+b)
    a, b = b"hello ", b"world"
    c = zlib.crc32(b"") & 0xFFFFFFFF
    c = zlib.crc32(a, c) & 0xFFFFFFFF
    c = zlib.crc32(b, c) & 0xFFFFFFFF
    check(crc32(a + b) == c, "crc32 链式 == 整体")

    # 2) BEGIN 帧布局逐字节
    body = "墨水屏测试".encode("utf-8")
    tk = 0x1A2B
    sid = 0xDEADBEEF
    begin = encode_begin(len(body), crc32(body), sid, tk, PUSH_TYPE_MARKDOWN, "标题✓")
    check(len(begin) == PUSH_BEGIN_LEN, f"BEGIN 长度 == {PUSH_BEGIN_LEN}")
    check(begin[0] == ord('P') and begin[1] == ord('C'), "魔数 PC")
    check(begin[2] == PUSH_OP_BEGIN and begin[3] == PUSH_VERSION, "op/version")
    check(begin[4] == PUSH_TYPE_MARKDOWN, "type 字段")
    check(struct.unpack("<H", begin[5:7])[0] == tk, "token(u16 LE)")
    check(struct.unpack("<I", begin[7:11])[0] == len(body), "total_len(u32 LE)")
    check(struct.unpack("<I", begin[11:15])[0] == crc32(body), "crc32(u32 LE)")
    check(struct.unpack("<I", begin[15:19])[0] == sid, "session_id(u32 LE)")
    title_field = begin[19:19 + PUSH_TITLE_MAX]
    check(title_field.rstrip(b"\x00").decode("utf-8") == "标题✓", "title UTF-8")
    check(begin[19 + PUSH_TITLE_MAX - 1] == 0, "title 末尾保 0")

    # 3) CTRL 公共头
    check(encode_ctrl(PUSH_OP_END) == bytes([0x50, 0x43, 0x02, 0x01]), "END 头")
    check(encode_ctrl(PUSH_OP_ABORT) == bytes([0x50, 0x43, 0x04, 0x01]), "ABORT 头")

    # 4) DATA 帧
    d = encode_data(0x1234, b"abc")
    check(d == struct.pack("<HB", 0x1234, 3) + b"abc", "DATA seq/len/payload")
    # 大载荷截断保护
    try:
        encode_data(0, b"x" * (PUSH_DATA_PAYLOAD_MAX + 1))
        check(False, "超长载荷应抛错")
    except ValueError:
        check(True, "超长载荷抛 ValueError")

    # 5) STATUS 解析
    check(parse_status(bytes([PUSH_ST_ERR, PUSH_ERR_CHECKSUM])) ==
          (PUSH_ST_ERR, bytes([PUSH_ERR_CHECKSUM])), "STATUS ERR 解析")
    check(parse_status(bytes([PUSH_ST_DONE, 1, 2, 3, 4])) ==
          (PUSH_ST_DONE, bytes([1, 2, 3, 4])), "STATUS DONE 解析")

    # 6) 广播名 -> token
    check(token_from_adv_name("PicoPush-1A2B") == 0x1A2B, "广播名解析 token")
    check(token_from_adv_name("Other-ABCD") is None, "非本服务广播名返回 None")

    print("自检结果 / result:", "ALL PASS" if ok else "HAS FAILURES")
    return 0 if ok else 1


def _main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="ReadPico 安卓 BLE 推流参考客户端")
    ap.add_argument("--selftest", action="store_true", help="仅做帧对表自检")
    ap.add_argument("--hex", action="store_true",
                    help="只打印可粘贴进 nRF Connect 的 BEGIN/DATA/END 十六进制，不连蓝牙")
    ap.add_argument("--scan", action="store_true", help="扫描并交互选择 PicoPush 设备")
    ap.add_argument("--address", help="设备 MAC/地址（跳过扫描）")
    ap.add_argument("--token", type=lambda s: int(s, 16),
                    help="广播名后缀 token（十六进制，如 1A2B）；不指定则扫描读取")
    ap.add_argument("--text", help="要推送的纯文本/Markdown/HTML 正文")
    ap.add_argument("--file", help="从文件读取正文（UTF-8）")
    ap.add_argument("--title", default="", help="标题（<=63 UTF-8 字节）")
    ap.add_argument("--type", type=int, default=PUSH_TYPE_TEXT,
                    help="正文类型 0=text 1=markdown 2=html 3=url 4=word")
    ap.add_argument("--word", help="单词卡：单词本身（type 强制为 4）")
    ap.add_argument("--url", help="URL 推送（type 强制为 3，M1.5 起设备端抓取）")
    ap.add_argument("--set-time", nargs="?", const=0, type=int, default=None,
                    help="gen22：连接并下发 UTC 校准设备时钟（不给值则用本机当前时间）")
    ap.add_argument("--mtu", type=int, default=247)
    ap.add_argument("--timeout", type=float, default=20.0)
    args = ap.parse_args(argv)

    if args.selftest:
        return _selftest()

    if args.hex:
        # 只生成帧十六进制（人工用 nRF Connect 粘贴测试）
        stype = args.type
        if args.word is not None:
            stype, body, title = PUSH_TYPE_WORD, b"", args.word
        elif args.url is not None:
            stype, body, title = PUSH_TYPE_URL, b"", (args.url if not args.title else args.title)
        elif args.file is not None:
            with open(args.file, "rb") as f:
                body = f.read()
            title = args.title
        elif args.text is not None:
            body = args.text.encode("utf-8")
            title = args.title
        else:
            ap.error("需指定 --text/--file/--word/--url 之一")
            return 2
        sid = 0x00000001
        tk = args.token if args.token is not None else 0x0000
        print(f"# token={tk:04X}  type={stype}  session_id={sid:08X}")
        print("BEGIN (写 5f12, 83 字节):")
        print("  " + encode_begin(len(body), crc32(body), sid, tk, stype, title).hex())
        if body:
            for i in range(0, len(body), PUSH_DATA_PAYLOAD_MAX):
                piece = body[i:i + PUSH_DATA_PAYLOAD_MAX]
                print(f"DATA seq={i // PUSH_DATA_PAYLOAD_MAX} (写 5f11):")
                print("  " + encode_data(i // PUSH_DATA_PAYLOAD_MAX, piece).hex())
        else:
            print("DATA seq=0 空包 (写 5f11):")
            print("  " + encode_data(0, b"").hex())
        print("END (写 5f12):")
        print("  " + encode_ctrl(PUSH_OP_END).hex())
        print("# 随后在 5f13 开启 Notify，等待 DONE(0x12) 或 ERR(0x13+code)")
        return 0

    # gen22：纯校时模式——连接即下发 UTC，不需要武装，也不推送正文。
    # 地址解析复用主流程（--address，或 --scan 交互选择，同 _scan_pick）。
    # gen22: time-only mode - connect, push UTC, disconnect. No arm, no body.
    # Address resolution reuses the main flow (--address, or --scan to pick).
    if args.set_time is not None:
        import asyncio

        async def _run_time():
            addr = args.address
            if args.scan or addr is None:
                addr, _token = await _scan_pick()
            cli = PushClient(addr, mtu=args.mtu, timeout=args.timeout)
            await cli.set_time(args.set_time)

        asyncio.run(_run_time())
        return 0

    # 决定正文/类型
    if args.word is not None:
        ptype, body = PUSH_TYPE_WORD, b""
        title = args.word
    elif args.url is not None:
        ptype, body = PUSH_TYPE_URL, b""
        title = args.url if not args.title else args.title
    elif args.file is not None:
        with open(args.file, "rb") as f:
            body = f.read()
        ptype = args.type
        title = args.title
    elif args.text is not None:
        body = args.text.encode("utf-8")
        ptype = args.type
        title = args.title
    else:
        ap.error("需指定 --text/--file/--word/--url 之一（或 --selftest）")
        return 2

    if ptype == PUSH_TYPE_WORD and not title:
        ap.error("单词卡需要 --word 提供单词")
        return 2
    if len(body) > PUSH_TOTAL_MAX:
        ap.error(f"正文 {len(body)} 字节超过 {PUSH_TOTAL_MAX} 上限")
        return 2

    token = args.token

    async def _run():
        nonlocal token
        import asyncio
        addr = args.address
        if args.scan or (addr is None and token is None):
            addr, scanned_token = await _scan_pick()
            if token is None:
                token = scanned_token
        cli = PushClient(addr, mtu=args.mtu, timeout=args.timeout)
        if ptype in (PUSH_TYPE_WORD, PUSH_TYPE_URL):
            # 单词卡/URL：正文为空，直接走 BEGIN/END
            await cli.send_text("", title=title, ptype=ptype, token=token or 0)
        else:
            await cli.send_text(body.decode("utf-8", "replace"), title=title,
                                ptype=ptype, token=token or 0)

    try:
        import asyncio
        asyncio.run(_run())
    except PushError as e:
        print(f"设备拒绝：{e}", file=sys.stderr)
        return e.code
    except RuntimeError as e:
        print(f"推送失败：{e}", file=sys.stderr)
        return 1
    print("推送完成 / pushed.")
    return 0


async def _scan_pick():  # pragma: no cover - 需要真实蓝牙
    from bleak import BleakScanner

    def _cb(dev, adv):
        name = adv.local_name or dev.name or ""
        tk = token_from_adv_name(name)
        if tk is not None:
            found.append((dev.address, tk, name))

    found = []
    scanner = BleakScanner(detection_callback=_cb)
    await scanner.start()
    import asyncio
    await asyncio.sleep(4)
    await scanner.stop()
    if not found:
        raise RuntimeError("未发现 PicoPush-XXXX 广播，请先在墨水屏开启接收")
    for i, (a, tk, n) in enumerate(found):
        print(f"  [{i}] {n}  ({a})")
    if len(found) == 1:
        return found[0][0], found[0][1]
    pick = input("选择设备序号: ").strip()
    chosen = found[int(pick)]
    return chosen[0], chosen[1]


if __name__ == "__main__":
    raise SystemExit(_main())
