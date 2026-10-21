# instrumentation v2：AOI1 布防回读只读探针（归档件）

**树锚定协议规则⑥适用对象**：「凡进入构建的临时改动（instrumentation/debug 探针/门控开关），patch 与 bin 同时归档并在锚定记录登记指针」——本目录即 v2 的 patch 本体归档，配套 bin 为 gen4/gen5/gen8 debug 构建产物（见下）。

## 这是什么

`instr_v2_aoi1_readback.py` = instrumentation v2 的**唯一实体**（2026-10-14 23:22 创建于 `/tmp/instr_apply.py`，5,293 B，sha256 `d47bfe0ceeb66112c2227fc73e1b12b639ed2c0d5ce2297ab7862b7247ab927c`，本目录副本与之逐字节一致）。它是一段「临时贴装脚本」：在 SC7A20 布防链路上插入三处**只读回读**代码（驱动 dump 函数 + 头声明 + sleep.c 调用点），验证「布防返回 ESP_OK 且寄存器内容正确」，证伪「返回 OK 但阈值/时长没落盘」这类静默失效。

**判据**（区间断言）：`mg 等效 >= 950 && dur == 5`——1000mg 在量化公式下编码为 992mg，寄存器原值随驱动量化参数微调，钉死原值会误报，故只卡 mg 等效下界与 duration 精确值。判据常量与做法方案 §13「睡眠/拿起唤醒」条目逐字对应（脚本内注释声明，改任一边必须同步另一边）。

## 用法

```sh
python3 tools/instrumentation/instr_v2_aoi1_readback.py apply    # 贴装（三处锚点各断言 count==1）
python3 tools/instrumentation/instr_v2_aoi1_readback.py revert   # git checkout 三文件回退
```

`ROOT` 硬编码 `/tmp/rpr`（工作树位置）。贴装后构建产出 debug bin，**验证后必须 revert**——这是临时代码，永不进生产。

## 服务于哪些构建（provenance）

| 构建 | 底座 HEAD | 贴装后脏摘要 | 产出 debug bin | 状态 |
|---|---|---|---|---|
| gen3 debug | `d55f158` | `cf9052e5c71749d7` | 已留档（gen3 双件） | 已签核退役 |
| gen4 debug | `f7e7f48` | `9b13dfba80e41d59` | 已留档（gen4 双件） | 第②层寄存器证据 PASS×2（t=110846/175496）后签核 |
| gen5 debug | `bb77ff7` | `a51f63184672153e` | `98018df1e242da70b97b00427a947012c8a90075ecb6fff146cf35f37b6feee7`（4,011,872 B） | **保留为第②层备用锚**（production/debug 目录现状） |
| gen8 debug | `a7832f5` | `1846913aae4347dd` | `9d19faec910342d25a1457e5735fab83bc0675bc849ff271437eb6643c4a2428`（4,012,656 B） | 编译验证用，**不贴交付** |

**gen8 轮的机械证实（规则⑥首次完整闭环）**：对干净树 `a7832f5` apply 本脚本 → `git diff | sha256sum` = `1846913aae4347dd…`，与 gen8 debug 构建记录值**精确命中**——本脚本即产出 gen8 debug bin 的 patch 本体，铁证。

## 历史断点与修正记录（勿删，制度由来）

- **gen6 轮打捞结论「不可恢复」系打捞范围错误（2026-10-17 补正）**：gen6 轮（NOTES.md「gen6 双 bin 重建重锚」节）曾以四条证据链判定「v2 源码从未入库、不可恢复」——搜的是 `.patch` 文件与 git 全史（`-S "arm verify"` 无命中、6 个工作树、stash、dangling 对象、/tmp 全部 7 个 patch）。**漏了 `.py` 脚本载体**：v2 实体从来不是 `.patch` diff 文件，而是存活于 `/tmp/instr_apply.py` 的 Python 脚本。gen8 轮按规则⑥整理归档时发现该文件健在，并以脏 Digest 精确命中机械证实其即 patch 本体。「不可恢复」结论就此作废，**该结论保留原位并以此条为准**。
- **制度根因**：规则⑤管 bin、没管进入 bin 的临时改动的源——贴装即回退、源码不落库，断点即断可复现性。规则⑥（2026-10-16 用户裁决升格）即为堵此缺口，「随贴随留 patch」。

## v3 触发条件（既有定档，不现在重建）

**不重建 v3**——未来任何触碰布防链路（`sleep.c` / SC7A20 驱动）的改动才需要 debug bin，届时 v3 从零重建走新签核。gen8 轮未触碰 S3 链路，故 gen5 debug bin 保留第②层备用锚、本目录 v2 原样封存。

## 观察项（与做法方案 §13.3 P2 互引）

本探针只读、不改布防决策，对 seg1 锚点无影响（gen7/gen8 的 seg1 两字段双双同值，见收口报告 §12.5）；B 方案（P2 跨栏独占页）实现时若触碰 `reader_image_slot` 回退分支的 band 语义，本探针的锚点断言（sleep.c `acc_armed = true;`）仍适用，无需改动。
