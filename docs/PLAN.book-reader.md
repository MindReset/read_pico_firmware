# Plan: 小纸 Pico 图书阅读器 + WiFi 传书 + 墨水屏触控交互层

> 状态：已评审。未决项全部已答（三键接管 → 4b；`storage` 保留 5 MB；晃动翻页兜底可）。执行者按 Phase 顺序推进；用户不在线时按 **Decisions** 执行，可逐条推翻。
> 执行者：任意模型。本计划按 Phase 拆分，每个 Phase 独立可编译、可烧写、可在 `docs/HANDOFF.local.md` 交接。
> 本文是设计文档，不是账本：进行中状态、烧写命令原文、日志片段只写 `docs/HANDOFF.local.md`（模板见 [HANDOFF.md](HANDOFF.md)）。每个 Phase 完成后在对应标题下追加一行“状态：完成于 `<sha>`”。
> 规则、契约、术语见 [AGENTS.md](../AGENTS.md)；环境与烧写见 [ONBOARDING.md](ONBOARDING.md)。

## TL;DR

在现有 14 页 demo 固件上新增：
1. **图书页（菜单 #15，`app_book`）**：读 `/sdcard/books/*.txt|*.epub`（有卡）或内置 FAT 分区 `/flash/books`（无卡，单文件 ≤ 1 MB，仅供测试），书架 → 阅读 → 目录 → **多本书进度记忆**（NVS blob，按路径哈希）。阅读页：屏幕左/中/右三区 = 上一页/工具条/下一页；页脚进度条；实验性“晃动下一页”（默认关，可开关）。Phase 1 只收 KEY1（下一页），三触控键 左/中/右 = 上一页/工具条/下一页 在 4b 与手势层一起接管。txt 支持 UTF-8/GBK；epub 走 ROM miniz 解压 + OPF spine + ncx/nav 目录 + 去标签纯文本流。
2. **传书页（菜单 #16，`app_transfer`，排最后）**：设备开 WiFi 热点，手机浏览器打开 `http://192.168.4.1` 上传到当前书源根目录（有卡不限大小；无卡限 1 MB/单文件、总量看分区余量）。不做蓝牙（S3 仅 BLE，手机无原生“发送文件到 BLE 设备”路径）。
3. **交互层（两步走）**：4a 在 `main/ui/ui_gesture.c` 做页内识别器（PRESS/TAP/LONG_PRESS/SWIPE），只接图书页并真机验收；4b 再一次性改主循环：可选 `on_gesture`、菜单改抬起提交、`owns_keys` 三键接管 + `request_menu`。准则写入 `docs/INTERACTION.md`。

前置条件：**factory 分区 2 MB → 4 MB**（当前 app 1.89 MB = 94%，WiFi 栈 +0.7–0.9 MB 放不下）；原 5 MB 空闲 spiffs 行改为 5 MB `storage`(fat) 供无卡测试书（固件无 OTA，不需预留）。

不涉及 MuxOS / CrossMux。

---

## 关键事实（Discovery 结论，供执行者直接引用）

| 事实 | 出处 |
| --- | --- |
| app 1,888,320 B / factory 0x200000 → 94% | `build/` 产物、`partitions_16M.csv` |
| spiffs 偏移自动（跟在 factory 后）、5 MB、无人使用；16 MB flash 余量充足。**无 OTA**：分区表无 otadata/ota_x，`main/`+`components/` 无 `esp_ota_*`/`esp_https_ota` 引用 | `partitions_16M.csv`、grep |
| WiFi/BT/lwip/esp_netif/esp_http_server **不在 build_components** 里；加入 REQUIRES 即拉入，`CONFIG_ESP_WIFI_ENABLED` 随之出现 | `build/project_description.json` |
| ROM 自带 `tinfl_decompress` / `tinfl_decompress_mem_to_mem` / `_to_heap` / `_to_callback`（0x40000828+） | `build/Read_Pico.map` |
| PSRAM malloc 已开，`MALLOC_ALWAYSINTERNAL 16384`；大缓冲用 `heap_caps_malloc(n, MALLOC_CAP_SPIRAM \| MALLOC_CAP_8BIT)` | `sdkconfig.h`、`app_reading.c` |
| FatFs LFN=255、CP437、`API_ENCODING_ANSI_OEM` → 中文文件名可能显示/打开异常，需真机验证 | `sdkconfig.h` |
| SD 挂载 `max_files=4`、`/sdcard`、SDMMC 1-bit 高速 | `components/read_pico/read_pico_sd.c` L25/L75/L91 |
| 内建字体是子集（UI 字串 + charset.txt），缺字 → `stbtt_FindGlyphIndex` 返 0 → .notdef；读书必须 TF 卡 TTF | `main/font/ttf_font.c` L1656、`tools/gen_builtin_font.py` |
| `gen_builtin_font.py` 只扫 `main/ui, main/app, main/apps, main/factory, main/assets, …`；新目录 `main/book/` **不在扫描列表** | `tools/gen_builtin_font.py` SCAN_DIRS |
| 主循环只在 `pressed` 上升沿分发 `on_touch`；无长按/滑动/抬起事件；`app_loop.c` 文件头无 Frozen 段 | `main/app/app_loop.c` L186–260 |
| 菜单当前“按下即切页”（`ui_menu_hit_test` 在 pressed 分支） | `app_loop.c` L239–251 |
| 均衡档：整页 GL16、`APP_PAGE_FORCE_FULL 1`、`APP_GC16_EVERY 14`、动态区 DU | `main/app/app_config.h` |
| `fatfs` 与 `wear_levelling` 已在 build_components（fatfs 依赖拉入）→ 内置 FAT 分区用 `esp_vfs_fat_spiflash_mount_rw_wl` 几乎不加 flash 体积 | `build/project_description.json` L28 |
| 三触控键：`ui_key_hit_test` 在 `UI_KEY_AREA_TOP 1300` 以下按 `UI_KEY_PITCH 160` 分三段；KEY2=全局 GC16、KEY3=全局菜单，页面只收 KEY1 | `main/ui/ui_menu.c` L166、`app_loop.c` L195–238 |
| 晃动检测已有现成路径：`sc7a20h_activity_config(acc, ths_mg, dur)`（AOI2）+ 轮询 `sc7a20h_read_events()` 看 `aoi2_src & 0x40`；进/离页用 `read_pico_sensor_wake/sleep` | `main/apps/app_axis.c` L1190、L1414–1446、L1310–1333 |
| NVS 分区 0x5000 = 20 KB，可用约 16 KB；每本书进度 blob ≈ 96 B → 上限约 100–150 本 | `partitions_16M.csv` |
| sdkconfig 注释：温度漂移 ~20℃ 可致 120 MHz PSRAM 随机出错，且温度补偿被迫关闭 → **WiFi 发热是真实风险** | `sdkconfig.defaults` L12–16 |
| 本机无 idf.py；用 `docker run espressif/idf:v6.1` 编译；串口 COM5；宿主 python `C:\Users\abc75\miniforge3\python.exe`（esptool 5.4.0） | [ONBOARDING.md](ONBOARDING.md) §3.3 / §3.4 |

---

## Phase 0 · 基线、分区扩容、交接脚手架（阻塞后续所有 Phase）

0.0 **计划落地**：把本计划写入 `docs/PLAN.book-reader.md`（入库），AGENTS.md 目录表加一行；后续每 Phase 完成在该文件对应标题后追加“状态：完成于 `<sha>`”。
　　状态：完成（本文件即产物；AGENTS.md 目录表已加行）。
0.1 建分支 `feat/apps-book-reader`（`git rev-parse --short HEAD` 记入账本）。
0.2 在 `docs/HANDOFF.local.md` 顶部按 `docs/HANDOFF.md` 模板追加条目「图书阅读器 + 传书 + 交互层 · Phase 0 开工」，“未决问题”栏抄本计划 **Decisions**。
0.3 **设备基线**（执行者需终端）：`esptool --port COM5 --after hard-reset read-mac` 触发复位 → pyserial 读 10 s 开机日志；记录 `Loaded app from partition at offset 0x10000`、`UI ready on 概览`、heap/PSRAM 行、HPM 行。TF 卡插入后进「TF 卡」页确认 `mounted`。在卡上建 `/sdcard/books/`（放 1 本 UTF-8 txt、1 本 GBK txt、2–3 本 epub：一本带 toc.ncx、一本仅 nav.xhtml；至少一本用中文文件名）与 `/sdcard/fonts/<CJK>.ttf`，并在「字体」页选中。另准备 1 本 < 1 MB 的 txt 作无卡测试书。
0.4 **分区**：`partitions_16M.csv` 第 4 行 factory Size `0x200000` → `0x400000`；第 5 行 `spiffs, data, spiffs, , 0x500000` 改为 `storage, data, fat, , 0x500000`（保留 5 MB；固件无 OTA——无 otadata/ota_x 分区、无 `esp_ota_*` 调用——不必预留；偏移自动 0x410000，结束 0x910000；nvs/phy 偏移不变，设置得以保留）。`sdkconfig.ci` 无需改（引用同一 csv）。
0.5 编译并全量烧写（分区表变了必须 `@flash_args` 全量），确认启动正常；账本记录烧写命令原文与日志片段。
0.6 新建 `docs/INTERACTION.md`（墨水屏交互准则，见 Phase 4 §4.0），供后续 Phase 引用；AGENTS.md 目录表加一行指向它。

验证：`idf.py build` 输出的分区余量 ≥ 50%；真机启动到概览页；`git status` 只含预期文件。

---

## Phase 1 · txt 阅读器（菜单 #15）

### 1.1 模块划分（新建 `main/book/`，纯逻辑，不含 UI 字串）

| 文件 (N) | 职责 | 参考 |
| --- | --- | --- |
| `main/book/book_store.h/.c` | 书源根目录与容量策略：`book_store_roots(root_t out[2], int* n)` 返回可用根（有卡：`/sdcard/books` 优先 + `/flash/books`；无卡：仅 `/flash/books`）；`book_store_upload_root(root_t*)` 给传书页用（有卡→SD，无卡→内置）；`book_store_file_limit(root)`（SD 无限；内置 `BOOK_STORE_FLASH_FILE_MAX 1 MB`）；`book_store_free_bytes(root)`；内置分区**首次使用时**惰性 `esp_vfs_fat_spiflash_mount_rw_wl("/flash", "storage", {format_if_mount_failed=true, max_files=4, allocation_unit_size=4096}, &wl)`（首次格式化约 1–2 s，调用方先在页脚 DU 写“初始化内置存储…”）；`is_flash` 标记供书架画“内置”chip | `components/read_pico/read_pico_sd.c` 挂载参数 |
| `main/book/book_progress.h/.c` | 多本书进度：NVS 命名空间 `rp_books`，键 `b_%08x`（FNV-1a 32 对 `path` 哈希），值 blob `{u32 file_size, u16 chapter, u32 byte_off, u8 px, u8 pct, u32 last_open_s}`；`book_progress_load(path, size, out*)`（size 不匹配视为不同文件 → 不恢复）、`_save()`、`_clear(path)`、`_last_path(buf)` / `_set_last_path()`（键 `last`）；写满（`ESP_ERR_NVS_NOT_ENOUGH_SPACE`）只打 WARN 不报错 | `main/settings.c` open/set/commit/close 模式 |
| `main/book/book_source.h/.c` | 书源抽象：`book_open(path)` / `book_close()` / `book_chapter_count()` / `book_chapter_title(i, buf, cap)` / `book_chapter_load(i, char** utf8, size_t* len)`（PSRAM，调用方释放） / `book_total_bytes()` / `book_chapter_byte_offset(i)` / `book_kind()`；按扩展名分派到 txt / epub 后端 | — |
| `main/book/book_txt.c` | 64 KB 分块读文件；编码判定：BOM → UTF-8；否则扫描前 64 KB 是否合法 UTF-8 → 否则视为 GBK；章节切分：逐行匹配「第[零一二三四五六七八九十百千万两0-9]+[章节卷回集部篇]」或「Chapter\s*\d+」且行长 ≤ 40 字节，得到 (offset, title) 表（上限 2048 章）；不足 2 章或单章 > 96 KB → 按 ~48 KB 在段落边界切“第 N 段”。章节表存 PSRAM | `app_sd.c` 文件遍历、`app_reading.c` 解析循环 |
| `main/book/gbk.h/.c` + `main/book/gbk_table.h` (N, 生成) | `gbk_to_utf8(src, n, dst, cap)`；表由 `tools/gen_gbk_table.py`（N）用 Python `codecs` 生成 `const uint16_t` 数组（0x8140–0xFEFE 双字节区，≈ 24k 项 ≈ 48 KB flash），文件头注明“生成文件勿手改” | 同 `fallback.h` 的“生成勿改”约定 |
| `main/book/book_layout.h/.c` | 从 `app_reading.c` 的 `blk_t`/`wrap_one()`/`take_line()`/`rebuild_pages()` **复制并泛化**（不改 `app_reading.c`）：输入 `blk_t{kind,text}` 数组 + 字号 px + 版心矩形 → 页表 `{blk, off}`（PSRAM 动态数组，单章上限 4096 页）；`book_layout_draw_page(fb, page, rect, px)`；`book_layout_page_for_offset(byte_off)`；`book_layout_page_start_offset(page)`。宽度测量走 `ttf_text_width_px`，绘制走 `ttf_draw_text_px` | `main/apps/app_reading.c`、`main/font/ttf_font.h` |

### 1.2 页面 `main/apps/app_book.c` (N)

- 导出 `const app_desc_t app_book = { .title="图书 Books", .detail="TF 卡 txt / epub 阅读", .enter_full=true, .render, .present, .on_enter, .on_exit, .on_touch(4a 起内部改走 ui_gesture), .on_key, .on_tick, .area_hint }`。（`owns_keys` 在 4b 才加。）
- 状态机 `s_view ∈ {SHELF, READING, TOC, NO_FONT}`：
  - **SHELF**：`on_enter` 起 `read_pico_sd_start_probe()`，`on_tick` 每 500 ms 轮询 `read_pico_sd_get_info`（复用 `app_sd.c` 的 `SD_PROBE_POLL_MS` 模式）；探卡结束后按 `book_store_roots()` 扫描各根匹配 `.txt/.epub`（大小写不敏感），上限 64 条，按名排序，存 `name[64]/path[160]/size/is_flash`；列表复用 `app_font_pick.c` 的分页布局（`UI_BTN_H` 行、`ctx->leaf`、上一页/下一页 `ui_bar_rect`）。每行右侧：有进度的书标“xx%”，内置存储的书标“内置”chip。顶部副标题显示当前书源与余量（“TF 卡 · 剩余 12.3 GB” / “内置存储 · 剩余 4.6 MB · 单文件 ≤ 1 MB”）。空列表提示“把 txt / epub 放到 books 目录，或用「传书」页上传”。**Phase 1 键位**：KEY1 = 下一叶（页面只收 KEY1；KEY2/KEY3 仍是全局强刷/菜单）。
  - **READING**：无 header；版心 `x=UI_MARGIN, y=24, w=ui_content_width(), h=UI_LOCK_HEIGHT-24-72`；底部 72 px 页脚 = **进度条**（全宽 `ui_hairline` 轨道 + 3 px 黑色已读段，位置 = 全书字节百分比，`UI_GRAY_LIGHT` 小刻度标章节边界，章节 > 40 时省略刻度）+ 一行 `UI_PX_CAPTION`“章节名（截断） · 本章 p/q · 全书 z%”。
    - **屏幕三区**：左 30% = 上一页；右 30% = 下一页；中 40% = 呼出/收起工具条。翻页零装饰（高频）。**Phase 1 键位**：KEY1 = 下一页；KEY2/KEY3 保持全局。三键“左/中/右 = 上一页/工具条/下一页”在 **4b** 与手势层一起落地（见 §4.2）。
    - **工具条**（浮层，占版心下半 2 行按钮，GL16 弹出/收起）：目录 · 字号− · 字号+ · 晃动翻页 开/关 · 清除本书进度 · 书架。（“强刷”“演示菜单”两键在 4b 接管三键后再加入；Phase 1 仍走全局 KEY2/KEY3。）工具条可见时进度条变为可点：点轨道任意位置 → 跳到该百分比对应章节的开头附近（章内按字节比例定位再 `page_for_offset`），低频动作 → GL16 定稿。
    - 字号档位复用 `READING_PX_MIN/MAX/STEP/DEFAULT`（36–72，默认 48）；字号按书记在进度 blob，全局默认记 `settings` 键 `bk_px`。
    - **晃动下一页（实验）**：默认关；开关记 `settings` 键 `bk_shake`。开启且 `ctx->sensor_ready` 时 `on_enter`/切开关时 `read_pico_sensor_wake(acc)` + `sc7a20h_activity_config(acc, BOOK_SHAKE_THS_MG, BOOK_SHAKE_DUR)`（初值 500 mg / 2，`app_axis.c` 演示用 160 太灵敏；真机调好后写进 INTERACTION.md）；`on_tick` 每 40 ms `sc7a20h_read_events()`，`aoi2_src & 0x40` 上升沿计一次，**600 ms 内 ≥ 2 次** = 一次晃动 → 下一页；随后 1500 ms 冷却；手指按着或翻页后 800 ms 内忽略。关闭或 `on_exit` → `sc7a20h_aoi_config(AOI2, 全 0)` + `read_pico_sensor_sleep(acc)`（沿用 `app_axis.c` `exit_tier` 做法，避免影响睡眠页的拿起唤醒）。Frozen：只翻下一页，不翻上一页；默认关。
  - **TOC**：章节列表 10 行/叶，当前章高亮，点按跳转并保存进度；`ui_bar_rect` 三键：上一叶 / 返回阅读 / 下一叶；Phase 1 KEY1 = 下一叶。
  - **NO_FONT**：`ttf_font_is_builtin()` 为真时在 READING 顶部画一条提示“内建字体缺字，请在「字体」页选择 TF 卡字体”，并给一个按钮 `ctx->request_app = app_at(索引 of app_font_pick)`（通过 `app_registry` 查找，不 extern 引用其他页文件）。无卡时提示改为“无 TF 卡：内建字体只含界面用字，正文可能缺字”。
- 翻页刷新策略（照抄 `app_reading.c` 的 `refresh_turn()`）：普通翻页 GL16 局部（版心+页脚），每 `APP_GC16_EVERY` 次翻页 GC16 整屏；字号连按走 DU 预览、400 ms 后 GL16 定稿。预渲染：复制 `prep_task`（core 1、优先级 3、12 KB 栈）+ `s_next_fb`（PSRAM）+ `s_draw_lock` 互斥模式，翻到下一页时直接 `memcpy` 再推屏。**注意 TTF 缓存有全局状态，任何绘制都要持锁。**
- 章节切换：当前章末页再下一页 → `book_chapter_load(i+1)` → 重排 → 第 1 页；上一页对称。加载时先在页脚 DU 写“加载中…”。
- **进度（多本）**：打开某书 → `book_progress_load(path, size)` 命中则直接进 READING 并 `book_layout_page_for_offset(byte_off)` 定位（章节号越界则回第 0 章）；未命中从头开始。保存时机：`on_exit`、返回书架、目录/进度条跳转、每 8 次翻页、切字号；同时 `book_progress_set_last_path()`。`on_enter` 若 `last` 路径仍存在 → 直接续读该书，否则进书架。书架长按条目 → “清除该书进度”（Phase 4a 手势就绪前先做成工具条内按钮）。

### 1.3 其它改动 (M)

- **不改 `main/app/app.h` / `main/app/app_loop.c`**（用户决定：主循环改动集中到 4b 一次做）。Phase 1 页面只收 KEY1，KEY2/KEY3 保持全局。
- `main/settings.h/.c`：新增 `app_settings_book_px()/set_book_px()`（全局默认字号，键 `bk_px` u8）与 `app_settings_book_shake()/set_book_shake()`（键 `bk_shake` u8，默认 0）。多本进度不进 settings，走 `book_progress.c` 独立命名空间。
- `main/app/app_registry.c`：`extern const app_desc_t app_book;` 并追加到 `s_apps[]` 末尾（成为第 15 项）。
- `main/CMakeLists.txt`：SRCS 加 `apps/app_book.c`、`book/book_store.c`、`book/book_progress.c`、`book/book_source.c`、`book/book_txt.c`、`book/book_layout.c`、`book/gbk.c`；`INCLUDE_DIRS` 加 `"book"`；`main_requires` 加 `wear_levelling`（已在构建图，只是显式依赖）。
- `components/read_pico/read_pico_sd.c` L91：`max_files` 4 → 8（书 + 字体 + zip + 上传临时文件）。
- `tools/gen_builtin_font.py`：UI 字串全部放在 `app_book.c`（已在扫描目录）；**不要**把 `main/book` 加进 SCAN_DIRS（避免把 GBK 表当字串扫入）。运行脚本重生成 `main/assets/builtin.ttf`，确认 ≤ 700 KB。
- `AGENTS.md` demo 页表加 `app_book.c` 行；`README*.md` 三语页面表各加一行（文案：中文“小纸 Pico”，英/日 “Read Pico”）。

验证（真机）：有卡：书架列出 3 类文件并显示余量；UTF-8 txt 翻页 20 次无崩溃、进度条前移、页脚百分比递增；GBK txt 中文正确；屏幕左/中/右三区 = 上一页/工具条/下一页，KEY1 = 下一页，KEY2/KEY3 仍为全局强刷/菜单；断电重启自动回到同一本同一页；换另一本再切回，两本进度各自保留；目录跳转、进度条点跳；字号 ±；晃动开关开→连晃两下翻页、单次轻晃不翻、按住屏幕时不翻、关→传感器休眠（串口无 I2C 轮询日志）；未选 TF 字体时出现提示条并可跳到字体页；连续翻页 14 次观察一次 GC16 定稿。无卡：书架只列内置存储，首次初始化提示，放入 < 1 MB 测试书（Phase 3 前先用 esptool 写分区镜像或等 Phase 3 上传）可读。`idf.py size` 记录增量。

---

## Phase 2 · epub 支持（依赖 Phase 1）

| 文件 (N) | 职责 |
| --- | --- |
| `main/book/zip_reader.h/.c` | 只读 zip：从文件尾部 66 KB 内找 EOCD(0x06054b50) → 中央目录遍历（名、method 0/8、压缩/原始大小、本地头偏移）→ `zip_find(name)`、`zip_entry_size()`、`zip_extract(entry, dst, cap)`：读本地头跳过 name+extra，method 0 直接读，method 8 用 ROM `tinfl_decompress()`（`TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF`，输入整段读入 PSRAM，输出预分配 = 原始大小）；单条目上限 2 MB；条目数上限 512。头文件名在 v6.1 容器内核实：`find $IDF_PATH/components/esp_rom -name 'miniz.h'` |
| `main/book/book_epub.c` | `META-INF/container.xml` → `full-path` 取 OPF 路径；OPF：manifest `id→href` 表 + spine `idref` 顺序（上限 512 项）；目录：优先 `toc.ncx`（`navPoint` → `navLabel/text` + `content src`），其次 nav.xhtml（`epub:type="toc"` 内 `<a href>`），都没有则用 spine 序号“第 N 节”。href 去 `#fragment`、相对 OPF 目录拼接、URL 解码 `%xx`。目录项映射到 spine 索引；`book_chapter_load(i)` = 解压 spine[i] → `html_to_blocks()` |
| `main/book/html_text.h/.c` | 单遍 HTML→文本块：跳过 `<head>/<style>/<script>` 及注释；块级标签（p/div/h1–h6/li/tr/br/hr/blockquote）切块，h1–h3 标为标题块；折叠空白；实体解码（amp/lt/gt/quot/apos/nbsp + `&#NNN;`/`&#xHH;`）；输出 UTF-8 到 PSRAM 缓冲 + `blk_t` 表 |

- `book_source.c` 按 `.epub` 分派；SHELF/TOC/READING 无需改 UI，仅目录数据源不同。
- 可选（推荐）：`tools/book_host_test.c`（N）用宿主 gcc 编译 `html_text.c` + `book_txt.c` 章节切分 + `gbk.c`，跑几份夹具，便于在无设备时快速迭代；zip/inflate 只在真机测。

验证：三本 epub 均能列出目录、跳转、翻页；缺 ncx 的 epub 回退到 nav；含大量 `<div>` 的排版不出现空页；断电续读到同一 spine 项/偏移；`idf.py size` 记录。

---

## Phase 3 · WiFi 热点传书（菜单 #16，可选，依赖 Phase 0；与 Phase 1/2 可并行）

### 3.1 组件 `components/read_pico_transfer/` (N)

- `CMakeLists.txt`：REQUIRES `esp_wifi esp_netif esp_event esp_http_server nvs_flash fatfs esp_timer`；`EMBED_TXTFILES upload.html`。
- `include/read_pico_transfer.h`：`read_pico_transfer_start(const read_pico_transfer_cfg_t*)`、`_stop()`、`_get_status(read_pico_transfer_status_t*)`（state、sta_count、cur_name[64]、cur_bytes、cur_total、done_count、last_error）。
- `read_pico_transfer.c`：`esp_netif_init` + 默认事件循环（若未创建）→ `esp_netif_create_default_wifi_ap` → `esp_wifi_init/set_mode(AP)/set_config`（SSID `ReadPico-XXXX`，XXXX = MAC 末两字节；WPA2 口令固定 `readpico` 并显示在屏上；`max_connection 2`）→ `esp_wifi_start` → `httpd_start`（stack 8 KB，`max_uri_handlers 4`）。
  - `GET /` → 内嵌 `upload.html`（纯 HTML+JS：`<input type=file multiple>`，逐个用 `fetch(PUT /upload?name=<encodeURIComponent>)` 发原始字节，显示进度；不解析 multipart）。
  - `PUT /upload?name=` → 目标根由 `book_store_upload_root()` 决定（有卡 → `/sdcard/books`；无卡 → `/flash/books`）；文件名清洗：URL 解码、去路径分隔符与 `..`、长度 ≤ 120、仅 `.txt/.epub`；**先查 `Content-Length`**：超过 `book_store_file_limit(root)`（内置 1 MB）或大于 `book_store_free_bytes(root)` 直接回 413/507 并附 JSON 说明，不开始接收；`httpd_req_recv` 16 KB 块（PSRAM 缓冲）写到 `<root>/<name>.part`，完成后 `rename` 覆盖；校验失败返回 400。
  - `GET /` 页面顶部显示服务端回传的 `GET /info` JSON（书源类型、剩余空间、单文件上限），让手机端上传前就能看到限制。
  - `GET /list` → JSON 文件列表（可选）。
- `_stop()`：`httpd_stop`、`esp_wifi_stop/deinit`、销毁 netif，释放内存。

### 3.2 页面 `main/apps/app_transfer.c` (N)

- `app_desc_t app_transfer = { .title="传书 Transfer", .detail="WiFi 热点，手机浏览器上传", .enter_full=true, … }`。`on_enter` 起热点；`on_exit` 停。**组件依赖方向**：`read_pico_transfer` 组件不依赖 `main/book`；`book_store` 的根目录/上限通过 `read_pico_transfer_cfg_t{root_dir, file_limit, free_bytes_cb}` 在 `app_transfer.c` 里注入。
- 版面：header；大字 SSID / 口令 / 地址 `http://192.168.4.1`；用现有 `ui/datamatrix.c` 画地址码（可选）；第二行写当前目标“上传到 TF 卡 / 上传到内置存储（单文件 ≤ 1 MB，剩余 x MB）”；状态区（连接数、当前文件、字节/总字节、已完成 N）每 500 ms `on_tick` DU 局部刷新（`APP_REDRAW_AREA`）；完成一件后 GL16 一次状态区定稿；底栏“停止并返回图书”。
- 限制（写入文件头 Frozen）：本页不做锁屏/浅睡协同——传输期间按电源键会锁屏但热点继续；页面离开即断网。

### 3.3 构建配置 (M)

- `main/CMakeLists.txt`：`main_requires` 加 `read_pico_transfer`；SRCS 加 `apps/app_transfer.c`。
- `main/app/app_registry.c`：追加 `app_transfer`（第 16 项）。
- `sdkconfig.defaults` 与 `sdkconfig.ci` 同步追加：`CONFIG_ESP_WIFI_SOFTAP_SUPPORT=y`、`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y`（WiFi/LWIP 缓冲进 PSRAM）、`CONFIG_ESP_WIFI_IRAM_OPT=n`、`CONFIG_ESP_WIFI_RX_IRAM_OPT=n`（省 IRAM；若 IRAM 仍溢出再评估）、`CONFIG_HTTPD_MAX_REQ_HDR_LEN=1024`。注释按仓库风格中英双语说明原因。
- 三语 README + AGENTS.md 页表加行。

### 3.4 风险与真机门槛

- **发热 vs 120 MHz PSRAM**：连传 50 MB 文件并观察串口是否有 PSRAM/cache 错误或重启；若不稳，`esp_wifi_set_max_tx_power(34)`（8.5 dBm）降功率再测；仍不稳则本页标为实验并默认不进菜单（用 `#if` 开关）。
- flash 增量预计 +0.7–0.9 MB；确认总量 < 0x400000 且余量 ≥ 20%。
- 中文文件名经 FatFs CP437 可能乱码 → 上传时若含非 ASCII，服务端改名为 `book_<unix时间>.ext` 并在响应 JSON 回显原名（书架显示文件名即可）。

验证：手机连 `ReadPico-XXXX` → 浏览器打开 `http://192.168.4.1` → 上传 1 个 txt + 1 个 5 MB epub → 页面进度到 100% → 退出到图书页书架可见并可打开；断开热点后 `heap_caps_get_free_size` 回到进页前水平（串口日志打印）。

---

## Phase 4 · 交互层与准则落地（依赖 Phase 1；Phase 4.1–4.2 可与 Phase 2/3 并行）

### 4.0 `docs/INTERACTION.md`（Phase 0 建骨架，此处补全）

内容：emilkowalski 原则 → 墨水屏译本对照表（来源：`docs/HANDOFF.local.md` 2026-09-26 条目）：
- 频率决定装饰：高频动作（翻页、按键）零动画、零按下高亮，直接提交；低频动作（菜单项、设置按钮）按下 DU 反色/加粗边、抬起提交。
- 反馈时机：按下 ≤ 1 帧 DU（~100 ms）给出反馈；抬起时提交；抬起落在原控件外 = 取消并 DU 复原。
- 不对称时序：破坏性/不可逆（删除、跳章、退出阅读）用长按 ≥ 500 ms 或二次确认；普通动作即时。
- 定稿：连续 ≥ 6 次 DU 或 2 s 无操作后对受影响矩形做一次 GL16；整页仍按 `APP_GC16_EVERY` GC16。
- 不适用清单：easing、spring、blur、stagger、hover、过渡动画。
- 阈值常量表：`UI_LONG_PRESS_MS 500`、`UI_SWIPE_MIN_PX 120`、`UI_TOUCH_SLOP_PX 24`、`UI_SETTLE_IDLE_MS 2000`、`UI_SETTLE_DU_MAX 6`。
- 评审表模板：Before / After / Why（每个改动的交互都填一行）。

### 4.1 【Phase 4a】页内手势识别器（N `main/ui/ui_gesture.h/.c`；M `main/apps/app_book.c`）— 不碰 app_loop.c / app.h

- 新建纯函数式识别器 `ui_gesture_t`（状态：`active, x0, y0, t0_ms, moved, long_fired`）与 `ui_gesture_feed(&g, const app_ctx_t* ctx, ui_gesture_event_t* out)`：页面在 `on_touch`（按下沿）与 `on_tick`（每轮）里喂入 `ctx->pressed / ctx->released / ctx->touch / ctx->now_ms`，返回 0/1 个事件：`PRESS / LONG_PRESS / TAP / SWIPE_L/R/U/D / CANCEL`（含 `x0,y0,x,y,hold_ms`）。放在 `main/ui/` 是为了 4b 直接复用同一实现（`app_loop.c` 届时只是调用方）。
- 识别规则：位移 > `UI_TOUCH_SLOP_PX` → `moved`；`!moved && hold ≥ UI_LONG_PRESS_MS` → LONG_PRESS 一次；抬起：`long_fired` → CANCEL；`|dx| ≥ UI_SWIPE_MIN_PX && |dx| > |dy|` → SWIPE_L/R；纵向同理；否则 TAP（坐标用 x0,y0）。
- `app_book.c`：`on_touch` 只负责 `ui_gesture_feed` 并处理 PRESS（DU 高亮，返回 `APP_REDRAW_AREA`），其余事件在 `on_tick` 中处理；参考 `app_touch.c` 用 `ctx->consumed` 跳过被主循环吃掉的轮次（键区/把手）。
- 交互映射：READING：TAP 左/右区翻页（零装饰）；SWIPE_L/R 翻页；TAP 中区开关工具条；LONG_PRESS 版心 → 进 TOC；工具条按钮 PRESS → `ui_draw_pressed_round_rect` DU，TAP 提交，CANCEL/落外复原；工具条可见时 TAP 进度条 → 跳转。SHELF/TOC：行 PRESS 高亮，TAP 打开；SWIPE_U/D 翻叶；LONG_PRESS 书架条目 → “清除该书进度”确认（唯一破坏性操作；Phase 1 临时放在工具条里的同名按钮此时移除）。三键（KEY1/2/3）不经识别器，仍按下即触发。
- `on_tick` 定稿：记录 DU 次数与最后 DU 时刻，满足 4.0 阈值时对合并矩形 GL16 一次。
- 文件头 Frozen：翻页动作不做按下反馈；阈值引用 `ui_kit.h` 常量。
- **4a 验收门槛（真机）**：长按 500 ms 稳定进目录、误触率可接受；左右滑翻页不与 TAP 混淆；6 次 DU 后自动 GL16；连续快速翻页 20 次无漏事件。通过后才进入 4b；若阈值需调整，只改 `ui_kit.h` 常量并记录到 INTERACTION.md。

### 4.2 【Phase 4b】提升到主循环（M `main/app/app.h`、`main/app/app_loop.c`、`main/ui/ui_menu.h/.c`；M `main/apps/app_book.c`）— 依赖 4a 验收；**本轮唯一一次改主循环，三件事一起做**

- **(1) 手势回调** `app.h`：新增 `app_redraw_t (*on_gesture)(app_ctx_t*, const ui_gesture_event_t*)` 可选回调。语义：**有 `on_gesture` 的页面不再收到 `on_touch`**；没有的页面行为与现在完全一致。`///` 注释中英双语。更新 AGENTS.md「`app_desc_t` 契约」表与回调纯度段。
- `app_loop.c`：持有一份 `ui_gesture_t`，每轮 `ui_gesture_feed`；按下沿命中键区/把手/菜单时**重置识别器**（这些仍按下即触发）；否则若 `current->on_gesture` 存在则分发事件并 `app_present`，返回非 NONE 时置 `ctx.consumed`；无 `on_gesture` 的页面照旧走 `on_touch`。
- **(2) 三键接管**：`app_desc_t` 加 `bool owns_keys`（同 `holds_pmu` 风格）：为 true 且菜单未打开时，KEY1/KEY2/KEY3 全部交给 `on_key`；KEY2 GC16 与 KEY3 菜单不再全局处理。菜单打开时三键仍按主循环原逻辑（翻叶/关菜单）。`app_ctx_t` 加 `bool request_menu`：页面置 true，主循环在本轮末尾（`request_app` 同一处）打开根菜单并 `present_menu`。
- `app_loop.c` 文件头补“手势层属于页面无关逻辑”，并新增 Frozen：“主循环只产出手势、不解释含义；KEY2 整屏强刷与 KEY3 菜单是全局出口，只有 `owns_keys` 页面可接管三键，且必须自己提供强刷与菜单入口（`request_menu`）；把手仍按下沿即触发。” AGENTS.md 契约表同步加 `owns_keys` / `request_menu` 两行。
- **(3) 菜单改“按下高亮、抬起提交”**：按下命中行 → `ui_draw_menu_row_pressed()` + DU 局部；抬起仍在同行 → 切页；否则 DU 复原（需 `ui_menu_row_rect`）。
- `app_book.c` 迁移：删除自持的 `ui_gesture_t` 与 `on_touch`，改实现 `on_gesture`（事件处理函数原样复用，改动应只在入口）；desc 加 `.owns_keys=true`；`on_key` 映射：READING KEY1/2/3 = 上一页/工具条/下一页（与屏幕三区同义）；SHELF/TOC KEY1/2/3 = 上一叶/返回或菜单/下一叶；工具条补“强刷”（`present` 内 `update_display_full`）与“演示菜单”（`ctx->request_menu = true`）两键，保证全局出口不丢。
- 4b 验收：旧页面（触摸、加速度、自检等）逐页点一遍与改动前一致，特别是 `app_touch.c` 依赖 `consumed` 的跟手逻辑；菜单按下可见高亮、滑出行外抬起不切页；非 `owns_keys` 页面 KEY2/KEY3 不受影响；图书页三键与三区一致，工具条“强刷/演示菜单”可达；图书页手势行为与 4a 一致。

### 4.3 `ui_kit` 新原语 (M `main/ui/ui_kit.h/.c`；4a 即需，4b 补菜单部分)

- `ui_draw_pressed_round_rect(fb, rect, radius)`：内缩 4 px 粗边 + `UI_GRAY_LIGHT` 填充（不反色文字，避免重画文本）。
- `ui_rect_union(a, b)` 便于页面合并 DU 区。
- 阈值常量放 `ui_kit.h`（见 4.0）。
- （4b）`ui_menu.h/.c`：`ui_menu_row_rect(leaf, row, EpdRect*)`、`ui_draw_menu_row_pressed(fb, leaf, row, bool pressed)`。

### 4.4 评审表

在 `docs/INTERACTION.md` 末尾填 Before/After/Why：菜单项（按下切页 → 按下高亮/抬起切页 / 误触可取消）、翻页（—/零装饰 / 高频）、目录入口（无/长按 / 低频且可发现）、字号（DU 预览+GL16 定稿，保持）、书架进度清除（无/长按+确认 / 破坏性）。4a 完成时先填图书页各行，4b 完成时补菜单行。

---

## 最终改动预览（文件级）

**新建 (N)**
- `main/apps/app_book.c`、`main/apps/app_transfer.c`
- `main/book/book_store.h/.c`、`book_progress.h/.c`、`book_source.h/.c`、`book_txt.c`、`book_epub.c`、`book_layout.h/.c`、`html_text.h/.c`、`zip_reader.h/.c`、`gbk.h/.c`、`gbk_table.h`(生成)
- `main/ui/ui_gesture.h/.c`（4a 建，4b 被 app_loop 复用）
- `components/read_pico_transfer/{CMakeLists.txt, include/read_pico_transfer.h, read_pico_transfer.c, upload.html}`
- `tools/gen_gbk_table.py`、（可选）`tools/book_host_test.c`
- `docs/PLAN.book-reader.md`（本文）、`docs/INTERACTION.md`

**修改 (M)**
- `partitions_16M.csv`（factory 0x400000；spiffs → `storage` fat 0x500000，保留 5 MB）
- `sdkconfig.defaults`、`sdkconfig.ci`（WiFi/HTTPD 选项）
- `main/CMakeLists.txt`（SRCS、INCLUDE_DIRS、main_requires）
- `main/app/app_registry.c`（+2 页）
- `main/app/app.h`、`main/app/app_loop.c`（**仅 4b**，一次改完：可选 `on_gesture`、`owns_keys` + `request_menu` 三键接管、菜单抬起提交、新 Frozen 段）
- `main/ui/ui_kit.h/.c`（按下态原语、阈值常量；4a）、`main/ui/ui_menu.h/.c`（行矩形、按下态；**仅 4b**）
- `main/settings.h/.c`（2 个 NVS 键：`bk_px`、`bk_shake`）
- `components/read_pico/read_pico_sd.c`（max_files 8）
- `main/assets/builtin.ttf`（重生成）
- `AGENTS.md`、`README.md`、`README.zh-CN.md`、`README.ja-JP.md`（页表、契约表、INTERACTION 链接）
- `docs/HANDOFF.local.md`（每 Phase 一条，不入库）

**不动**：`components/epdiy/**`、`main/font/fallback.h`、`stb_truetype.h`、`waveforms/*.h`、`main/apps/app_reading.c`（保留为 demo）、任何 SY7636A 写操作、VCOM 路径、现有 UI 字串与日志。

**菜单结果**：… 13 IO 扩展、14 自检、**15 图书 Books**、**16 传书 Transfer**。

---

## 通用验证命令（每 Phase 收尾必做）

1. 编译（PowerShell）：`docker run --rm -v ${PWD}:/project -w /project espressif/idf:v6.1 idf.py build`；读取末尾分区余量行；`git checkout -- dependencies.lock`（若仅 EOL 变化）。
2. CI 配置：`… idf.py -B build_ci -DSDKCONFIG_DEFAULTS=sdkconfig.ci build`。
3. 烧写：`cd build; & C:\Users\abc75\miniforge3\python.exe -m esptool --chip esp32s3 --port COM5 --before default-reset --after hard-reset write-flash '@flash_args'`。
4. 串口：`--after hard-reset read-mac` 触发复位后 pyserial 读 10 s，确认 `Loaded app from partition at offset 0x10000` 与 `UI ready on 概览`，无 `PSRAM`/`cache` 错误。
5. 硬约束自检：AGENTS.md「硬约束」逐条 + ONBOARDING §9；注释中英双语；新 Frozen 段已写。
6. HANDOFF.local.md 顶部追加条目（模板：`docs/HANDOFF.md`），含烧写命令原文与日志片段。

---

## Decisions（用户不在线时按此执行，可推翻）

- **传书 = WiFi SoftAP + 网页上传**，作为独立可选 Phase 3；不做 BLE（S3 无经典蓝牙 OPP，BLE 需自研 App 且 10–30 KB/s）。
- **epub = 纯文本流**（用户确认）：spine + ncx/nav 目录 + 去标签；忽略 CSS/图片/脚注跳转。
- **txt = UTF-8 + GBK 自动检测**（内置 ≈48 KB 映射表）。
- **交互层 = 两步走**（用户 2026-09-26 确认）：4a 在 `main/ui/ui_gesture.c` 做页内识别器，仅 `app_book.c` 接入并真机验收；4b 再把同一识别器接进 `app_loop.c` 并加可选 `on_gesture`，菜单改抬起提交。旧页面零改动。AGENTS.md “加页不改 app_loop.c” 仍成立（4b 是加契约不是加页）。
- **三键接管并入 4b（用户：“放到 4b，先不动主循环，要动一起动”）**：Phase 1 阅读页只用屏幕左/中/右三区 + KEY1 = 下一页，KEY2/KEY3 保持全局强刷/菜单；`owns_keys` + `request_menu` + 三键 左/中/右 = 上一页/工具条/下一页 + 工具条“强刷/演示菜单”两键，全部在 4b 与 `on_gesture`、菜单抬起提交一次提交。主循环在整个计划里只改一次。
- **晃动翻页（用户确认接受兜底）**：实验性，默认关，工具条开关 + NVS `bk_shake`；复用 `sc7a20h_activity_config` AOI2 + 轮询 `read_events`，两次触发/600 ms 判定；只翻下一页；离页休眠传感器。若真机误触率压不下来，保留开关、INTERACTION.md 标注“实验，默认关”，不再追加复杂滤波。
- **多本进度**：`book_progress.c` 独立 NVS 命名空间 `rp_books`，按路径哈希存 blob（含文件大小校验），另记 `last`；阅读页页脚常驻进度条，工具条打开时可点跳。容量约 100–150 本，超出只 WARN。选 NVS 而非 sidecar 文件：无卡模式同样可用、换卡不丢、不往用户卡上写隐藏文件。
- **无卡存储（用户确认保留 5 MB）**：原 5 MB 空闲 spiffs 改为 5 MB `storage`(fat, wear-levelling) 分区（固件无 OTA，无需预留），惰性挂 `/flash`，单文件 ≤ 1 MB，仅供测试/演示；有卡时 SD 优先，两根同时列出。传书页按当前根拒收超限文件。
- **TF 卡缓存：本轮不做。** 理由：正文路径按章加载进 8 MB PSRAM 已足够（单章上限 2 MB），无重复重计算；无卡模式下也没有卡可缓存；图片本轮不做。若日后加图片，再做“解码后 4bpp 位图缓存”到 `/sdcard/.cache/<hash>/`，键含文件大小+mtime。可选小优化：章节分页索引缓存，仅当真机实测首次排版 > 1 s 才做。
- **翻页在抬起提交**（用户确认），保留左右滑动翻页。
- **Phase 3 纳入本轮**（用户确认），排最后，50 MB 连传门槛。
- **允许新增中文 UI 字串并重生成 builtin.ttf**；三语 README 页表同步。
- **不重构 `app_reading.c`**，布局代码复制到 `main/book/book_layout.c` 泛化，避免回归 demo。
- **分区扩容在 Phase 0 一次做完**，即使最终放弃 Phase 3 也保留（epub+GBK 表本身就逼近 2 MB）。
- 不涉及 MuxOS/CrossMux。

## Further Considerations

1. **中文文件名**：FatFs CP437 + ANSI_OEM 编码下可能不可打开。若真机确认有问题，备选是 `CONFIG_FATFS_API_ENCODING_UTF_8=y` + `CONFIG_FATFS_CODEPAGE_936`（+~100 KB flash），需评估。Phase 0.3 放中文名测试书即可暴露。
2. **WiFi 发热 vs 120 MHz PSRAM**：Phase 3 需在 50 MB 连传后观察是否出现 PSRAM/cache 报错；若出现，Phase 3 内加“传书页在位时 CPU 降频到 160 MHz”或标注该功能只在 `sdkconfig.ci` 时序下可靠。

已决（不再是问题）：三键接管 → 4b；内置存储分区 → 5 MB；晃动翻页误触 → 保留开关并标“实验，默认关”。
