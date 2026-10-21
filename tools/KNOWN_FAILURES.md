# 宿主测试已知失败登记（Known failures）

只有「证据链完整、且已确认非我方回归」的失败才能进这张表。每条必须写明：现象、根因、
A/B 取证、产品侧影响核验、重建触发器。runner 里对登记项**只放行登记的失败形态**，形态一变
即视为新缺陷并让 runner 失败——隔离不等于放弃守门。

---

## 1. ui_kit 主机测试（tools/ui_kit_host_test.c）：r93 4bpp packed 帧缓冲契约失效

- **登记形态**：断言 `fb[10*W+30]<128&&fb[11*W+30]<128&&fb[12*W+30]==255`（`main()` 里
  `ui_draw_control_frame` 之后的第一个像素采样）失败。runner 用 `fb[10*W+30]` 这个关键字
  识别登记形态，出现其它失败形态一律不放行。
- **现象**：调用 `ui_draw_control_frame` 后采样点全为 255（白），看起来像「边框没画」。
- **根因（上游 r93 契约变化）**：r93 把实心填充换成 `ui_fill_rect_fast`，它是**4bpp packed
  写入**——`stride = epd_width()/2`（一字节存相邻两像素），而测试环境是「1 字节 1 像素、
  `stride=W`」的老模型，两者不再对齐，采样点因此落空。
- **A/B 取证**：
  - r92 锚 `279e74e`（补上 r93 同款 antialias 桩后）**断言 203 通过**，一路跑到
    `test_native_titles`（行 96）才挂——即 r92 上边框绘制断言是绿的；
  - r93 树上同一断言直接挂 → 差异由 r93 引入。
  - r92 锚上该测试另有**存量欠债**：未加 `--gc-sections` 时缺 14 个符号
    （`epd_draw_rotated_image`、`ui_fb_insert`、`dm_encode` 等），**从未跑绿过**。
- **产品侧无虞（已核验）**：`ui_fill_rect_fast` 对奇/偶起始列分别处理高低 nibble
  （`x&1` 只改高 nibble、尾部单列只改低 nibble），真机 4bpp 帧缓冲下不会污染邻像素；
  真机路径不存在「画不出边框」的问题。
- **为何不修**：修它要按 packed 模型重建整个 harness，并把 `==UI_GRAY_LIGHT(0x90)`
  一类相等断言量化成 `0x99`——改的是判据而非桩；且该测试从来没绿过，#
  20 的双栏改动在 `book_layout` 域（有 page_map / 布局测试守着），面板有 settings_panel
  黄金值与 reading_settings 套件，此刻重建成本高于价值。
- **重建触发器（定档）**：当 UI 几何回归网成为实际需求（未来面板 / UI 类工作）时，
  按 packed 模型**从零重建**这个 harness——不是打补丁，因为模型本身要换。
- **维持可见**：测试仍在 `run_book_host_tests.sh` 里编译并运行，失败只记录不拦截。

---

## 2. title_font 主机测试（tools/title_font_host_test.py）：~~r92 AA 化后 BW 契约失效~~ —— **已重建（2026-10-16 关闭）**

> **状态：已关闭。** 触发器（第 3 步章首题头双栏化触及 `ui_text_title_vc` 调用点几何入参
> `center_x`/`fit_w`，`app_book.c` 题头块）已于 #95 击发；判据按登记预案重造为三层 AA
> oracle，测试转绿，runner 隔离分支退役（`run_book_management_tests.sh` 第 46 行改直跑，
> 任何失败都是新缺陷）。以下保留完整证据链备查。

- **原登记形态**：运行期 SIGABRT，`epd_draw_pixel: Assertion \`c==0' failed`（桩的 BW 断言）。
  runner 原用 `epd_draw_pixel: Assertion` 关键字识别放行。
- **原失败机制**：测试 `epd_draw_pixel` 桩强制 BW 契约（`assert(c==0)`），并与
  `ttf_draw_text_px_bw` 逐位恒等对比（原测试 106-109 行）；而产品自 **r92 快照
  `1260587`（v0.3.3-rc92）** 起把 `ui_text_title_vc`（`main/ui/ui_kit.c:363`）从
  `ttf_draw_text_px_bw` 切换为 `ttf_set_antialias(true)+ttf_draw_text_px`——与新增
  `ui_text_fixed_context_aa_vc` 一体的 AA 化，r92/r93 逐字相同。测试引入于 `ba8b5ae`
  （BW 时代），自 r92 起红（先被 format-truncation 编译错掩盖，`bb77ff7`/`08f4481` 修好
  编译层后红形态转为运行期 SIGABRT）。
- **产品意图证据**：`1260587` 的 changelog 明言 "Add five stroke weights and optional
  antialiasing, paired reading controls"，并新增 `app_settings_book_antialias()` 开关
  （默认开）。BW 契约是测试侧旧约，不是产品回退。
- **parity 状态（关闭前）**：契约层与上游同源——上游 r93 的同一测试带着同样的 BW oracle，
  在自家 r93 上同样红；「与上游同红」的既有政策在重建前一直适用。
- **重建内容（2026-10-16）**：原 106-109 行的「桩强制 c==0 + 与 `ttf_draw_text_px_bw`
  逐位恒等」重造为三层 oracle（测试内 105 行起，32/40/48px 三个字号逐号执行）：
  1. **灰度域**：4bpp 量化后墨（`UI_INK_BLACK`）/纸（`UI_INK_WHITE`）/中间灰（1..14）
     三层俱全——证明标题入口走的是 AA 灰度路径而非二值；
  2. **AA 参考对照**：`ui_text_title_vc` 输出与同基线（`center_y+(above-below)/2`，
     与产品路径同出）、同字号、同对齐的 `ttf_draw_text_px` AA 光栅化逐位一致；
  3. **结构**：`painted>0`、热身 15 次缓存/句柄复用不变（原 111-112 行断言原样保留）。
     热身循环与主断言同在 `record_coverage=true` 区内收口（AA 灰度同样过桩）。
- **变异验证（三层各有牙）**：① 产品路径关 AA（`ui_kit.c:373` true→false）→ memcmp 擒；
  ② 产品路径基线换算错位（`center_y+(above-below)/2`→`center_y`）→ memcmp 擒；
  ③ 产品路径与参考路径**同时**关 AA → memcmp 绿但 `ink_px&&paper_px&&gray_px` 擒——
  「实现与参考两边同错」场景由灰度域兜底。三变异后 `main/ui/ui_kit.c` 零 diff
  （逐字节校验），测试复绿。
- **变异复证（2026-10-16 收口复审轮，用户审计要求「转绿须证明判据有牙」补测）**：
  按序重跑 ①③ 两个 BW 变异并记录精确断言行——① 单侧退化（仅产品路径
  `ui_kit.c:373` true→false）：`test.c:75: Assertion '!memcmp(pixels,expected,
  sizeof(pixels))' failed`（exit 1，AA 参考对照层擒）；③ 双侧同时退化（产品路径 +
  测试参考路径同关 AA）：`test.c:82: Assertion 'ink_px&&paper_px&&gray_px' failed`
  （exit 1，灰度域层独立擒——此时 memcmp 两边同错会绿，唯灰度域有牙）。两变异后
  `git checkout` 还原产品码 + 还原测试，`git status` 零改动，测试复跑 PASS。
  「转绿」≠「判据放宽」：本 oracle 红过，且红在判据层而非环境层。
- **为何不是补桩**：旧判据编码的是 BW 时代契约（二值 + 与 BW 光栅化恒等），产品契约
  已换成 AA 灰度；补桩（放宽 `assert(c==0)`）只会把判据退化为「什么都不查」。
- **证据指针**：提交 `1260587`（产品 AA 化）、`ba8b5ae`（测试引入）、`bb77ff7`/`08f4481`
  （编译层适配）、`76817f7`（oracle 重造 + 隔离退役）；第 3 步改形方案 §5「title_font
  触发器」；变异复证运行日志 `/tmp/mut_run.log`（①）/`/tmp/mut2_run.log`（③）（会话内）。

---

## 3. test_home_recent（tools/test_home_recent.py）：上游自带过期测试（观察项）

- **登记形态**：编译失败——harness 序幕缺 r93 `on_enter` 新引用的符号，首个报错
  `error: 's_cover_generation' undeclared`（gcc 用的是弯引号；随后
  `book_cover_jobs_begin`、`book_progress_revision`、`clock_base_prepare`、
  `s_widget_trail`、`s_view` 等连带）。runner 用 `s_cover_generation` 这个关键字
  识别登记形态。
- **失败机制（两层，第②层现被第①层掩盖）**：
  - ① 编译层：`main/apps/app_dashboard.c` 的 `on_enter` 在 r93 引入了封面/时钟挂件/
    进度修订号等新依赖，测试序幕还是 r92 的符号集；
  - ② 运行层：r93 把进度刷新门控为 `if(s_cached_progress_revision!=progress_revision)
    refresh_progress()`（`book_progress_revision()` 为原子计数器，仅
    `book_progress_notify_changed` 推进），测试场景（调用 2→3 间无进度变化）下产品恒
    不刷新 → `refreshes==0`，而断言 `refreshes==1` 编码 r92 的无条件刷新。
- **产品意图证据**：r93 上游 `on_enter` 的 revision 门控是刻意设计（避免重复刷新）；
  我方 `93fce0a` 已按同一语义适配 book_ui。首页不在 #20 任何施工面上。
- **parity 状态**：**与上游 r93 逐字节相同**（2026-10-16 diff 核验）——上游自己带着
  过期测试在跑。「与上游逐字节相同的测试文件一律不碰，保持与原厂同红」的既有政策
  直接适用：改它 = 主动偏离上游换取零产品价值。
- **重建触发器**：**上游自修时自动翻绿**——观察项，与 ui_refresh_feedback 同类处理。
  若上游只修第①层不修第②层，失败形态变为运行期断言（`refreshes==1`），届时按
  「形态一变即新缺陷」重新 triage 并更新本条目。
- **证据指针**：`r93-rebase-全量验证-20261015.md` §6.3；`NOTES.md` G 段；
  `/tmp/fullE.log`、`/tmp/fullF.log` 全失败清单。
- **维持可见**：测试仍在 `run_book_management_tests.sh`（第 47 行）编译并运行，
  失败只记录不拦截。

---

## 4. 散装 python 宿主测试：编译 / parity 失败（覆盖盲区，2026-10-16 step-4 收口审计）

> **覆盖盲区（重点，必读）**：以下 5 个失败测试**没有任何 `run_*.sh` 引用**
> （`grep -rln '<test>' tools/run_*.sh` 零命中）——它们是 runner 管辖外的**散装 python
> 测试**。commit `abac67b`（known-fail 正式化 + runner 隔离收口 12/12）确立的「12/12 绿」
> **从未覆盖它们**，因此给了虚假安全感：step-3 收口时这 5 个就已不在 runner 视野内，
> 当时 12/12 绿未暴露它们。
> **教训**：今后任何「全绿」口径必须声明覆盖边界（哪些在 runner 内、哪些散装）。
> 本批全部按五要素登记，但**不入 runner**——维持散装现状，不拦截也不伪装绿。
> 回退对照（app_book.c/settings.c/settings.h 还原到 HEAD `4cb7714`，含 step-3、不含
> step-4）后 5 个仍全部失败，故均**非 step-4 回归**；A/B 取证统一据此。

### 4.1 acrylic_dither_host_test.py：测试前奏未声明产品符号（include 漂移）

- **登记形态 / 现象**：编译失败——`dots.c:153: error: 'UI_LOCK_WIDTH' undeclared`；
  随后 `display_main_transition_gray_bands` / `ui_fill_rect_fast` 隐式声明
  （`-Werror=implicit-function-declaration`）。
- **根因**：测试前奏（PRELUDE）未声明这些产品符号。`ui_fill_rect_fast` 是真实存在的
  产品函数（`app_book.c:2818` 使用），仅测试 include 集未暴露其声明——**测试桩/include
  漂移，非产品逻辑问题**。
- **A/B 取证**：回退产品文件到 HEAD（不含 step-4）后本测试仍编译失败 → 非 step-4 回归；
  且本测试不在任何 runner 内，属历史散装失败。
- **产品侧影响核验**：无。纯宿主编译测试，不产设备产物；失败不影响真机固件，也不触及
  #20 任何施工面。
- **重建触发器 / 处置**：**登记，不修**。修复 = 给 PRELUDE 补 `UI_LOCK_WIDTH` 声明并
  include 暴露 `display_main_transition_gray_bands`/`ui_fill_rect_fast` 的头；属测试基建，
  不在 #20 范围。若上游 r93 同款测试逐字节相同，则按「与上游同红」政策不碰，等上游自修翻绿。

### 4.2 dashboard_clock_host_test.py：编译器收紧（`-Werror=sign-compare`）

- **登记形态 / 现象**：编译失败——`clock.c:109: error: operand of '?:' changes
  signedness from 'int' to 'app_home_component_t' due to unsignedness of other operand`
  （`-Werror=sign-compare`）。
- **根因**：**编译器严格度**——`?:` 两侧类型符号不一致（int vs 枚举），新版 gcc 将该警告
  升为错误。代码风格 / 工具链问题，非逻辑回归。
- **A/B 取证**：散装、不在 runner；未触碰 home/clock 域，与 step-4 无关。
- **产品侧影响核验**：无（宿主测试）。
- **重建触发器 / 处置**：**登记，不修**。修复 = 该测试编译命令去掉 `-Werror=sign-compare`
  或在 `?:` 处显式转型；测试基建，非 #20。

### 4.3 reader_refresh_release_host_test.py：rc92 冻结锚 parity 分歧（parity 桶）

- **登记形态 / 现象**：运行期断言 `assert current==baseline,name` →
  `AssertionError: turn_page`（rc92 冻结锚 parity 测试）。
- **根因**：**冻结锚 parity 分歧**——本测试即 rc92 锚的冻结对照，自 step-2 起即在存量失败
  桶；`current` 与 `baseline` 在 `turn_page` 上不一致属锚态漂移，非新缺陷。
- **A/B 取证**：设计即为此类冻结锚 parity；step-3 收口时已红，且不在 runner 视野。
- **产品侧影响核验**：无（parity 对照，不产设备产物）。
- **重建触发器 / 处置**：**登记（parity 桶）**，与上游同红政策一致——保持原厂同红，等上游
  / 锚重建翻绿；若形态变化（出现除 `turn_page` 外的分歧）按「形态一变即新缺陷」重新 triage。

### 4.4 settings_entry_clock_host_test.py：测试 include/桩集未暴露产品声明（include 漂移）

- **登记形态 / 现象**：编译失败——`test.c:168: error: implicit declaration of function
  'reader_prefetch_resume'`；`draw_reader_outer` 隐式声明（`did you mean 'draw_reader'?`）；
  `ui_fill_rect_fast` 隐式声明。
- **根因**：测试 include/桩集未暴露产品声明。`draw_reader_outer`（`app_book.c:3321`）与
  `reader_prefetch_resume` 均为真实产品函数，仅测试侧抽取/声明集未带其头。注：
  `draw_reader_outer` 在 step-4 被改写，但本失败在回退到 HEAD（不含 step-4）后依旧 →
  **非 step-4 引入**。
- **A/B 取证**：回退产品文件到 HEAD 后仍编译失败 → 非 step-4 回归；散装、不在 runner。
- **产品侧影响核验**：无（宿主测试）。
- **重建触发器 / 处置**：**登记，不修**。修复 = 在测试 include 集补对应产品头声明；测试基建，
  非 #20；若与上游逐字节相同则按同红政策不碰。

### 4.5 shelf_cover_batch_host_test.py：编译器收紧（`-Werror=misleading-indentation`）

- **登记形态 / 现象**：编译失败——`test.c:40: error: this 'if' clause does not guard...
  [-Werror=misleading-indentation]`（`if(...)return -1;if(...)return 0;` 单行串联两 if）。
- **根因**：**编译器严格度**——新版 gcc 把单行串联 `if` 的 misleading-indentation 升为
  错误。纯代码风格 / 工具链问题。
- **A/B 取证**：散装、不在 runner；与 step-4 无关。
- **产品侧影响核验**：无。
- **重建触发器 / 处置**：**登记，不修**。修复 = 两个 if 拆到独立行（或该测试编译去
  `-Werror=misleading-indentation`）；测试基建，非 #20。

> **本批小结**：5 个全部 off-runner、off #20、无产品侧影响。其中 1 个 parity 桶
> （4.3），4 个编译失败（2 个 include 漂移 4.1/4.4 + 2 个编译器严格度 4.2/4.5）。
> 统一处置 = **登记不修**（同红政策优先；测试基建修复留待后续 test-infra 工作，不阻塞 #20）。
> 覆盖盲区教训已记于本节点开头。

---

## 5. twin_image 主机测试（tools/book_layout_twin_image_host_test.c）：~~G7-A1 双栏 × 大插图放置几何越栏~~ —— **已退役（2026-10-17，G7-A1 修复落地）**

> **状态：已退役。** 触发器已击发——产品码修复 `61909a1`（出血宽发布收归建版单出口
> `reader_relayout_cols`，按栏数快照发布：单栏整屏宽不变、双栏发栏宽）落地后，本 fixture
> 镜像新接线（bleed=栏宽）复跑转绿：**45 checks / 0 red / exit 0**（大插图记录 476x317、
> 绘制矩形 [0,476] 恰为本栏；小插图 200x150 / [138,338] 不变；无图基线不变）。runner
> 隔离分支已退役（`run_book_host_tests.sh` 改直跑，任何非零都是新缺陷）。以下保留完整
> 证据链备查。

- **登记形态**：退出码 1，且输出含 `REPRODUCED（大插图场景几何层复现 G7-A1`。runner 按
  「退出码 1 + 该行」识别放行；退出码 2（对照/基线见红、或大图红点不在几何层）= 新缺陷，
  照旧拦。退出码 0（三场景全绿）= 修复落地后的预期形态，直接通过、不属登记范围。
  （退役后口径：fixture 退出码 1 的报文已改为 `FIX_INCOMPLETE`——修复不完整或布局层
  回归；runner 直跑，任何非零照失败拦。）
- **现象**：大插图场景（900x600 横构图，宽于 476px 栏宽）几何层 4 红——插图记录宽
  900 > 栏宽（A1）；按设备侧绘制契约（`draw_reader_images` 的 band 居中公式
  `left = band.x + (band.width - image->width) / 2`）复算的绘制矩形 [-212,688] 同时越出
  本栏 [0,476] 左界、与对侧栏 [524,1000] 相交（遮挡 164px）（A2）。小插图（200x150）
  绘制矩形 [138,338] 完全在栏内；无图基线纯文本行为不变——三场景区分度兑现
  （小图全绿、大图见红）。band 本身 [-262,1000] 宽于栏宽属 bleed 结构性事实，只作机制
  登记信息行，不作判据。
- **根因**：即 #20 e2e 返工范围第 1 条待修缺陷本身（G7-A1）：出血宽被设为整屏宽
  （`book_layout_set_image_bleed_width(UI_LOCK_WIDTH)`，单栏时代「未知尺寸的整页图同样
  通栏」意图的遗留），竖排分栏下插图区域随之扩至整屏，大图居中后侵入对侧栏。判据落在
  记录宽与实际绘制矩形两层；PR9「双栏下整页图与栏宽关系未定」在案。
- **A5 对表（用户可执行注记①）**：宿主侧绘制流逐字完整（与已知文本 strcmp 一致、签名
  探针 picture / the eye 均在），故宿主侧形态 = 「被盖住」类（字形已绘制、被插图盖住）；
  照片 3 的缺字签名（`icture` ← `picture`、`e eye` ← `the eye`）未在宿主侧复现——按
  注记① 不吻合，**现象面还需再采**（设备侧复测走 gen8 流程）。
- **A/B 取证**：fixture 自身三场景即对照——同一 build、同一已知文本，仅插图尺寸不同：
  大图几何 4 红 / 小图 0 红 / 无图 0 红，差异由插图宽度引入，非测试基建问题。
- **产品侧影响核验**：即返工对象本身。修复方案经用户批准（A 方案：`reader_relayout_cols`
  按栏数快照发布出血宽）后由产品码独立提交 `61909a1` 落地。
- **为何不修**：~~修复方案待批（见上）；fixture 维持编译并运行以保持可见。~~（退役：
  已修，`61909a1`。）
- **重建触发器（退役条件）**：~~G7-A1 修复落地（gen8 换锚）后，本 fixture 应转退出码 0
  （三场景全绿）——届时本登记退役，runner 分支改直跑，任何非零都是新缺陷。若修复后仍为
  退出码 1 = 修复不完整，照新缺陷拦。~~（**已击发**：fixture 已镜像新接线转 exit 0，
  登记退役、runner 直跑；gen8 换锚见收口报告。）

---

## 6. 登记流程

1. 先做 A/B（对照 r92 锚或等价基线），确认非我方回归；
2. 查清产品侧影响（真机是否有损）；
3. 在本文件登记上述五项；
4. runner 里放行**具体失败形态**（关键字匹配），其余形态照旧失败。
