/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：PushProto 的宿主机对表测试。期望向量由 gen-vectors.py 从参考实现
 * push_pico.py 生成到 vectors.txt（不手抄字节），本测试只读取并断言。
 * 运行：python3 gen-vectors.py && javac -cp <PushProto.class 所在目录> PushProtoTest.java && java PushProtoTest
 */
import dev.readpico.push.PushProto;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.charset.Charset;
import java.util.HashMap;
import java.util.Map;

public final class PushProtoTest {
    private static final Charset UTF8 = Charset.forName("UTF-8");
    private static int pass = 0;
    private static int fail = 0;

    private static void check(boolean cond, String msg) {
        if (cond) {
            pass++;
            System.out.println("  [PASS] " + msg);
        } else {
            fail++;
            System.out.println("  [FAIL] " + msg);
        }
    }

    private static String hex(byte[] b) {
        StringBuilder sb = new StringBuilder(b.length * 2);
        for (int i = 0; i < b.length; i++) {
            sb.append(String.format("%02x", b[i] & 0xFF));
        }
        return sb.toString();
    }

    private static Map<String, String> vectors() throws IOException {
        Map<String, String> m = new HashMap<String, String>();
        InputStream in = PushProtoTest.class.getResourceAsStream("/vectors.txt");
        if (in == null) {
            throw new IOException("vectors.txt not found on classpath (run gen-vectors.py first)");
        }
        BufferedReader r = new BufferedReader(new InputStreamReader(in, UTF8));
        String line;
        while ((line = r.readLine()) != null) {
            int eq = line.indexOf('=');
            if (eq > 0) {
                m.put(line.substring(0, eq), line.substring(eq + 1));
            }
        }
        r.close();
        return m;
    }

    private static int vecInt(Map<String, String> v, String key) {
        return (int) Long.parseLong(v.get(key).trim());
    }

    private static String buildLongTitle() {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < 40; i++) {
            sb.append("🎉"); // U+1F389
        }
        for (int i = 0; i < 30; i++) {
            sb.append("汉");
        }
        return sb.toString();
    }

    private static byte[] bigBody() {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < 100; i++) {
            sb.append("推流测试数据块-");
        }
        byte[] all = sb.toString().getBytes(UTF8);
        byte[] out = new byte[1230];
        System.arraycopy(all, 0, out, 0, 1230);
        return out;
    }

    public static void main(String[] args) throws Exception {
        Map<String, String> v = vectors();
        System.out.println("PushProtoTest（对表 push_pico.py，向量来自 vectors.txt）:");

        // 1) CRC32
        check(PushProto.crc32("ReadPico BLE push reference".getBytes(UTF8)) == vecInt(v, "crc_sample"),
                "crc32(sample) == " + v.get("crc_sample"));
        check(PushProto.crc32(new byte[0]) == vecInt(v, "crc_empty"), "crc32(empty) == 0");

        // 2) CTRL 公共头
        check(hex(PushProto.encodeCtrl(PushProto.OP_END)).equals(v.get("end")), "END 头");
        check(hex(PushProto.encodeCtrl(PushProto.OP_ABORT)).equals(v.get("abort")), "ABORT 头");

        // 3) DATA 帧
        check(hex(PushProto.encodeData(0x1234, "abc".getBytes(UTF8))).equals(v.get("data_abc")),
                "DATA seq=0x1234 payload=abc");
        check(hex(PushProto.encodeData(0, new byte[0])).equals(v.get("data_empty")), "DATA 空包");

        // 4) BEGIN 帧
        byte[] mdBody = "墨水屏测试".getBytes(UTF8);
        check(hex(PushProto.encodeBegin(mdBody.length, PushProto.crc32(mdBody),
                0xDEADBEEFL, 0x1A2B, PushProto.TYPE_MARKDOWN, "标题✓")).equals(v.get("begin_md")),
                "BEGIN markdown/sid=DEADBEEF 逐字节一致");

        byte[] txtBody = "ReadPico BLE push reference".getBytes(UTF8);
        check(hex(PushProto.encodeBegin(txtBody.length, PushProto.crc32(txtBody),
                1L, 0x1A2B, PushProto.TYPE_TEXT, "来自手机")).equals(v.get("begin_text_sid1")),
                "BEGIN text/sid=1 逐字节一致");

        check(hex(PushProto.encodeBegin(0, 0, 1L, 0x1A2B, PushProto.TYPE_WORD,
                "serendipity")).equals(v.get("begin_word")), "BEGIN 单词卡（空正文）逐字节一致");

        // 5) 长标题截断：40 emoji + 30 汉字 -> 63 字节截断 + 续字节剥离
        byte[] lb = PushProto.encodeBegin(3, PushProto.crc32("abc".getBytes(UTF8)),
                1L, 0x1A2B, PushProto.TYPE_TEXT, buildLongTitle());
        check(lb.length == PushProto.BEGIN_LEN, "长标题 BEGIN 长度 83");
        check(hex(lb).equals(v.get("begin_long")), "长标题 BEGIN 逐字节一致");
        int titleLen = 0;
        for (int i = 19; i < 19 + 64; i++) {
            if (lb[i] != 0) {
                titleLen++;
            } else {
                break;
            }
        }
        check(titleLen == vecInt(v, "long_title_len"),
                "标题字段实占 " + v.get("long_title_len") + " 字节（截断保 0）");

        // 6) 广播名 -> token
        Integer tk = PushProto.tokenFromAdvName("PicoPush-1A2B");
        check(tk != null && tk == vecInt(v, "token_ok"), "广播名解析 token 0x1A2B");
        check(PushProto.tokenFromAdvName("Other-ABCD") == null, "非本服务广播名返回 null");

        // 7) 多分片（1230 字节 / 6 包，seq 0..5）
        byte[] big = bigBody();
        check(big.length == vecInt(v, "big_len"), "大正文 1230 字节");
        check(PushProto.crc32(big) == (int) Long.parseLong(v.get("big_crc")), "大正文 CRC32");
        check(PushProto.chunkForMtu(247) == vecInt(v, "chunk_247"), "chunk(247) == 241");
        check(PushProto.chunkForMtu(23) == vecInt(v, "chunk_23"), "chunk(23) == 17");
        check(hex(PushProto.encodeData(0, java.util.Arrays.copyOfRange(big, 0, 241)))
                .equals(v.get("big_first")), "分片 0（seq=0 len=241）逐字节一致");
        check(hex(PushProto.encodeData(5, java.util.Arrays.copyOfRange(big, 5 * 241, 1230)))
                .equals(v.get("big_last")), "分片 5（seq=5 len=25）逐字节一致");

        // 8) STATUS 解析
        byte[] errFrame = new byte[] { (byte) PushProto.ST_ERR, (byte) PushProto.ERR_CHECKSUM };
        check(PushProto.statusOp(errFrame) == PushProto.ST_ERR, "STATUS opcode");
        byte[] errBody = PushProto.statusBody(errFrame);
        check(errBody.length == 1 && (errBody[0] & 0xFF) == PushProto.ERR_CHECKSUM,
                "STATUS body = ERR_CHECKSUM");
        byte[] readyFrame = new byte[] { (byte) PushProto.ST_READY, 1, 0, 0, 0 };
        check(PushProto.readySessionId(readyFrame) == 1L, "READY session_id 解析");

        // 9) 错误码文案
        check(PushProto.errText(PushProto.ERR_CHECKSUM).contains("CRC32"), "errText CHECKSUM");
        check(PushProto.errText(PushProto.ERR_NOT_ARMED).contains("接收窗口"), "errText NOT_ARMED");

        System.out.println("结果 / result: " + pass + " passed, " + fail + " failed — "
                + (fail == 0 ? "ALL PASS" : "HAS FAILURES"));
        System.exit(fail == 0 ? 0 : 1);
    }
}
