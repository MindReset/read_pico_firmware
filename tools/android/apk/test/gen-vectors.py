#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
#
# 中文：生成 PushProtoTest.java 用的期望向量。期望值全部由本仓库的参考实现
# push_pico.py 产生（帧布局与固件 push_proto.h 逐字段对表），Java 侧只做断言，
# 不手抄任何字节。修改协议字段时必须同步重跑本脚本。
#
# English: emit the expectation vectors consumed by PushProtoTest.java. Every
# value comes from push_pico.py (the reference implementation); the Java side
# only asserts, so no bytes are ever hand-transcribed.
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
import push_pico as pp  # noqa: E402

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "vectors.txt")


def main() -> int:
    v = {}

    # CRC32
    v["crc_sample"] = pp.crc32(b"ReadPico BLE push reference")
    v["crc_empty"] = pp.crc32(b"")

    # CTRL 公共头
    v["end"] = pp.encode_ctrl(pp.PUSH_OP_END).hex()
    v["abort"] = pp.encode_ctrl(pp.PUSH_OP_ABORT).hex()

    # DATA 帧
    v["data_abc"] = pp.encode_data(0x1234, b"abc").hex()
    v["data_empty"] = pp.encode_data(0, b"").hex()

    # BEGIN 帧（三种典型 + 长标题截断）
    body = "墨水屏测试".encode("utf-8")
    v["begin_md"] = pp.encode_begin(
        len(body), pp.crc32(body), 0xDEADBEEF, 0x1A2B, pp.PUSH_TYPE_MARKDOWN, "标题✓").hex()
    txt = "ReadPico BLE push reference".encode("utf-8")
    v["begin_text_sid1"] = pp.encode_begin(
        len(txt), pp.crc32(txt), 1, 0x1A2B, pp.PUSH_TYPE_TEXT, "来自手机").hex()
    v["begin_word"] = pp.encode_begin(
        0, 0, 1, 0x1A2B, pp.PUSH_TYPE_WORD, "serendipity").hex()
    long_title = ("🎉" * 40) + ("汉" * 30)
    lb = pp.encode_begin(3, pp.crc32(b"abc"), 1, 0x1A2B, pp.PUSH_TYPE_TEXT, long_title)
    v["begin_long"] = lb.hex()
    # 标题字段实占字节数（63 字节截断 + 尾随续字节剥离后的结果）
    v["long_title_len"] = len(lb[19:19 + 64].rstrip(b"\x00"))

    # 广播名 -> token
    v["token_ok"] = pp.token_from_adv_name("PicoPush-1A2B")
    v["token_bad"] = "" if pp.token_from_adv_name("Other-ABCD") is None else "NOT_NULL"

    # 多分片：1230 字节 / 6 包，seq 从 0 递增（客户端协议语义）
    big = ("推流测试数据块-" * 100).encode("utf-8")[:1230]
    v["big_len"] = len(big)
    v["big_crc"] = pp.crc32(big)
    v["big_first"] = pp.encode_data(0, big[0:241]).hex()
    v["big_last"] = pp.encode_data(5, big[5 * 241:1230]).hex()
    v["chunk_247"] = min(pp.PUSH_DATA_PAYLOAD_MAX, 247 - 6)
    v["chunk_23"] = min(pp.PUSH_DATA_PAYLOAD_MAX, 23 - 6)

    with open(OUT, "w", encoding="utf-8") as f:
        for k in sorted(v):
            f.write(f"{k}={v[k]}\n")
    print(f"wrote {OUT} ({len(v)} vectors)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
