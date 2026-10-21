# 二期 M1「安卓 BLE 蓝牙推流」· gen11 审计报告 + gen12 修复附录

> 范围：六步执行序终步审计 + 用户审计意见核销 + gen12 真机缺陷回归修复。基线 `main` @ `ae6cf52`。
> gen11 可烧录包（含四 bin + 安卓四件套）：**`/workspace/read-pico-gen11.zip`**。
> gen12 修复包（GATT 服务空白，见第七节）：**`/workspace/read-pico-gen12.zip`**。
> 编译与打包由本沙箱完成（用户侧为纯 Windows，无 ESP-IDF 工具链）。

---

## 一、审四件核销

### ① 地基验证全套 — ✅ 通过（三值改显式）

| 项 | 结果 |
|----|------|
| 13/13 宿主 runner | **全绿**，逐个 `exit=0` |
| **黄金三值（显式，不再「隐含」）** | 单栏 **`d3a187989f1f617c`** / page_map **`39cc94d66cbeb2b1`** / settings_panel **`81dffeaf94d63657`** —— 三值内嵌于 `book_layout_single_column` / `reader_page_map` / `reader_settings_panel` 三套 golden 对比，由 `run_book_host` / `run_display*` 等 runner 驱动；本轮 13/13 绿即三值未漂移 |
| 整树干净重建 | `idf.py clean && build` **exit 0** |
| 镜像体积 | `Read_Pico.bin` = **0x3dc0a0 = 4,047,008 B**，分区 0x580000，余 **30%** |
| 全量警告 | **8 条，与 gen10 基线同集**（M1 零新增） |

警告明细（清一色 `-Wunused-function`，均在 app_book.c / ui_pinpad.c）：

```
ui_pinpad.c:106   rounded_inside        app_book.c:718   save_shelf_cover_fast
app_book.c:683    load_shelf_cover_gray app_book.c:1392  editor_start
app_book.c:700    load_shelf_cover_fast app_book.c:5234  resize_text
app_book.c:6022   adjust_spacing        app_book.c:6049  select_reading_font
```

### ② push_sink 宿主红绿 + mutation 有牙 — ✅ 通过（补重建哈希铁证）

- 基线 **168 checks / 0 red**（含本轮新增的 py↔C 跨语言向量 19 条，见 ④）。
- 有牙演示：`push_session.c:68` `if (b->token != s->token)` 注入恒假 `&& 0`
  → **1 red @ line 411**（token 比对失效被 tooth-5 抓到）。
- **精确还原铁证**：还原该行后重建，`Read_Pico.bin` sha256 仍为
  `07e4e4722938274964bbfbfafaf578a5940f9fe16386427e21cad49397dd92c9`，
  与注入前逐字节相同 —— 证明还原无残留（此为版本串 bump 之前的锚，bump 后见第三节）。

### ③ gen11 锚定 — ✅ 补全（段表已补）

| 文件 | 大小 (B) | sha256 |
|------|---------|--------|
| `Read_Pico.bin` | **4,047,008** (0x3dc0a0) | `6490999f89bfa66411f971e29fbbe77c7a5fae321adeb90127f187c5d84e817e` |
| `bootloader.bin` | 21,232 | `229b7d357d3d3cb64dc2ada602baf2193a874cfb7a9edcf02d267c92aa042b27` |
| `partition-table.bin` | 3,072 | `9d5d890b49862fdb8a2cdd0df434e35b8cca5f4768d4b1a0980513b1af2d9e58` |
| `ota_data_initial.bin` | 8,192 | `7d2c7ac4888bfd75cd5f56e8d61f69595121183afc81556c876732fd3782c62f` |

**seg1 双锚段表**（esptool image-info，7 段全表）：

```
Seg  Length   Load addr (vaddr)  File offs (paddr)   Memory types
  0  0x232e28 0x3c190020         0x00000018          DROM
  1  0x08a8c  0x3fc9c700         0x00232e48          BYTE_ACCESSIBLE, MEM_INTERNAL, DRAM
  2  0x04734  0x40374000         0x0023b8dc          MEM_INTERNAL, IRAM
  3  0x188054 0x42000020         0x00240018          IROM
  4  0x13f94  0x40378734         0x003c8074          MEM_INTERNAL, IRAM
  5  0x00024  0x500003dc         0x003dc010          RTC_DATA
  6  0x00030  0x600fe000         0x003dc03c          RTC_DRAM, RTC_IRAM
```

- **seg1 vaddr `0x3fc9c700` —— 四代不变锚保持**（gen8/gen9/gen10/gen11 同为 `0x3fc9c700`）。
- seg1 **File offs `0x00232e48`**（esptool 报的是**段头**偏移）；
  真机 boot 日志的 **seg1 paddr 预测值 = `0x00242e50`** —— 公式
  `paddr = File offs + 8(段头) + 0x10000(ota_0 基址)`，
  **由 gen5/gen6/gen10 三轮真机日志实证**（gen10：File offs `0x00232038` →
  实测日志 `paddr=00242040` ✓，见 `realhw-session-20261016/prod-log-gen10-round1.txt`）。
  相对 gen10 的 paddr 差 **+0x810 = 2,064 B**，与 seg0(DROM) 增长顺移一致，量级合理。
- 术语纪律：本文档此前把 esptool 的 File offs 直接称作 paddr，**两者相差段头 8B**，
  已就地更正；真机日志对表须用 paddr 口径（`0x242e50`），不要用 File offs。
- 镜像内部标识：`Project name: Read_Pico` / `App version: 0.3.3-pico11` /
  `Compile time: Oct 18 2026 15:34:25` / `Validation hash: 98008f3b…dd5ffd (valid)`。
- 相对 gen10（4,013,568 B）：**+33,440 B**，全部归因 M1 的 push_sink 组件 + 武装页
  （`tools/` 不进 bin）；分区余量 30%，安全。

### ④ 帧对表 — ✅ 升级为机器级（py↔C 不再靠人眼）

- py 自检：`push_pico.py --selftest` **20/20 PASS**。
- py↔js：`push_frames.js` 与 `push_pico.py` 同一 BEGIN 的十六进制 **diff 完全一致**。
- **py↔C（本轮新增，补齐单腿）**：把 `push_pico.py --hex` 生成的字节钉进
  `tools/push_sink_host_test.c` 的 `test_py_cross_vector()`，用 C 侧解码器断言：
  - BEGIN(83B) → token=0x1A2B、type=TEXT、total_len=300、crc32=0xf4ab327b、session_id=1、
    title=`跨语言向量✓`（多字节 UTF-8，含 ✓）
  - DATA 全帧（seq=0/241B + seq=1/59B）→ seq 与长度正确、载荷指针 == 帧+3
  - CTRL END / RESUME_QUERY 公共头 opcode 正确
  - **端到端 CRC**：两包载荷拼回正文，用 C 的 `push_crc32` / `push_crc32_continue` 重算，
    结果必须等于 py 写进 BEGIN 的 crc32 字段 —— CRC、长度、分包边界、seq 语义一次对死
  - 测试规模 **149 → 168 checks（+19）**，仍 **0 red**
- 两份自动化 XML `xml.dom.minidom` 良构校验通过。

**踩坑记录（诚实）**：首版只把 DATA 的 3 字节头喂给 `push_data_decode`，结果 4 条红线——
该解码器会校验 `len >= 3 + n`（载荷确实在缓冲区里），只喂头解不出来。这是实现**正确**的
行为，测试写法错了；改为嵌入 py 生成的完整 DATA 帧后转绿。

---

## 二、实证修正记录（PUSH_DATA 写模式：所谓「WWR → WNR 漂移」从未发生）

> 本节由 gen11 的「协议变更记录」**改写**而来。gen11 曾记录「PUSH_DATA 由 WWR 改为
> write-no-response」并裁决「接受 + 三条件」。**gen12 源码实证表明：该裁决的前提
> 不成立——漂移从未发生，固件特性位一直是 WWR。** 三个事实并列如下，全部一手实证。

### 三事实并列

| # | 事实 | 证据 |
|---|------|------|
| F1 | gen11 汇报与四件套曾称 PUSH_DATA（`…5f11`）为 write-no-response | gen11 审计旧第二节、`push_pico.py:99` 旧注释等 |
| F2 | **固件特性位从未是 WNR**：WNR 标志（`BLE_GATT_CHR_F_WRITE_NO_RSP`）在本仓库固件源码中零出现；`push_sink.c` 注册 5f11/5f12 的特性位一直是 `BLE_GATT_CHR_F_WRITE`（WWR） | gen12 轮全仓检索 + `push_sink.c` 特征定义 |
| F3 | NimBLE ATT 服务端对两种写一视同仁：`ble_att_svr_rx_write_no_rsp` 与 `rx_write` 都汇入 `write_handle → ha_cb`（`MYNEWT_VAL_BLE_ATT_SVR_WRITE_NO_RSP=1` 已实证） | ESP-IDF NimBLE 源码 + gen12 构建配置 |

### 结论与处置

- **撤销 gen11 裁决的条件①**：「ATT 逐包确认退场、重传职责移交 RESUME」的前提是
  固件走 WNR。固件实际是 WWR——**ATT 逐包确认从未退场**，该条件整条撤销。
- **保留 RESUME 作为第二道防线**：断连补包继续有效；gen12 真机验收按
  **WWR + RESUME 双重防线**口径执行（见第六节项 4）。
- **条件②（速率承诺保守化）保留**：文档维持 8–25 KB/s 保守值；WWR 路径下有逐包
  往返，吞吐数学待真机数据重算，不在真机轮前据理论上修。
- **条件③（M2 路线图）保留**：「视真机吞吐决定是否引入窗口 ACK」不变。
- **最终协议方向：不定稿，待真机数据。** gen12 真机轮须验证「安卓端写 ↔ 固件 WWR
  属性」这条从未真机跑过的跨路径（见第六节新增必验项「推送全链成功」）；若真机
  暴露问题，再议是否让固件补 WNR 位——那是协议变更，走变更流程，不在本轮暗度。
- **四件套 + 源码注释同步修正**：`push_pico.py` / Tasker XML / nRF Connect 手册 /
  `push_sink.c` 特征注释均已按本记录改写；手机端默认走 WWR 写（ATT 逐包回执）。

### 附带同步（不变）

BEGIN 83B 中的 `token(u16)` 字段系开工指令 a（token = 广播名后缀本身）的落地，
草案 §3.2 帧表原无此字段 —— 已同步进协议文档与四件套，按**记录过的**协议处理，不另计漂移。

---

## 三、真机前必解两项

### 1. 版本串 bump — ✅ 已完成

`0.3.3-rc95` → **`0.3.3-pico11`**（fork 专属、与 gen10 可区分，TF 卡升级不会被判「版本相同」拒绝）。

| 位置 | 改动 |
|------|------|
| `CMakeLists.txt:14` | `set(PROJECT_VER "0.3.3-pico11")` |
| `README.md:11` | 当前版本行 → gen11 / 0.3.3-pico11 |
| `web-flasher/manifest.json:3` | `"version": "0.3.3-pico11"` |
| Release tag | **用户侧待办**（真机轮八项通过前不推送，见第五节） |

bin 内实测 `App version: 0.3.3-pico11` ✓。

### 2. EVT_READ（0x14）— ✅ 已实现，本轮补齐触发入口

审计指出「汇报的 STATUS 清单里没有 0x14，Step 4 描述也没有标记已读入口」。核查结论分两层：

- **通知通道本就存在**：`push_proto.h:48` `PUSH_ST_EVT_READ = 0x14`；
  `push_sink.h:148` 声明 `void push_sink_notify_read(void)`；
  `push_sink.c:1198` 实现（编码 0x14、空 body、notify）。
- **但零调用方** —— 通道有、入口无，属模糊态，审计点破正确。

**裁定：补齐（非降级）**，惊喜2 ⭐ 属 M1 批准范围。落点：

> `main/apps/app_book.c` `book_on_exit()`：若离开的是手机推来的虚拟书
> （`strcmp(s_path, PUSH_TEXT_PATH) == 0`），则 `push_sink_notify_read()`。

选退出处而非翻开处的理由：**读没读完只有退出这一刻才说得上**，且不动阅读页任何既有路径。
宿主侧 `book_ui_host_test.py` 补 `PUSH_TEXT_PATH` / `push_sink_notify_read` 桩
（抽取式测试会抽走 `book_on_exit`），13/13 仍全绿。

**证据**：`grep -rn push_sink_notify_read main/` → `app_book.c`（book_on_exit 内 1 处调用）；
编码与 notify 由 push_sink 宿主测试覆盖（168 checks 0 red）。

---

## 四、gen11 交付包

`/workspace/read-pico-gen11.zip`（2.2 MB，**12 条目**，`unzip -t` 完整性通过；
条目数含 `android-tools/` 目录项；`flash.sh` 为第四节之二同步后补入，提交 `13b2c4d`）

```
Read_Pico.bin            4,047,008 B   ← 0x10000
bootloader.bin              21,232 B   ← 0x0
partition-table.bin          3,072 B   ← 0x8000
ota_data_initial.bin         8,192 B   ← 0xb10000
flash.sh                               烧录脚本（EXPECTED 锚定自检 + 拒烧防呆）
烧录说明-NOTES.md                      偏移/锚定/新功能/真机轮八项
android-tools/push_pico.py            参考实现（--selftest/--hex/--scan）
android-tools/push_frames.js          帧构建 JS（Tasker/MacroDroid 内嵌）
android-tools/ReadPicoPush.tasker.xml Tasker 导入件
android-tools/ReadPicoPush.macro      MacroDroid 导入件
android-tools/nrf-connect-manual.md   手动验证手册
```

**包内 bin 实测**：解压后 `Read_Pico.bin` 与 `build/Read_Pico.bin` `cmp` 逐字节一致 ✓，
包内 `App version` 读数 `0.3.3-pico11` ✓。

---

## 四之二、烧录防呆：flash.sh EXPECTED 同步 + 双路径干跑（gen11 烧前最后一件）

> 边界更正：发布管线（`/workspace/read-pico-realhw/production/`）本就在工作区内，
> 此前「gen11 打包由用户发布环境收口」的表述是**边界判错**，standing SOP 不该跳。

- **gen10 归档**：`Read_Pico.bin` → `Read_Pico.bin.gen10-9e573c84`（4,013,568 B，勿烧）。
- **四 bin 换入 gen11**：Read_Pico.bin（4,047,008）/ bootloader.bin / partition-table.bin /
  ota_data_initial.bin；实测 sha256 与第三节锚定表一致。
- **EXPECTED 同步**：`9e573c84…` → **`6490999f…4e817e`**。
- **flash.sh 头部锚定注释同步**：gen11 段表（seg1 vaddr / len / File offs / 预测 paddr）、
  体积 +33,440 B、版本串 `0.3.3-pico11`、bootloader 非确定性归因、partition/ota 逐字节不变。
- **双路径干跑**（留痕 `read-pico-realhw/realhw-session-20261019/flash-dryrun-gen11-20261019.txt`）：

| 路径 | 操作 | 结果 | exit |
|------|------|------|------|
| 1 | EXPECTED = gen11 正确值 | `sha256 OK: 6490999f… (production build, anchor verified)` → esptool 止于无串口设备 | **2**（沙箱无设备，与 gen10 轮同形态） |
| 2 | EXPECTED 篡改为 gen10 值 | `FATAL: Read_Pico.bin sha256 mismatch - refusing to flash.` | **1** ✅ 拒烧 |
| 复验 | 还原回 gen11 值 | `sha256 OK` | 2 |

烧录防呆有效：锚定值不符必定拒烧。gen8/gen9/gen10 三轮同形态，本轮补齐 gen11。

## 五、工程卫生：M1 已本地提交（不推送）

审计指出 1,300+ 行未提交改动躺在 pico-plus 公开树（ae6cf52）存在误推送与分叉风险。
**已按组件本地提交**（写实提交信息），**真机轮八项通过前不推送 pico-plus**
（公开仓只见稳定发布面）。canonical 开发位置记入 `docs/HANDOFF.local.md`。

提交分组：① `components/push_sink/*`（新组件 + 第 13 runner）② `components/ble_page_turner/*`
（三薄钩子）③ `main/app` + `main/sleep.c` + `main/app_main.c`（调度/睡眠豁免）
④ `main/apps/app_transfer.c` + `app_files.c`（武装页/入口）⑤ `main/apps/app_book.c`
（EVT_READ 入口）⑥ `tools/*`（桩/runner/四件套）⑦ 版本串三处 ⑧ `docs/*`（账本/审计）。

---

## 六、真机轮八项判读（用户侧，随协议漂移微调）

1. 推送后**自动唤醒全屏显示**，不需点屏（死线③）。
2. **连招**（以 EVT_READ 为前置）：复制 → 推 → 读 → 退出推送书 → 手机收到「已读」→ 剪贴板清空。
3. 中文长文（多包）**不掉字不乱序**（死线②）。
4. **断点续传**（重要度上升）：中途断连 → 重连自动续推。口径改为「**WWR + RESUME 双重防线**」：手机端逐包等 ATT 回执（第一道防线，gen12 起推送通道默认 WWR，见第二节实证修正），设备侧 NVS 检查点 + RESUME 快进（第二道防线）。
5. 单词卡：推送单词 → 弹词典卡片，不开整书。
6. 充电常开：插电不计时；**拔电即退出**。
7. 翻页器让位：开启期间暂停，退出自动接回。
8. 防闪烁：收完统一写盘、一次全刷（死线④）。
9. **（gen12 起前置必验三项，全过才进八项判读）**：
   - **① Services 页验收**：连上后 Services 页须显示推流服务
     `e2f5a1c0-9b7d-4e3f-8a61-3c9d2b7e5f10`，展开含三特征 `…5f11`（写）/
     `…5f12`（写）/ `…5f13`（读+通知）；主动扫描时扫描记录（scan response）里
     带该服务 UUID（gen13 起 UUID 移驻 scan response，adv data 只放 flags +
     名字，见 7.6）。gen11 此处空白（根因见第七节）。
   - **② boot 日志 seg1 paddr 对表 `0x00242ff0`**（seg1 段头 File offs
     `0x00232fe8` + 8 + ota_0 基址 `0x10000`；公式 gen5/gen6/gen10 三轮实证；
     gen13 重建后 File offs 较 gen12 `0x00232f88` +96B 系 seg0 DROM 增长顺移，
     vaddr `0x3fc9c700` 与 len `0x08a8c` 六代不变，锚随 gen13 换值）。
   - **③ 推送全链成功（gen12 新增必验）**：用交付的 ReadPicoPush APK（或
     nRF Connect 手工通道）实际推一段文字，看到「设备已校验 CRC32 并开书」。
     **安卓端写 5f11/5f12 vs 固件 WWR 属性的跨路径从未真机验证过**（gen11 被
     Services 空白阻断在本项之前）；若失败，**第一嫌疑即写入类型/属性不匹配**
     （客户端误用 write-without-response，或设备侧属性与预期不符），按第七节
     附录 7.5 的通道说明逐段排查。本项不过 → 其余判读项全部失去意义。

---

## 七、gen12 修复附录：GATT 服务空白（gen11 真机缺陷回归）

> 触发：gen11 真机轮反馈「BLE 能连上 `PicoPush-XXXX`，但 nRF Connect 的 Services 页空白，
> 看不到推流服务」。本节记录根因的源码实证链、修复形态与 gen12 门禁全表。

### 7.1 根因：NimBLE GATT 注册时序（源码实证，非猜测）

- `ble_hs_start()`（`host/src/ble_hs.c:815`）在 host 启动路径里**一次性**调用
  `ble_gatts_start()`，**然后**才 `ble_hs_sync()` 触发 sync 回调。
- `ble_gatts_start()` 消费 `ble_gatts_add_svcs()` 排进的定义队列，末尾
  `ble_gatts_free_svc_defs()` 把队列整个释放；属性表与句柄由它分配填充。
- 两阶段启动链：`ble_hs_init()`（nimble_port_init 内）enqueue stage1 → host 任务
  stage2 → `ble_hs_start()` → `ble_gatts_start()` → `ble_hs_sync()` → sync_cb。
  `BLE_HS_AUTO_START` 启用，stage2 必定消费队列。
- gen11 的 `register_gatt()` 排在 **sync 回调里**——排在一场**已经散会**的队列后面：
  `add_svcs` 返回 0 只代表**排队成功**，而消费队列的 `ble_gatts_start()` 早已跑完。
  注册成功的唯一实证是三个句柄非 0；排队成功 ≠ 注册成功。

### 7.2 修复形态（三处，均按用户指定：常驻注册 + 显式 start + 三次调用错误日志 + adv 带 UUID）

| # | 落点 | 内容 |
|---|------|------|
| 1 | `push_sink.c` 新增 `push_sink_gatt_register()` + `queue_svcs()` | `ble_gatts_count_cfg()`（累加上限，必须在 start 前调）→ `ble_gatts_add_svcs()`，**两步返回码都查**，失败 `ESP_LOGE` 留证；`ble_page_turner.c` `start_locked()` 在 `bonds_load()` 之后、`xTaskCreatePinnedToCore(host_task)` 之前调用，让 host 自己的 `ble_gatts_start()` 自然消费 |
| 2 | `push_sink_on_stack_sync()` 改 `gatt_ensure_registered()` | 以 `s_data/ctrl/status_handle` 全非 0 为注册实证；未注册则**显式** `ble_gatts_start()` 补一次（第三次调用），成功/失败分别 `ESP_LOGI`/`ESP_LOGE` 打印三句柄。源码路径上不该触发（AUTO_START 下 stage2 必消费）；真触发即排队或注册失败，日志会喊 |
| 3 | `adv_start()` | 广播数据带 128-bit 服务 UUID（`fields.uuids128`），nRF Connect 扫描列表连上前即可见 `e2f5a1c0…` |

**簿记语义修正**：`s_gatt_registered` → `s_gatt_queued`（定义已排队 ≠ 已注册）；
`push_sink_on_stack_stop()` 同步改。每启动周期恰好排队一次：`start_locked` 失败回退走
`nimble_port_deinit()`（`ble_gatts_stop` → `ble_gatts_free_svc_defs` 清队列），
stop 事件 FIFO 晚于 stage2，`s_running` 早退守卫兜底。

**设计权衡（为何不是另两种形态）**：显式 start 前置于 host 启动会被 stage2 的二次
start 清表（`ble_gatts_free_mem` + 空队列重注册）；仅靠 sync 回调兜底则依赖 host 已
启动且 sync 完成的时序。故取「常驻排队让 host 自己的 start 消费（主路径）+ sync 里
以句柄为证的防御性显式 start（兜底）」。另实证：本构建无人调用
`ble_svc_gap_init()`，队列中只有我们的服务，兜底 start 无误伤面。

**协议注释澄清（零行为变更，2026-10-19 按实证修正重写）**：PUSH_DATA 特性位保持
`F_WRITE`（WWR，写请求）。gen11 汇报曾称协议已裁定 write-no-response——**源码实证该
前提从未成立**：`BLE_GATT_CHR_F_WRITE_NO_RSP` 在仓库源码零出现，固件 5f11/5f12 特性位
从实现第一天就是 `BLE_GATT_CHR_F_WRITE`，ATT 逐包确认从未退场（详见第二节三事实）。
NimBLE ATT 服务端对两种写一视同仁（`ble_att_svr_rx_write_no_rsp` 与 `rx_write` 均汇入
`write_handle → ha_cb`，`MYNEWT_VAL_BLE_ATT_SVR_WRITE_NO_RSP=1` 已实证），故客户端选
WWR 或 WNR 都能到达 `push_access_cb`；本工程所有客户端通道**默认 WWR**
（response=True）：逐包 ATT 回执是第一道防线，设备侧 RESUME 是第二道。注释已按此重写，
「位上是 WWR」与「协议是 WNR」的表观矛盾连同错误前提一并撤销。**最终协议方向
（是否引入 WNR 快速档）不定稿，待真机数据**。

### 7.3 gen12 门禁全表

| 项 | 结果 |
|----|------|
| 13/13 宿主 runner | **全绿**（`for s in tools/run_*.sh` 逐个 exit 0）；push_sink **168 checks / 0 red** |
| 整树干净重建 | `idf.py clean && build` **exit 0** |
| 全量警告 | **8 条 = 基线同集**（`ui_pinpad.c`×1 + `app_book.c`×7，unused-function；M1 与 gen12 零新增） |
| 版本串 | `0.3.3-pico11` → **`0.3.3-pico12`**（CMakeLists.txt / README.md / web-flasher/manifest.json 三处），bin 内实测 `App version: 0.3.3-pico12` |
| 镜像体积 | **4,048,208 B**（0x3dc550）；**+1,200 B = 4,048,208 − 4,047,008**（双操作数）；分区 0x580000，余 30% |
| seg1 双锚 | vaddr **`0x3fc9c700`（五代不变）** / len **`0x08a8c`（与 gen11 持平）** / File offs **`0x00232f88`**（gen11 `0x00232e48`，+320B，与 seg0 DROM 增长顺移一致）；boot 日志 paddr 预测 **`0x00242f90`** = `0x00232f88 + 8 + 0x10000`（公式 gen5/gen6/gen10 三轮实证） |
| 镜像 sha256 | **`6e1d15c16fe8a4c97c11cede6fa89276c82639c0afc46fdfe3d9835c7e7e2dbd`**（2026-10-19 重建重锚，见下「两轮重锚记录」）；bootloader / partition-table / ota_data_initial 与 gen11 逐字节不变 |
| 宿主测试适配 | `tools/ble_turner_host_test.py` 补 `push_sink_gatt_register` 桩（start_locked 被抽取编译，隐式声明触发 -Werror） |
| flash.sh 防呆 | EXPECTED 同步 `6e1d15c1`（含两轮重锚注释块）+ gen11 bin 归档 `Read_Pico.bin.gen11-6490999f`；双路径 dry run（本轮重跑）：锚对 → `sha256 OK` 放行至 esptool（stub python 收到 `0x0/0x8000/0x10000/0xb10000` 四偏移，exit 0）；篡改 2 字节 → `FATAL … refusing to flash`（exit 1）✅ 拒烧 |
| 交付包 | `/workspace/read-pico-gen12.zip` **12 条目**（四 bin + flash.sh + NOTES + android-tools/ 五件），`unzip -t` 通过；包内 bin 与 production 逐字节一致；安卓四件套与 gen11 逐字节相同（协议未变） |

**两轮重锚记录（诚实记录，2026-10-19 收官）**：gen12 先后重建两次，两次都只由
**零行为变更**触发，哈希插曲的归因链完整留痕：

- **第一轮**：先编 gen12（`a4c975a1`）→ 后改 push_sink.c 注释 → 增量重建得
  `219e0529`。逐字节 diff 仅 65 B（`0xB0–0xCF` app_desc 内嵌 ELF 哈希 +
  `0x3DC52F–0x3DC54F` 镜像尾校验和），系 DWARF 行表平移，非代码行为变化。
- **第二轮（本轮协议审计）**：第二节实证修正重写 push_sink.c 5f11 注释
  （「协议层已裁定 write-no-response」→ WWR 实证修正记录）后全链重建 →
  `6e1d15c1`。新旧 bin 逐字节 diff 仅 **69 B**：app descriptor 内嵌构建时间戳串
  （`0x71–0x77`，`05:26:54` → `08:31:46`）+ 尾部 5 B。**seg1 段头（0x232f88）与
  seg1 数据逐字节相同**（python 直读两 bin 断言），故 paddr `0x00242f90`、
  vaddr `0x3fc9c700`、len `0x08a8c、体积 4,048,208 B 全部不变。

处置：**以 `6e1d15c1` 为最终锚**，重部署 bin / flash.sh EXPECTED（含重锚注释）/
双路径 dry run / 交付 zip——保证发出的 bin 与提交源码严格同源。ESP-IDF 构建
**非位级可复现**（镜像内嵌构建时间戳），此后任何重建必须重走「sha256 →
production bin → EXPECTED → dry run → zip」全链，不得沿用旧哈希。

### 7.4 真机验收点（八项判读之前先看）

1. Services 页显示服务 `e2f5a1c0-9b7d-4e3f-8a61-3c9d2b7e5f10` + 三特征（5f11/5f12/5f13）——本条即 gen11 缺陷的修复判据。
2. 扫描列表广播可见服务 UUID（连上前确认对面是推流服务）。
3. boot 日志 seg1 paddr 对表 `00242f90`；`App version: 0.3.3-pico12`。
4. **推送全链成功（gen12 新增必验，第一优先级）**：用交付的 ReadPicoPush APK
   （`tools/android/apk/ReadPicoPush.apk`）推一段文字 → 设备开书。安卓端写
   5f11/5f12 vs 固件 WWR 属性的跨路径从未真机验证；失败第一嫌疑 = 写入类型/
   属性不匹配（排查顺序：APK 是否默认 WWR → nRF Connect 手工写选「Write」
   而非「Write without response」→ 设备侧属性位）。
5. 通过后再走第六节八项判读（判读项 2 以 EVT_READ 为前置、项 4 口径为
   「WWR + RESUME 双重防线」）。

### 7.5 安卓推送通道定型：MacroDroid 不可用 → Termux 否决 → APK 交付（2026-10-19）

> 背景：gen11 起规划用 MacroDroid 作零开发推送通道。本轮用户侧导入
> `ReadPicoPush.macro` 失败（荣耀 / Android 16 / 无 Google Play，提示
> 「导入失败，无法导入宏」），排查后通道选型彻底改写。

**三条通道的实证结论**：

| 通道 | 结论 | 根因（全部官方/源码实证） |
|------|------|--------------------------|
| MacroDroid `.macro` 导入 | **任何版本都导不进**，非兼容问题 | ① `.macro` 容器须 JSON 而非 XML（仓库内 `.macro` 为 XML 示意）；② MacroDroid **无原生 BLE GATT 写动作**；③ 其 JS 引擎（Rhino）无 `TextEncoder`，拼不出二进制帧 |
| MacroDroid Java Code 动作（`ReadPicoPush.bsh` 后备方案） | **技术上跑得通但主动放弃** | BeanShell 解释器 + 仅 Java 1.4 语法 + 后台线程阻塞等蓝牙回调可行；沙箱 9 场景全过（含抓出 `byte[]` 赋 int 字面量抛 `ArrayStoreException` 的真机必炸坑）。但硬用脚本调底层 API 既难维护又不稳定，用户裁定不再投入 |
| Termux + Python（`push_pico.py`） | **一票否决** | `push_pico.py` 真机推送依赖 `bleak`；bleak 走 BlueZ/dbus，而 **Android 用 Fluoride 栈，不暴露 BlueZ GATT 接口**，Termux 里装 bleak 也连不上。要用 Python 调 BLE 只能 pyjnius 桥接回同一套 Android Java API——把 Java 代码套层 Python 壳，更慢更脆 |
| **原生 APK（选定）** | **已交付** | `tools/android/apk/`：单 Activity、纯框架 API、零第三方依赖；协议层 `PushProto.java` 与 `push_proto.h`/`push_pico.py` 逐字段对表，宿主机 **25/25 断言全过**（向量由 `test/gen-vectors.py` 从参考实现生成，不手抄字节） |

**APK 交付形态**（`tools/android/apk/`，构建脚本 `build-apk.sh`，全手动链不依赖 Gradle）：

- `javac --release 8`（对表 `platforms;android-34/android.jar`）→ **PushProto 宿主机对表测试** → `d8 --min-api 26` → `aapt package` → `aapt add classes.dex` → `zipalign` → `apksigner`（debug keystore 首次自动生成）。
- 产物 `ReadPicoPush.apk`（25 KB，debug 签名可侧载；`aapt dump badging` 与 `apksigner verify` 通过，启动项 `dev.readpico.push.MainActivity`）。
- minSdk 26 / targetSdk 34；Android 12+ 走 `BLUETOOTH_SCAN`（neverForLocation）+ `BLUETOOTH_CONNECT` 运行时权限，Android 8~11 走 `ACCESS_FINE_LOCATION`。
- 推送流程与 Python/bsh 参考实现同构：扫 `PicoPush-XXXX`（12s）→ 连接（TRANSPORT_LE）→ MTU 247 → 发现服务 → 订阅 5f13（CCCD）→ BEGIN 写 5f12 → 等 READY → DATA×N 写 5f11（**WWR 逐包等回执**）→ END 写 5f12 → 等 DONE/ERR；空正文（单词卡）也发 seq=0 空包对齐有正文路径。
- 界面：输入框（启动自动读剪贴板）+「读取剪贴板」+「发送到墨水屏」+ 日志区 + 单词卡模式开关；标题取首行 40 字符（协议层另有 63 字节硬截断 + 续字节剥离）。
- 已知边界：沙箱无模拟器/无真机，BLE 状态机未经运行时验证——这正是第六节前置必验③存在的原因；协议字节层已 25/25 对表，风险集中在 Android GATT 回调接线（标准模式）。

**四件套处置**：`push_pico.py` / `nrf-connect-manual.md` 的 WNR 措辞已按第二节实证
修正清零；`ReadPicoPush.bsh` + `MacroDroid手动搭建教程.md` 保留在仓库作**调查留痕**
（记录「为何不走 MacroDroid」），但**不再进交付 zip**、不再投入维护；
`ReadPicoPush.macro` / `ReadPicoPush.tasker.xml` 头部已标注「不可导入，参考流程图」。
用户手机（荣耀 / Android 16 / 无 Play）的安装路径：APK 直接侧载，允许未知来源即可。

---

## 7.6 gen13 修复附录：广播数据溢出导致设备完全不可扫描（gen12 真机阻断级回归）

> 2026-10-19 晚。gen12 烧录后真机反馈：UI 正常（`PicoPush-8C92` + 29 分钟倒计时），
> 但手机系统蓝牙、第三方 BLE 助手、交付的 APK **全部搜不到设备**；同一第三方助手
> 在 gen11 上可搜可连——手机侧无问题，设备侧广播从未启动。

### 7.6.1 根因（串口日志 + NimBLE 源码双重实证）

真机串口日志关键行（每次进入「接收推送」页必出）：

```
W (42947) push_sink: adv set fields failed
```

且全程**没有** `advertising as PicoPush-XXXX` 行——`ble_gap_adv_start()` 从未被调用。

NimBLE 源码实证（`host/src/ble_gap.c` / `ble_hs_adv.c` / `hci_common.h`）：
`ble_gap_adv_set_fields()` 把字段序列化进 `BLE_HS_ADV_MAX_SZ` =
`BLE_HCI_MAX_ADV_DATA_LEN` = **31 字节**的缓冲区，超出即返回 `BLE_HS_EMSGSIZE`
（`ble_hs_adv_set_flat_mbuf` / `set_array_uuid128` 写超即拒）。

字节账（gen12 adv data）：

| 字段 | AD 结构大小 |
|------|------------|
| Flags | 1+1+1 = 3 B |
| Complete Local Name「PicoPush-8C92」 | 1+1+13 = 15 B |
| Complete List of 128-bit UUIDs（gen12 新增） | 1+1+16 = 18 B |
| **合计** | **36 B > 31 B → 拒收** |

即 7.2 修复形态里的「广播带 Service UUID」一项把广播包撑爆：`ble_gap_adv_set_fields`
失败 → `adv_start()` 早退（旧代码静默 return，连 rc 都不打）→ 广播没起来；
而状态机在 `ble_gap_adv_set_fields` **之前**已完成 ARM，UI 武装页照常渲染倒计时——
**假武装**：应用层一切正常，射频层面设备不存在。gen11 无 UUID 字段（3+15=18B）所以可扫。

### 7.6.2 gen13 修法（三处，全部有实证）

1. **UUID 移驻 scan response**：`ble_gap_adv_rsp_set_fields()` 是独立 31B 载荷，
   UUID 放那里（0x07 结构 18B），adv data 回到 gen11 实证可用的 flags + 名字
   （18B，字节级同构）。主动扫描的 nRF Connect / 安卓扫描器 / 交付 APK 的
   ScanRecord 仍能看到 `e2f5a1c0…`（gen12 的意图不丢，代价为零）。
2. **rc 全部打印**：`adv set fields rc=%d` / `adv rsp set fields rc=%d` /
   `adv start rc=%d`——失败不再静默，串口一眼可判。
3. **失败不留假武装**：任一广播步骤失败即 CANCEL 武装 + UI 收 `start_failed`
   （「蓝牙启动失败，请重试」）。为此 `app_transfer.c` `push_tick` 调整优先级：
   `start_failed` 先于 `closed` 判（失败路径两者会同时置位，CANCEL 的 ADV_OFF
   顺带发 closed），并吞掉同路的 closed 标志防闪两帧。

### 7.6.3 纪律入册：adv data 31 字节预算

- **adv data 硬上限 31 B**（`BLE_HCI_MAX_ADV_DATA_LEN`），序列化超限即
  `BLE_HS_EMSGSIZE` 拒收，且**不是运行时才发现的问题**——加字段前先算总长。
- push_sink `adv_start()` 内已钉死预算注释：flags(3) + name(≤15) = 18B，
  余量 ≥ 13B；再想加 adv data 字段，先过 31B 这条线，否则就是 7.6.1 这个坑。
- **「加个字段而已」从来不是小改动**：广播包是控制器级硬约束，不是软件缓冲区。

### 7.6.4 gen13 门禁全表

| 项 | 值 |
|----|----|
| 版本串 | `0.3.3-pico12` → **`0.3.3-pico13`**（CMakeLists.txt / README.md / web-flasher/manifest.json 三处），bin 内 esptool 实测 `App version: 0.3.3-pico13` |
| 镜像体积 | **4,048,512 B**（0x3dc680）；**+304 B = 4,048,512 − 4,048,208**（双操作数，vs gen12），scan-response 代码与日志串增量；分区 0x580000，余 30% |
| seg1 双锚 | vaddr **`0x3fc9c700`（六代不变）** / len **`0x08a8c`（与 gen12 持平）** / File offs **`0x00232fe8`**（gen12 `0x00232f88`，+96B，seg0 DROM 增长顺移）；boot 日志 paddr 预测 **`0x00242ff0`** = `0x00232fe8 + 8 + 0x10000` |
| 镜像 sha256 | **`03e59a07c8d09034e0ac564e770d1fe35ed32014a8706077f0927a4b69b5cad7`** |
| bootloader | 21,232 B 同尺寸；哈希 `e9498911…`（gen12 `1427993bda…`）——逐字节 diff 实证仅 39B：0x54 处内嵌构建时间串 6B（`13:26:51`→`20:02:38`）+ 0x52cf–0x52ef 尾部摘要 33B，零行为变化（既定非确定性归因，本轮新增实证） |
| 宿主测试 | 13/13 runner 全绿（push_sink 168 checks / 0 red；transfer_ui PASS，含 start_failed 优先级用例） |
| 构建 | fullclean exit 0；警告 **8 条与基线同集**（ui_pinpad×1 + app_book×7，全 unused-function） |
| flash.sh 防呆 | EXPECTED 同步 `03e59a07`（锚点注释块含 7.6 归因）+ gen12 bin 归档 `Read_Pico.bin.gen12-6e1d15c1`；双路径 dry run 重跑：锚对 → `sha256 OK` 放行至 esptool（沙箱无端口 exit 2）；篡改 → `FATAL … refusing to flash`（exit 1）✅ |
| APK | 重建（扫描重试加固，见 7.6.5）：25,046 B，sha256 `a688ccf977577128609cb57df563a14fb9559edae4ffef3f2bd7b54f87901662`，25/25 对表 + badging + 签名校验过 |

### 7.6.5 附带：APK 扫描重试加固（真机日志发现）

用户回传的 APK 日志里有连续两发 `扫描启动失败（code=1）`。Android 的
`SCAN_FAILED_ALREADY_STARTED`(1) 是**异步**经 `onScanFailed` 送达的（`startScan`
同步返 true），成因是上一轮 12s 超时后扫描器未停净就再起。修法：`onScanFailed`
对 code=1 只唤醒等待循环不判死刑；`scanForDevice` 改重试循环（最多 3 轮，
每轮先 `stopScan` 再等 200ms 重启），硬失败码照常立即报出。

### 7.6.6 本轮教训（诚实留档）

- gen12 的 7.2 修复形态四条里，三条（常驻注册 / 显式 start / 错误日志）经真机
  验证有效，但**第四条「adv 带 UUID」是未过 31B 预算就上的字段**，直接打飞广播。
  教训：凡改广播内容，先算字节账；且「UI 正常」从来不等于「射频正常」——假武装
  状态下倒计时照跑，只有扫才暴露。gen13 的 start_failed 出口就是堵这个感知盲区。
- 上一轮交付 APK 时我自己写明「BLE 状态机未经运行时验证」，用户真机一测就同时
  暴露了两侧问题（设备不可扫 + 手机侧重试缺失）。**未经运行时验证的交付物，
  第一次真机 run 必然伴随排错轮**，交付话术里应始终带这条边界。

## 7.7 gen14 修复附录：gen13 真机推送全链不通（双 bug：prep queue 深度 + notify 静默失败）

### 7.7.1 现象（gen13 真机测试，2026-10-19 夜）

gen13 治愈了 gen12 的不可扫描回归（广播 / 扫描 / 连接 / 服务发现 / MTU 247 全通），
但**推送链路一步都没走通**：设备端弹「内容格式不符，推送被拒绝」（= `PUSH_ERR_BAD_FRAME`），
手机端「等待设备 STATUS 超时」。串口关键行：

```
E (402329) NimBLE: ble_att_svr_insert_prep_entry rc=6
```

### 7.7.2 Bug 1 根因：NimBLE prepare-write 队列深度 4（源码 + sdkconfig 双重实证）

- `ble_att_svr_insert_prep_entry` rc=6 = `BLE_HS_ENOMEM`，来自
  `ble_att_svr_prep_alloc` 返回 NULL（ble_att_svr.c:2681-2700）：prep entry 池
  `os_mempool_init(&ble_att_svr_prep_entry_pool, MYNEWT_VAL(BLE_ATT_SVR_MAX_PREP_ENTRIES))`
  取空（ble_att_svr.c:3570-3586）。
- sdkconfig 实证 `CONFIG_BT_NIMBLE_ATT_MAX_PREP_ENTRIES=4`。
- 字节账（MTU 23 长写按每包 20 字节有效载荷切分）：83 B BEGIN = `ceil(83/20)` = **5 个**
  prepare entry；241 B DATA = **13 个**。默认 4 个必炸，且失败只打 LOGD（release 不可见）。
- 下游依赖：每个 prep entry 的 `bape_value` mbuf 经 `ble_hs_mbuf_l2cap_pkt()` 从
  **MSYS_1 池**领取（块大小 256 B，原 12 块）——只加队列深度会换个地方 ENOMEM，
  故 MSYS_1 同步 12 → 20。

### 7.7.3 Bug 2 根因：notify 空口失败全静默

`ble_gatts_notify_custom` 返回 0 仅代表 HCI 命令入本地队列；真正空口结果在
`BLE_GAP_EVENT_NOTIFY_TX` 的 `status`。gen13 对该 status 零处置 → 手机永远等不到
STATUS、设备侧毫无痕迹。时间线重建（串口双周期）证明 APK（MTU 247、BEGIN 一次写完）
与第三方 BLE 助手（无 MTU 协商）**两个独立客户端同症状**，确认与 Bug 1 并列的第二病灶。

### 7.7.4 用户三假设裁定（诚实留档）

| 假设 | 裁定 |
|------|------|
| ① PUSH_CTRL 特征 Max Attribute Length 不够 83 字节 | **说法修正、方向命中**：ESP-IDF NimBLE 的 `ble_gatt_chr_def` **没有** Max Attribute Length 字段，属性值经 access_cb 的 `os_mbuf_append` 动态处理；实质限制是 prep 队列深度 + MSYS_1 池 |
| ② PushProto.java 与 push_proto.c 字节序/填充不一致 | **排除**：逐字段对表 magic/op/ver/type/token(u16LE)/total(u32LE)/crc(u32LE)/sid(u32LE)/title[64] 全一致，PUSH_BEGIN_LEN=83 两侧一致；25/25 对表 + 168 项 host test 佐证 |
| ③ rc=6 是属性值太长或 prepare write 队列不够 | **命中（后半句）**：队列深度 4 < 5，非属性值长度问题 |

### 7.7.5 gen14 修法（设备侧，五处 + 诊断日志）

1. `CONFIG_BT_NIMBLE_ATT_MAX_PREP_ENTRIES` 4 → 16（sdkconfig / sdkconfig.defaults /
   sdkconfig.ci 三处同步，附字节账注释）。
2. `CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT` 12 → 20（同上三处）。
3. notify 失败可见 + 重投：mbuf 失败与 `rc != 0` 升级 `ESP_LOGW`；`NOTIFY_TX` 的
   status != 0 同样留痕（conn_handle/attr_handle）；100 ms oneshot esp_timer 重投
   `s_last_status`，每连接周期配额 3 次。**锁序纪律**：DISCONNECT 持 `s_lock` 只翻旗
   不 `esp_timer_stop`（timer 回调要拿同一把锁，会死锁）；fire 持锁发完、出锁后 delete
   （oneshot 已触发，允许回调内删自己）。
4. APK 侧加固：MTU < 32 明确失败（不再带 23 硬发后莫名超时）；RESUME 续传语义识别
   （offset/next_seq，next_seq=0 提示重新完整发送）。
5. 写入口全路径诊断日志：ctrl hdr rejected / begin decode rejected / begin precheck /
   data state / data decode rejected / op rejected / ACT_ERR——下次被拒日志直接指明
   败在哪个检查，不再只有一句「格式不符」。

**协议零变化**：本轮只动队列容量、失败可见性与客户端加固，帧格式一个字节未改。

### 7.7.6 gen14 门禁全表

| 门禁 | 结果 |
|------|------|
| 宿主机测试（runner 13/13，push_sink 168 checks） | 全绿 |
| APK 协议对表 | 25/25 |
| fullclean 重建 | exit 0，8 警告同基线（ui_pinpad 1 + app_book 7，零新增） |
| APK 重建 + badging + 签名 | 过（25,046 B，sha256 `3756ee89…`） |
| flash.sh EXPECTED 重锚 + 双路径 dry run | 正确 exit 2 / 篡改 exit 1 |
| 镜像锚 | `Read_Pico.bin` 4,049,984 B（0x3DCC40）sha256 `214541c2…`；+1,472 B（双操作数 vs gen13 4,048,512）；seg1 vaddr `0x3fc9c700`（七代不变）/ len `0x08a8c` 持平 / File offs `0x00233248`（gen13 `0x00232fe8`，+96B）；boot 日志 paddr 预测 `0x00243250`；版本串 `0.3.3-pico14`；bootloader 21,232 B 哈希 `0e84dd68…` |

### 7.7.7 本轮教训（诚实留档）

- **「默认配置够用」是隐性假设**：NimBLE 的 `BLE_ATT_SVR_MAX_PREP_ENTRIES=4` 对
  常规 GATT 够用，但本协议 83 B BEGIN + MTU 23 长写路径需要 5 个。凡引入长写队列
  依赖，先按「最大帧长 / 单包有效载荷」算 entry 数，再对池容量做门禁。
- **「API 返回 0」从来不等于「空口成功」**：notify 的双阶段语义（入队 ≠ 送达）与
  gen12 的「UI 正常 ≠ 射频正常」同构。凡 fire-and-forget 的射频操作，必须有
  TX 完成事件的 status 处置，否则失败全静默。
- **第三方工具与 APK 同症状 = 强信号**：本次两个独立客户端同病，直接排除了「APK
  客户端bug」分支，把侦查聚焦到固件侧——排错时应优先寻找这种交叉验证点。

## 7.8 gen15 修复附录：链路跑通但 VERIFY 判负（FATFS 目录项 size 未回写）

### 7.8.1 现象（gen14 真机，2026-10-19 夜）

gen14 的两个 bug 均已治愈——串口只有一句 `W (42631) push_sink: session refused err=4`，
**没有任何拒帧诊断**（`ctrl hdr rejected` / `begin decode rejected` / `data rejected`
全无），且 `conn_params_apply(FAST)` 与紧随的 notify 证明 **BEGIN 被接受并回了 READY**，
之后又有一帧 PROGRESS notify（DATA 已收），APK 也确实收到了 STATUS——**Bug 2 的
notify 链路已被真机证活**。链路走到 END → VERIFY 才判负：设备 UI「内容校验失败，
请重新发送」，手机端 CRC32 校验不符。`err=4` = `PUSH_ERR_CHECKSUM`。

### 7.8.2 根因（三重源码/sdkconfig 实证）

| 证据 | 结论 |
|---|---|
| `components/fatfs/vfs/vfs_fat.c` 全文 **零个 "flush"** | FATFS VFS **未注册 `.flush_p`**：`fflush()` 只把 newlib 缓冲推进 `f_write`，**不触发 `f_sync`** |
| sdkconfig `# CONFIG_FATFS_IMMEDIATE_FSYNC is not set` | `f_write` 之后也不补 `f_sync` |
| FatFs `ff.c:4345`：`f_close(){ res = f_sync(fp); ...}` | 只有 **close / 显式 fsync** 才回写目录项的 size |

`do_verify()` 原实现在 `s_part` 写句柄**仍打开**时 `fflush()` → `stat()` → `fopen("rb")` 回读。
目录项 size 从未更新，于是：`stat()` 读目录项拿到过期值 → `sz != received` → `ok=false`；
`fopen("rb")` 读到 0 字节 → `crc(0) != BEGIN.crc32` → 同样判负。两条口径同时炸在 err=4。

**为何 168 项宿主测试全绿也测不出来**：Linux 的 `write()` **即时更新 inode size**，
`fflush()` 后 `stat()` 立刻可见正确长度；FATFS 必须 `f_sync` 才更新目录项。这是典型的
「宿主 ≠ 目标」语义差，且**落在文件层——纯函数宿主测试零覆盖**。

**附带隐患（同一处代码）**：`CONFIG_FATFS_FS_LOCK=0`，`ffconf.h` 明文要求
"avoid illegal open, remove and rename to the open objects"，原代码正是写句柄未关就
二次 `fopen("rb")`。`do_commit()` 早就正确（`part_close()` 后 `rename`），只有
`do_verify()` 漏了——同一函数族内的**不一致**本身就是可查信号。

### 7.8.3 gen15 修法（三处）

1. **`do_verify()` 回读前 `part_close()`**（核心）：`f_close` 内部第一行即 `f_sync`
   （`ff.c:4345` 实证），目录项落盘 → `stat()`/回读取到真值；同时消除非法双开。
2. **新增 `part_sync()`**（`fflush` + `fsync`），NVS 检查点对齐调用，固定顺序
   「先 NVS 后 sync」——崩溃后 `.part` 可见长度只会落后于记账、绝不超前，续传安全。
   这让 `part_append` 那句「崩溃后 .part 不会比 NVS 记账多出未落地的字节」**真正成立**
   （此前只有 `fflush`，并不落地）。
3. **VERIFY 五项判据全量留痕**：`sz / received / total / crc_file / crc_want`，
   成败都打，一次真机即可归位败因。

### 7.8.4 附带修复：常量 session_id 造成的「一次失败 → 永久续传」卡死

真机日志形态：首推失败后**连续 4 次重连均为「连上 → 订阅 → 一次 notify → 立刻断开」**。
两个事实撞在一起：① APK `SESSION_ID` 是常量 1；② 设备端 `CONNECTED + BEGIN` 有
「同 sid 且 `has_part` → 直接 RESUME」分支。首推失败留下 `.part`（设计如此，供续传），
此后每次推送都命中该分支被判为续传，而 APK gen14 起遇非 0 offset 就放弃 → **永久卡死**。
**修法**：session_id 逐次生成（非零且每次不同）。新 sid → 判新会话 → 旧 `.part` 作废 →
正常 READY；续传能力不丢（同一推送断连重连 sid 相同仍走 RESUME）。

### 7.8.5 本轮教训（诚实留档）

1. **「宿主绿」不等于「目标绿」，尤其跨文件系统语义差**。本轮根因是 Linux 与 FATFS 对
   「写后多快可见长度」的约定不同。凡代码依赖**文件元数据**（size/mtime）而非仅内容，
   必须逐条确认目标 FS 的语义，不能拿宿主行为当围栏。
2. **文件层是宿主测试的盲区，必须点名登记**。纯函数测试覆盖编解码与状态机，而
   `part_*` / `do_verify` 属胶水、零覆盖——本轮 err=4 正落在此。已把「文件层只能真机验」
   写进已知项，避免下次再默认它被门禁罩住。
3. **同族函数内的写法不一致是可查信号**。`do_commit()` 先 `part_close()` 再 `rename`，
   `do_verify()` 却裸 `stat()`——同族不一致往往就是漏掉的那一条。
4. **「链路跑通了」与「结果正确」是两件事**。gen14 修好了传输（BEGIN 接受、notify 到达），
   gen15 才修好正确性（VERIFY）。真机验收必须走到**最终业务结果**（开书），
   停在「通知到达」会放过这一整类错误。

## 7.9 gen16 修复附录：首推成功后「再推全失败 + 弹旧书」（FatFs rename 不覆盖 + 提交失败仍开书）

### 7.9.1 gen15 真机：err=4 断根，第一次端到端推送成功

```
I (67612) push_sink: verify ok: sz=30 crc=0x15542824
```
首次推送「哈哈」走完 BEGIN → READY → DATA → END → VERIFY → 生成 push.txt 自动开书。
**gen15 的「FATFS 目录项 size 未回写」确认治愈**，这是本项目第一次端到端推送成功。
另两处诊断也一次归位：`begin precheck err=9 type=4`（单词卡无词典，预期行为）。

### 7.9.2 根因（两条叠加，日志铁证）

```
I (67612) push_sink: verify ok: sz=30 crc=0x15542824
E (67618) push_sink: commit rename: File exists
I (67653) book: open start ...            ← 旧 push.txt 照样被打开
```

1. **FatFs 的 `f_rename` 不覆盖已存在目标**（目标存在即 `FR_EXIST` → `EEXIST`）——
   POSIX 的「原子替换」语义它没有。首轮 `push.txt` 不存在所以成功；**第二轮起 rename
   必败** → `reply_err(ERR(5))`，APK 渲染成「TF 卡不可用」——**文案误导**（实为 EEXIST），
   用户据此去查卡，方向全错。
2. **提交失败仍然开书**：`do_verify()` 递归进入 `exec_actions` 时 `COMMIT` 与
   `OPEN_BOOK` **同位携带**，而 `do_commit()` 返回 `void`、失败无法短路 →
   旧 push.txt 被照样弹出（用户现象：「内容永远是首次写入的哈哈」）。

### 7.9.3 gen16 修法

1. `do_commit()` 改名前先 `remove(PUSH_TEXT_PATH)`，补上 FatFs 缺失的覆盖语义
   （同目录 rename 只挪目录项，remove→rename 丢失窗口仅掉卡场景）。
2. `do_commit()` 改**返回 bool**；`exec_actions` 里提交失败即清除
   `OPEN_BOOK / WORD_CARD`——**内容没落盘就不开书**。
3. RESUME 通道加 sid 诊断日志（审计裁定：分支保留、改造为诊断通道而非死循环入口）。
4. APK 的 `ERR(5)` 文案改诚实：「设备存储写入失败（TF 卡缺失、写保护或提交失败）」。

**协议零变化**，帧格式一字节未动。

### 7.9.4 两个「非 bug」的诚实定性（用户报的问题 2 / 3）

- **问题 2「切页面 / 弹书后蓝牙断开」= 设计行为**：日志 `app_loop: page -> 图书 Books`
  表明开书即离开「接收推送」页 → 本轮武装结束（停广播）；且 APK 推完主动
  `closeQuietly()`。设计本就是「每轮推送 = 扫描 → 连接 → 推送 → 断开」，
  `MAX_CONNECTIONS=1`、推送书打开即接管屏幕。
- **问题 3「单词卡报无字典」= 预期行为**：`begin precheck err=9 type=4`，
  设备未放 `dict.bin`（裁定 b：不降级渲染）。用户把 dict.bin 放 TF 卡 `dict/` 即可。

### 7.9.5 本轮教训（诚实留档）

1. **同族 API 的 POSIX 假设必须逐条验**：`rename()` 的「原子覆盖」是 POSIX 契约，
   FatFs **不提供**。凡用跨层标准库函数，先确认目标实现的契约边界——
   本轮是 gen15 之后**文件层连续第二个**仅真机暴露的缺陷。
2. **void 返回值 = 无法短路**：动作位（act）同位携带多个动作时，任何一个动作失败
   都必须能掐掉后续动作。`do_commit()` 返回 void 是「失败不可传播」的结构性缺陷。
   纪律：**动作执行器统一返回成败，编排层按成败裁剪后续动作位**。
3. **错误文案要能指路，不能指错路**：ERR(5)「TF 卡不可用」把 EEXIST 说成卡的故障，
   用户据此查卡。错误码的**文案**也是诊断链路的一环，语义必须覆盖所有触发分支。
4. **「第一次成功」是强信号，也是陷阱**：首轮成功容易让人以为整条路径验过了，
   而**幂等/覆盖语义**只有第二轮才走得到。真机验收清单必须含「重复执行同一操作」。

## 7.10 gen17 修复附录：单词卡 ERR(9) 误报（探针挂错函数 + gen16 判错更正）

### 7.10.1 gen16 的判读错误（诚实更正）

gen16 把 ERR(9) 定性为「设备没放 dict.bin，属预期行为」，并让用户去放词典。
**这是错的**。用户举证：TF 卡 `/sdcard/dict/` 下确实有 dict.bin，阅读时长按查词完全正常。
词典一直在，判读把原因赖到了卡上。**更正记录在此，不遮掩。**

### 7.10.2 用户三假设逐条排除（均有实证）

| # | 假设 | 结论 | 依据 |
|---|---|---|---|
| 1 | 路径拼写 / 缺 `/sdcard/` 前缀 | 排除 | `DICT_DIR` 恒为绝对路径 `"/sdcard/dict"`（`dict_lookup.c:24`），扫描拼成 `/sdcard/dict/<name>.bin` |
| 2 | 文件系统并发锁 / 上一条推流失败遗留 / UI 独占 SD | **三重排除** | ① ERR(9) 在 37681ms，**早于** rename 失败（67612ms），时序上不可能是其后果；② `CONFIG_FATFS_FS_LOCK=0`，FatFs 未启用锁；③ `dict_ready()` 只读内存指针，**不做任何 fopen** |
| 3 | VFS 挂载状态 | 待实测 | 已加 errno 日志，本轮可证实/证否 |

### 7.10.3 真根因：探针语义错配

```c
bool dict_ready(void) { return s_dict.file != NULL && s_dict.sparse != NULL; }
```
语义是「**句柄当前是否已打开**」，不是「卡上有没有文件」。词典**惰加载**——只有查词或
预热才真正 fopen。

- 长按查词正常，因为 `dict_popup.c:191-196`：`if (!dict_ready()) ready = dict_open();`
  ——查词路径**自己会打开**。
- 推送单词卡误报，因为 BEGIN 前置闸门只**读**状态、`dict_available()` 从不尝试打开。

**推论：开机后一次词都没查过时，单词卡必误报 ERR(9)。**
日志自洽：`begin precheck err=9`（37681ms）之后才出现 `book: dict warm ok=1`（67636ms）——
warm 成功发生在报错**之后**，证明报错那一刻词典确实尚未打开。

### 7.10.4 gen17 修法

1. 探针 `dict_ready` → **`dict_warm`**（已打开则 true，否则尝试 `dict_open()`，且失败
   **不武装退避**——其注释正是为「卡片可能还没就绪」而写）。
   不用 `dict_open()` 是因为它失败后会进 `DICT_OPEN_BACKOFF_MS` 退避，
   会让单词卡在一段时间内一直 ERR(9) 且不重试。
2. 词典侧打印完整绝对路径 + errno：`opendir` 失败、`fopen` 失败、非 RPD1、打开成功。
3. push_sink 的 err=9 日志改诚实措辞：「探针此刻判定不可用，不等于卡上没有词典」。

**协议零变化**。

### 7.10.5 本轮教训（诚实留档）

1. **「状态查询」与「能力查询」是两个语义，混用即事故**。`dict_ready()` 是状态查询
   （「句柄当前是否已打开」），被当成能力查询（「卡上有没有文件」）用了。
   纪律（审计表述）：**凡跨模块注册的探针/回调，调用方必须做语义校验**——
   接口注释与调用点同时写明它回答的是「状态」还是「能力」，并确认这正是调用方要问的。
   同名近义的三个函数 `dict_ready / dict_warm / dict_open` 语义各不同
   （是否已打开 / 确保打开且不退避 / 打开且失败退避），**按语义选，不按名字选**。
2. **环境类归因是最后选项，不是第一选项**（审计点名入册）：
   gen16 未验证「文件是否存在」就下了「卡上没词典」的环境归因——**断言无证**。
   用户的证据（同一词典一处可用、另一处不可用）本应让任何环境归因当场破产：
   物理文件在、查词能用，环境侧无任何变量可解释差异，**差异必然在代码路径**。
   纪律：**先自查代码路径，再怀疑用户环境**；尤其「同一资源两种路径行为不同」时，
   先假设是自己用错了。此条与「同族不一致就是信号」是同一哲学的两个面。
3. **用户举证优先于我的推断**（与第 2 条同源）。用户给出「资源存在 + 另一条路径
   可用」的证据时，正确动作是立刻查代码找「为什么同一资源在两处表现不同」，
   而不是把差异常识化归为「用户环境有问题」。**判错要记录，不遮掩。**
4. **诊断日志必须覆盖「尝试了什么」而不只是「失败了什么」**：
   ERR(9) 只说「无词典」，把「卡上没有」「路径错」「文件非 RPD1」「未打开」
   四种原因折叠成一个码——这就是用户被误导去查卡的直接原因。
