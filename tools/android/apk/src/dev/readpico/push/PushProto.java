/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：ReadPico 安卓 BLE 推流的协议编解码（纯 Java，无 Android 依赖）。
 * 本文件是 components/push_sink/include/push_proto.h 与 tools/android/push_pico.py
 * 的逐字段对表：帧布局、CRC32、小端字节序全部一致，宿主 main() 测试
 * （test/PushProtoTest.java）用 Python 参考实现生成的向量逐字节断言。
 *
 * English: frame codec for the ReadPico BLE push. Pure Java, zero Android
 * imports, so it can be verified on the host JVM against the vectors emitted
 * by push_pico.py (the reference implementation).
 *
 * 冻结：帧布局与 push_proto.h 完全一致，单方面改字段会破坏已发货固件。
 * Frozen: byte-identical to push_proto.h; unilateral changes break shipped firmware.
 */
package dev.readpico.push;

import java.nio.charset.Charset;
import java.util.zip.CRC32;

public final class PushProto {
    private PushProto() {}

    /* ---- 常量（与 push_proto.h / push_pico.py 一字不差） ---- */

    public static final int MAGIC0 = 0x50; // 'P'
    public static final int MAGIC1 = 0x43; // 'C'
    public static final int VERSION = 0x01;

    public static final int TITLE_MAX = 64;          // 标题 UTF-8 上限（含结尾 0）
    public static final int DATA_PAYLOAD_MAX = 241;  // MTU 247 时每包载荷
    public static final int TOTAL_MAX = 512 * 1024;  // 正文硬上限；超了 ERR(3)

    public static final int OP_BEGIN = 0x01;
    public static final int OP_END = 0x02;
    public static final int OP_RESUME_QUERY = 0x03;
    public static final int OP_ABORT = 0x04;
    // gen22：手机下发 UTC 秒给设备校时（不受武装限制，连上即可）。
    // gen22: the phone pushes a UTC second count to set the device clock (not
    // gated on an armed window - any connection can carry it).
    public static final int OP_SET_TIME = 0x05;

    public static final int ST_READY = 0x10;
    public static final int ST_RESUME = 0x11;
    public static final int ST_DONE = 0x12;
    public static final int ST_ERR = 0x13;
    public static final int ST_EVT_READ = 0x14;
    public static final int ST_BATT = 0x15;
    public static final int ST_PROGRESS = 0x16;

    public static final int TYPE_TEXT = 0;
    public static final int TYPE_MARKDOWN = 1;
    public static final int TYPE_HTML = 2;
    public static final int TYPE_URL = 3;
    public static final int TYPE_WORD = 4;

    public static final int ERR_NONE = 0;
    public static final int ERR_NOT_ARMED = 1;
    public static final int ERR_BAD_FRAME = 2;
    public static final int ERR_TOO_LARGE = 3;
    public static final int ERR_CHECKSUM = 4;
    public static final int ERR_NO_CARD = 5;
    public static final int ERR_LOW_BATTERY = 6;
    public static final int ERR_BUSY = 7;
    public static final int ERR_FETCH = 8;
    public static final int ERR_NO_DICT = 9;

    public static final int BEGIN_LEN = 83;

    /* GATT 服务与特征（草案 3.1，写死；实证修正记录见 push_pico.py 常量区：
     * 固件 5f11/5f12 特性位一直是 F_WRITE（WWR 写请求），F_WRITE_NO_RSP 从未出现，
     * 本客户端同样默认 WWR：ATT 逐包回执是第一道防线，设备侧 RESUME 是第二道）。 */
    public static final String SERVICE_UUID = "e2f5a1c0-9b7d-4e3f-8a61-3c9d2b7e5f10";
    public static final String DATA_UUID    = "e2f5a1c0-9b7d-4e3f-8a61-3c9d2b7e5f11"; // WWR 写 DATA 帧
    public static final String CTRL_UUID    = "e2f5a1c0-9b7d-4e3f-8a61-3c9d2b7e5f12"; // WWR 写 BEGIN/END
    public static final String STATUS_UUID  = "e2f5a1c0-9b7d-4e3f-8a61-3c9d2b7e5f13"; // read + notify
    public static final String CCCD_UUID    = "00002902-0000-1000-8000-00805f9b34fb";

    public static final String ADV_NAME_PREFIX = "PicoPush-";

    private static final Charset UTF8 = Charset.forName("UTF-8");

    /* ---- CRC32（与 zlib.crc32 / C push_crc32 完全一致） ---- */

    /** 整段 CRC32；返回值按无符号 32 位解释（Java int 可能为负，比较时注意）。 */
    public static int crc32(byte[] data) {
        CRC32 c = new CRC32();
        c.update(data);
        return (int) c.getValue();
    }

    public static int crc32(byte[] data, int off, int len) {
        CRC32 c = new CRC32();
        c.update(data, off, len);
        return (int) c.getValue();
    }

    /* ---- BEGIN 帧（83 字节） ---- */

    /**
     * 序列化 BEGIN：magic(2) + op(1) + ver(1) + type(1) + token(u16 LE) +
     * total_len(u32 LE) + crc32(u32 LE) + session_id(u32 LE) + title[64]。
     * title 按 UTF-8 字节截断到 63 并剥离尾随续字节（与 C 的 strnlen 行为一致）。
     */
    /**
     * gen22：SET_TIME 帧（8 字节）= 帧头 4B + UTC 秒 u32 LE。
     * BEGIN 帧布局保持冻结，故校时走独立操作码；老固件收到未知 op 会拒绝，
     * 不影响既有推送链。/ BEGIN stays frozen, so time sync uses its own opcode;
     * older firmware rejects the unknown op without disturbing the push chain.
     */
    public static byte[] encodeSetTime(long unixSeconds) {
        byte[] out = new byte[8];
        out[0] = (byte) MAGIC0;
        out[1] = (byte) MAGIC1;
        out[2] = (byte) OP_SET_TIME;
        out[3] = (byte) VERSION;
        putU32(out, 4, unixSeconds);
        return out;
    }

    public static byte[] encodeBegin(long totalLen, long crc, long sessionId,
                                     int token, int ptype, String title) {
        byte[] out = new byte[ BEGIN_LEN ];
        out[0] = (byte) MAGIC0;
        out[1] = (byte) MAGIC1;
        out[2] = (byte) OP_BEGIN;
        out[3] = (byte) VERSION;
        out[4] = (byte) ptype;
        putU16(out, 5, token);
        putU32(out, 7, totalLen);
        putU32(out, 11, crc);
        putU32(out, 15, sessionId);
        byte[] raw = title == null ? new byte[0] : title.getBytes(UTF8);
        if (raw.length > TITLE_MAX - 1) {
            raw = copyOf(raw, TITLE_MAX - 1);
            while (raw.length > 0 && (raw[raw.length - 1] & 0xC0) == 0x80) {
                raw = copyOf(raw, raw.length - 1); // 删除尾随 UTF-8 续字节
            }
        }
        System.arraycopy(raw, 0, out, 19, raw.length);
        return out;
    }

    /* ---- CTRL 帧（END / RESUME_QUERY / ABORT：4 字节公共头） ---- */

    public static byte[] encodeCtrl(int op) {
        return new byte[] { (byte) MAGIC0, (byte) MAGIC1, (byte) op, (byte) VERSION };
    }

    /* ---- DATA 帧：seq(u16 LE) + len(1) + payload ---- */

    public static byte[] encodeData(int seq, byte[] payload) {
        if (payload.length > DATA_PAYLOAD_MAX) {
            throw new IllegalArgumentException("payload too large for one DATA frame");
        }
        byte[] out = new byte[3 + payload.length];
        putU16(out, 0, seq);
        out[2] = (byte) payload.length;
        System.arraycopy(payload, 0, out, 3, payload.length);
        return out;
    }

    /* ---- STATUS 帧 ---- */

    /** 取 opcode；空帧返回 0。 */
    public static int statusOp(byte[] frame) {
        return (frame == null || frame.length == 0) ? 0 : frame[0] & 0xFF;
    }

    /** 取 body（opcode 之后的字节）。 */
    public static byte[] statusBody(byte[] frame) {
        if (frame == null || frame.length <= 1) {
            return new byte[0];
        }
        return copyOf(frame, frame.length - 1, 1);
    }

    /** READY 帧 body 前 4 字节是设备侧 session_id（小端）；不足 4 字节返回 -1。 */
    public static long readySessionId(byte[] frame) {
        byte[] body = statusBody(frame);
        if (body.length < 4) {
            return -1L;
        }
        return (getU32(body, 0)) & 0xFFFFFFFFL;
    }

    /**
     * RESUME 帧 body：offset(u32 LE) + next_seq(u16 LE)（gen14 新增解析）。
     * offset 是设备 .part 里已落盘的字节数；不足 4 字节返回 -1。
     * RESUME body: offset (u32 LE) + next_seq (u16 LE). The offset is the number
     * of bytes already flushed to the device's partial file.
     */
    public static long resumeOffset(byte[] frame) {
        byte[] body = statusBody(frame);
        if (body.length < 4) {
            return -1L;
        }
        return getU32(body, 0) & 0xFFFFFFFFL;
    }

    /** RESUME 帧 body 的 next_seq（u16 LE，偏移 4）；不足 6 字节返回 -1。 */
    public static long resumeNextSeq(byte[] frame) {
        byte[] body = statusBody(frame);
        if (body.length < 6) {
            return -1L;
        }
        return (body[4] & 0xFF) | ((body[5] & 0xFF) << 8);
    }

    /* ---- 广播名 -> token ---- */

    /** 从广播名 PicoPush-XXXX 解析 token（十六进制后缀）；非本服务广播名返回 null。 */
    public static Integer tokenFromAdvName(String name) {
        if (name == null || !name.startsWith(ADV_NAME_PREFIX)) {
            return null;
        }
        String suffix = name.substring(ADV_NAME_PREFIX.length());
        if (suffix.length() == 0) {
            return null;
        }
        try {
            return (int) Long.parseLong(suffix, 16) & 0xFFFF;
        } catch (NumberFormatException e) {
            return null;
        }
    }

    /* ---- 错误码文案 ---- */

    public static String errText(int code) {
        switch (code) {
            case ERR_NONE:        return "成功";
            case ERR_NOT_ARMED:   return "设备未处于接收窗口";
            case ERR_BAD_FRAME:   return "帧魔数/版本/长度非法";
            case ERR_TOO_LARGE:   return "正文超过 512KB 上限";
            case ERR_CHECKSUM:    return "CRC32 校验不符";
            // gen16：ERR(5) 的语义不止「TF 卡不可用」——固件 do_commit 的 rename
            // 失败（旧版 FatFs 不覆盖目标名时）也回它。文案写宽，别误导用户去查卡。
            // gen16: ERR(5) means more than "card unavailable" - the firmware's
            // commit rename failure also answers it. Keep the label broad so
            // users don't chase the wrong cause.
            case ERR_NO_CARD:     return "设备存储写入失败（TF 卡缺失、写保护或提交失败）";
            case ERR_LOW_BATTERY: return "电量不足且未充电";
            case ERR_BUSY:        return "已有一次推送在处理";
            case ERR_FETCH:       return "URL 抓取失败";
            case ERR_NO_DICT:     return "设备无词典，无法显示单词卡";
            default:              return "未知错误码 " + code;
        }
    }

    /* ---- 分片大小 ---- */

    /** 每包 DATA 载荷 = min(241, mtu - 3 ATT 写头 - 3 帧头)。 */
    public static int chunkForMtu(int mtu) {
        return Math.min(DATA_PAYLOAD_MAX, mtu - 6);
    }

    /* ---- 小工具 ---- */

    private static void putU16(byte[] out, int off, int v) {
        out[off] = (byte) (v & 0xFF);
        out[off + 1] = (byte) ((v >> 8) & 0xFF);
    }

    private static void putU32(byte[] out, int off, long v) {
        out[off] = (byte) (v & 0xFF);
        out[off + 1] = (byte) ((v >> 8) & 0xFF);
        out[off + 2] = (byte) ((v >> 16) & 0xFF);
        out[off + 3] = (byte) ((v >> 24) & 0xFF);
    }

    private static int getU32(byte[] in, int off) {
        return (in[off] & 0xFF) | ((in[off + 1] & 0xFF) << 8)
                | ((in[off + 2] & 0xFF) << 16) | ((in[off + 3] & 0xFF) << 24);
    }

    private static byte[] copyOf(byte[] src, int len) {
        byte[] out = new byte[len];
        System.arraycopy(src, 0, out, 0, Math.min(len, src.length));
        return out;
    }

    private static byte[] copyOf(byte[] src, int len, int off) {
        byte[] out = new byte[len];
        System.arraycopy(src, off, out, 0, Math.min(len, src.length - off));
        return out;
    }
}
