# ReadPico 安卓 BLE 推流 · nRF Connect 手动验证手册

> 适用对象：开发者/测试，想不写代码就验证固件 `push_sink` 是否正确收包、唤醒、开书。
> 配套工具：`push_pico.py`（桌面自动推送 + 帧生成）、`ReadPicoPush.bsh` + `MacroDroid手动搭建教程.md`（安卓免电脑自动推送）、`ReadPicoPush.tasker.xml` / `ReadPicoPush.macro`（参考草图/流程图，macro 不可导入）。
> 协议冻结：本手册的 UUID / opcode / 帧布局与 `components/push_sink/include/push_proto.h` 完全一致，单方面改动会破坏已发货固件。
> 写入方式（实证修正记录，gen12）：5f11/5f12 固件特性均为 **WWR（Write With Response，写请求）**；源码实证固件从未出现 WNR 属性位，两种写固件都接收，但验收默认走 WWR（ATT 逐包回执）。旧文档"PUSH_DATA 是 write-no-response"的记录已撤销，最终协议方向待真机数据定案（见 `docs/M1-audit.md`）。

---

## 1. 在墨水屏开启接收窗口

1. 墨水屏主菜单 → **传输** → **手机推送**（或首页「手机推送」卡片）。
2. 进入后屏幕显示：
   - **广播名**：`PicoPush-XXXX`（XXXX = 4 位大写十六进制，即 token）
   - **状态**：等待连接 / 接收中 / 窗口倒计时
   - **提示**：开启后将暂停翻页器连接，退出自动恢复
3. 窗口默认 **30 分钟**；插着 USB 充电时进入「常开」——不计时、不拔电不退出。
4. 记下图 1 里的 `XXXX`（token），后续手机端要用。

> 注意：未开启接收窗口时，任何写入都会收到 `ERR(0x13 0x01)` = `NOT_ARMED`。

---

## 2. 手机端准备 nRF Connect

- 安装 **nRF Connect for Mobile**（iOS / Android 均可）。
- 蓝牙已开，手机与墨水屏处于可发现距离（BLE 距离通常 < 10m）。

---

## 3. 连接并发现服务

1. nRF Connect → **Scanner** → 扫描，找到名称 `PicoPush-XXXX` 的设备。
2. 点 **Connect**。连接成功后切到 **Device Services** 视图。
3. 找到自定义服务：

   ```
   服务 Service : e2f5a1c0-9b7d-4e3f-8a61-3c9d2b7e5f10
     ├─ 5f11  PUSH_DATA    (Write / WWR，写请求)              ← 发正文分片
     ├─ 5f12  PUSH_CTRL    (Write / WWR，写请求)              ← 发 BEGIN/END/ABORT
     └─ 5f13  PUSH_STATUS  (Read, Notify)                    ← 收设备回执
   ```

4. 点开 **5f13 PUSH_STATUS** → **Enable CCCD / Notify**，准备接收回执。

---

## 4. 生成待写字节（用 push_pico.py）

手机键盘敲 83 字节 BEGIN 很易错。用配套脚本直接吐十六进制：

```bash
# 纯文本（中文亦可，UTF-8）
python3 push_pico.py --hex --text "墨水屏测试" --title "标题✓" --token 1A2B

# 单词卡（type=4，正文为空，title 即单词）
python3 push_pico.py --hex --word serendipity --token 1A2B

# URL（type=3，M1.5 起设备端抓取；M1 仅占位）
python3 push_pico.py --hex --url "https://example.com/a" --title "好文" --token 1A2B
```

输出形如（请照抄 BEGIN 与每段 DATA 的十六进制串）：

```
# token=1A2B  type=0  session_id=00000001
BEGIN (写 5f12, 83 字节):
  50430101002b1a0f0000005695774901000000e6a087e9a298e29c930000...0000
DATA seq=0 (写 5f11):
  00000fe5a2a8e6b0b4e5b18fe6b58be8af95
END (写 5f12):
  50430201
```

---

## 5. 按序写入

| 步骤 | 特征 | 方向 | 写入内容 | 期望回执(5f13 Notify) |
|------|------|------|----------|------------------------|
| 1 | **5f12** PUSH_CTRL | 手机→设备 | `BEGIN`（上表 83 字节） | `READY` = `10` + session_id(4B LE) |
| 2 | **5f11** PUSH_DATA | 手机→设备 | `DATA seq=0` 包（头部 `seq(u16 LE) len(1) payload`） | （可选 `PROGRESS` = `16` + cur(2B)+total(2B)） |
| 3 | 重复 2 | 手机→设备 | 后续 DATA 分片，seq 自增 | 同上 |
| 4 | **5f12** PUSH_CTRL | 手机→设备 | `END` = `50430201` | `DONE` = `12` + total_len(4B LE) |

- DATA 每包载荷上限 **241 字节**（MTU 247 时）。超出请拆成多包，seq 从 0 连续递增。
- **写入方式选「Write（带响应/WWR）」**：nRF Connect 点特征的下写按钮后，写入类型选带响应的写（有的版本显示为 ↑↩ 或下拉可选 "Write without response"——**不要选**后者）。固件两种都收，但验收口径是 WWR：ATT 逐包回执是第一道防线，RESUME 回归是第二道。
- 设备只在 `RECEIVING` 态接收 DATA；其余态写 DATA 会回 `ERR(0x13 0x02)` = `BAD_FRAME`。
- 若 CRC32 不符，设备回 `ERR(0x13 0x04)` = `CHECKSUM`；此时可重发 `RESUME_QUERY`(`50430301`)，设备回 `RESUME`=`11`+received(4B)+next_seq(2B)，手机从该 offset 续传。

---

## 6. 错误码速查（STATUS ERR 的 body 字节）

| code | 含义 | 处理 |
|------|------|------|
| `01` | NOT_ARMED 未开启窗口 | 回墨水屏开启接收 |
| `02` | BAD_FRAME 魔数/版本/长度错 | 检查帧 |
| `03` | TOO_LARGE 超 512KB | 拆分或放弃 |
| `04` | CHECKSUM CRC 不符 | RESUME_QUERY 续传 |
| `05` | NO_CARD TF 卡不可用 | 插卡 |
| `06` | LOW_BATTERY 电量不足且未充电 | 充电 |
| `07` | BUSY 已有推送在处理 | 等当前完成 |
| `08` | FETCH URL 抓取失败（M1.5） | 换 URL |
| `09` | NO_DICT 无词典，单词卡不可 | 改用普通文本 |

---

## 7. 成功判据

- 收到 `DONE`(0x12) 后，墨水屏应**自动唤醒并全屏显示**该内容（不许手动点屏）。
- 单词卡（type=4）收到后弹**词典卡片**，不打开整书。
- 正文写入 TF 卡根目录 `.readpico/push.txt`（设备自动创建），书名/标题按推送标题显示。

---

## 8. 帧布局速查（冻结）

```
BEGIN (83B):  'P'(50) 'C'(43) OP=01 VER=01 | type(1) | token(u16 LE)
              | total_len(u32 LE) | crc32(u32 LE) | session_id(u32 LE)
              | title(64B, UTF-8, NUL 填充, 截断保 0)

DATA:         seq(u16 LE) | len(1) | payload(≤241)

CTRL:         'P' 'C' OP VER  —— OP: 01 BEGIN / 02 END / 03 RESUME_QUERY / 04 ABORT

STATUS:       opcode(1) | body(≤12)
  READY=10 + session_id(4)
  RESUME=11 + received(4) + next_seq(2)
  DONE=12 + total_len(4)
  ERR=13 + code(1)
  PROGRESS=16 + cur(2) + total(2)
  BATT=15 + percent(1)
```

CRC32 = IEEE 反射式 0xEDB88320（与 `zlib.crc32` 同款）。

---

## 9. 与自动化对照

- `push_pico.py --selftest`：离线对表 CRC/帧布局（不连蓝牙也能跑）。
- `push_pico.py --text "..." --scan`：桌面真机一键推送（需 bleak）。
- `ReadPicoPush.bsh` + `MacroDroid手动搭建教程.md`：安卓 MacroDroid「复制即推」全自动通道（Java Code 动作 / BeanShell，已完整测试：帧字节与本文档逐字节一致）。
- `ReadPicoPush.tasker.xml` / `ReadPicoPush.macro`：参考草图/流程图（macro 文件不可导入 MacroDroid，原因见其头注；帧算法可移植）。
