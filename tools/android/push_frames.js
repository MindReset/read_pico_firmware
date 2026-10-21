// SPDX-FileCopyrightText: 2026 mindreset
// SPDX-License-Identifier: Apache-2.0
//
// 中文：ReadPico BLE 推流的「帧构建 JS」——在 Tasker(MacroDroid) 的 JavaScript
// 动作里运行，把一段文字/单词/URL 变成可写入 GATT 的 BEGIN/DATA/CTRL 字节。
// 算法与 tools/android/push_pico.py 一字不差（CRC32 = IEEE 反射式 0xEDB88320）。
// 这段 JS 同时被 ReadPicoPush.tasker.xml 与 ReadPicoPush.macro 复用。
//
// English: the frame-building JS for ReadPico BLE push, run inside Tasker's /
// MacroDroid's JavaScript action to turn text/word/URL into GATT-ready
// BEGIN/DATA/CTRL bytes. Algorithm is byte-identical to push_pico.py (CRC32 is
// the reflected IEEE 0xEDB88320). Shared by ReadPicoPush.tasker.xml and .macro.

/* CRC32 —— 与 zlib 同款（初始 0xFFFFFFFF、反射表、末异或）。*/
function pushCrc32(bytes) {
  let c = ~0 >>> 0;
  for (let i = 0; i < bytes.length; i++) {
    c ^= bytes[i];
    for (let k = 0; k < 8; k++) {
      c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1);
    }
  }
  return (~c) >>> 0;
}

function le16(n) { return [n & 0xFF, (n >>> 8) & 0xFF]; }
function le32(n) {
  return [n & 0xFF, (n >>> 8) & 0xFF, (n >>> 16) & 0xFF, (n >>> 24) & 0xFF];
}

/* UTF-8 编码（不依赖 TextEncoder 也行，这里优先用原生）。*/
function utf8(str) {
  if (typeof TextEncoder !== "undefined") return Array.from(new TextEncoder().encode(str));
  const out = [];
  for (let i = 0; i < str.length; i++) {
    let cp = str.codePointAt(i);
    if (cp > 0xFFFF) i++;
    if (cp < 0x80) out.push(cp);
    else if (cp < 0x800) { out.push(0xC0 | (cp >> 6), 0x80 | (cp & 0x3F)); }
    else { out.push(0xE0 | (cp >> 12), 0x80 | ((cp >> 6) & 0x3F), 0x80 | (cp & 0x3F)); }
  }
  return out;
}

const PUSH_TITLE_MAX = 64;
const PUSH_DATA_PAYLOAD_MAX = 241;
const PUSH_MAGIC0 = 0x50, PUSH_MAGIC1 = 0x43, PUSH_VERSION = 0x01;
const PUSH_OP_BEGIN = 0x01, PUSH_OP_END = 0x02, PUSH_OP_RESUME_QUERY = 0x03, PUSH_OP_ABORT = 0x04;

/* 构造一次推送的全部帧。返回 {begin:number[], chunks:number[][], ctrlEnd:number[]}。
   bodyBytes 为正文字节数组（单词卡/URL 传 []）；token/type/sessionId 为整数。*/
function buildPush(opts) {
  const { bodyBytes, token, type, title, sessionId } = opts;
  const crc = pushCrc32(bodyBytes);
  const begin = [
    PUSH_MAGIC0, PUSH_MAGIC1, PUSH_OP_BEGIN, PUSH_VERSION,
    type & 0xFF,
    ...le16(token & 0xFFFF),
    ...le32(bodyBytes.length),
    ...le32(crc),
    ...le32(sessionId & 0xFFFFFFFF),
  ];
  const t = utf8(title || "").slice(0, PUSH_TITLE_MAX - 1);
  while (begin.length < 19 + PUSH_TITLE_MAX) begin.push(0);
  // 标题逐字节填入 out[19..]
  const out = begin.slice(0, 19).concat(t, new Array(19 + PUSH_TITLE_MAX - 19 - t.length).fill(0));
  // 补足到 83
  while (out.length < 83) out.push(0);

  const chunks = [];
  let seq = 0;
  for (let off = 0; off < bodyBytes.length; off += PUSH_DATA_PAYLOAD_MAX) {
    const piece = bodyBytes.slice(off, off + PUSH_DATA_PAYLOAD_MAX);
    chunks.push([...le16(seq & 0xFFFF), piece.length, ...piece]);
    seq++;
  }
  if (bodyBytes.length === 0) chunks.push([...le16(0), 0]); // 空正文也发一包 seq0 长度0
  const ctrlEnd = [PUSH_MAGIC0, PUSH_MAGIC1, PUSH_OP_END, PUSH_VERSION];
  return { begin: out.slice(0, 83), chunks, ctrlEnd };
}

/* 字节数组转十六进制串（GATT 写入常需 hex 文本）。*/
function toHex(bytes) { return bytes.map(b => b.toString(16).padStart(2, "0")).join(""); }

// 浏览器/Node 双环境导出
if (typeof module !== "undefined") module.exports = { pushCrc32, buildPush, toHex, utf8 };
