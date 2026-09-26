# 小纸 Pico 交互准则 / Read Pico interaction guidelines

本文是 [图书阅读器计划](PLAN.book-reader.md) Phase 0.6 的交互基线，供 Phase 1 与 Phase 4 引用；描述目标行为，不代表已经实现或通过真机验收。
This document establishes the Phase 0.6 interaction baseline for Phases 1 and 4 of the [book-reader plan](PLAN.book-reader.md). It describes intended behavior, not completed implementation or hardware acceptance.

依据为计划 §4.0–4.4 与 Decisions；原则对照源自本机交接记录 `docs/HANDOFF.local.md` 的 2026-09-26 emilkowalski/skills 讨论。该记录不入库，本文保留可独立使用的产品准则，不把 Web 动画参数直接搬到墨水屏。
The source of truth is the plan's sections 4.0–4.4 and Decisions. The principle mapping comes from the local 2026-09-26 handoff discussion of emilkowalski/skills. That log is not tracked; this document preserves the product rules without directly importing Web animation parameters.

## 冻结决策 / Frozen decisions

- 屏幕翻页在抬起时提交，保留左右滑动翻页；翻页不画按下反馈，不加过渡动画。三键仍按下即触发。
  Screen page turns commit on release, including horizontal swipes. Page turns have no pressed decoration or transition animation. The three keys still trigger on press.
- Phase 4a 仅图书页接入页内手势识别器，不改 `app_loop.c` / `app.h`；**4a 真机验收通过前，不进入 4b**。
  Phase 4a integrates the recognizer only inside the book page, leaving `app_loop.c` and `app.h` unchanged. **Do not enter 4b before 4a passes hardware acceptance.**
- Phase 4b 一次合入可选 `on_gesture`、菜单抬起提交、`owns_keys` 三键接管与 `request_menu`。接管三键的页面必须提供强刷与菜单入口；旧页面保持原行为。
  Phase 4b introduces optional `on_gesture`, menu release-to-commit, `owns_keys`, and `request_menu` together. Pages owning the keys must provide full-refresh and menu actions; existing pages retain their behavior.
- 晃动翻页是**实验，默认关**，只翻下一页；工具条开关存入 `bk_shake`。若真机误触仍不可接受，保留开关及实验标记，不追加复杂滤波。
  Shake-to-turn is **experimental and off by default**, advances only to the next page, and persists its toolbar toggle in `bk_shake`. If hardware false triggers remain unacceptable, keep the toggle and experimental label instead of adding complex filtering.

## 原则与墨水屏行为 / Principles and e-paper behavior

| 原则 / Principle | 墨水屏行为 / E-paper behavior |
| --- | --- |
| 频率决定装饰 / Frequency determines decoration | 高频翻页、按键零动画、零按下高亮；屏幕翻页抬起提交，三键按下提交。低频控件按下 DU 高亮、抬起提交。/ Frequent page turns and key actions have no animation or pressed highlight. Screen turns commit on release; keys commit on press. Infrequent controls highlight with DU on press and commit on release. |
| 及时反馈 / Timely feedback | 低频控件在按下后 ≤ 1 帧 DU 给反馈，目标约 100 ms；实际延迟由真机记录。抬起落在原控件外则取消并 DU 复原。/ Infrequent controls respond within one DU frame, targeting about 100 ms; record actual latency on hardware. Releasing outside the original control cancels and restores it with DU. |
| 不对称时序 / Asymmetric timing | 高风险动作优先长按 ≥ 500 ms 或二次确认；清除进度必须走书架条目长按后的确认。跳章、退出阅读按计划的目录、可见工具条等明确入口执行，不给普通翻页增加等待。/ Prefer a hold of at least 500 ms or confirmation for risky actions. Clearing progress requires confirmation after a shelf-row long press. Chapter jumps and leaving reading use the plan's explicit TOC or visible toolbar actions; ordinary page turns do not acquire a delay. |
| 跟手与定稿 / Tracking and settling | 累计 ≥ 6 次 DU，或无操作 2 s，对受影响的合并矩形做一次 GL16；无待定稿区域时不刷新。整页仍按 `APP_GC16_EVERY` 做 GC16。字号预览保留计划中的 DU + 400 ms 后 GL16。/ Settle the union of affected rectangles with GL16 after at least six DU updates or two idle seconds; do nothing if no region is pending. Keep page GC16 cadence under `APP_GC16_EVERY`. Font-size preview retains its planned DU plus GL16 after 400 ms. |
| 限制视觉噪声 / Limit visual noise | 不使用 easing、spring、blur、stagger、hover 或过渡动画；按下态用内缩 4 px 粗边与 `UI_GRAY_LIGHT` 填充，不反色文字。/ Do not use easing, spring, blur, stagger, hover, or transition animation. Pressed controls use a 4 px inset thick border and `UI_GRAY_LIGHT` fill without inverting text. |

## 阈值基线 / Threshold baseline

以下是计划初值，尚不代表真机调优结果。手势与通用定稿阈值集中于 `main/ui/ui_kit.h`；真机调整后同时更新本表并在交接记录保留原因与验证证据。
These are planned initial values, not hardware-tuned results. Gesture and general settling thresholds belong in `main/ui/ui_kit.h`. After hardware tuning, update this table and retain the reason and evidence in the handoff log.

| 常量 / Constant | 初值 / Initial value | 用途 / Meaning |
| --- | --- | --- |
| `UI_LONG_PRESS_MS` | 500 ms | 未超容差时触发一次长按。/ Emit one long press while movement remains within slop. |
| `UI_SWIPE_MIN_PX` | 120 px | 主轴位移至少达到此值且大于另一轴才为滑动。/ Swipe requires at least this displacement on the dominant axis, exceeding the other axis. |
| `UI_TOUCH_SLOP_PX` | 24 px | 位移超过此值后不再触发长按。/ Movement beyond this value disqualifies a long press. |
| `UI_SETTLE_IDLE_MS` | 2000 ms | 无操作后对待定稿区域 GL16。/ Settle pending regions with GL16 after inactivity. |
| `UI_SETTLE_DU_MAX` | 6 次 / updates | 达到此 DU 次数即定稿。/ Settle when this DU count is reached. |
| `BOOK_SHAKE_THS_MG` | 500 mg | 实验晃动的 AOI2 初始阈值。/ Initial AOI2 threshold for experimental shake detection. |
| `BOOK_SHAKE_DUR` | 2 | AOI2 时长参数，非毫秒值。/ AOI2 duration parameter, not milliseconds. |

晃动识别每 40 ms 轮询；600 ms 内至少两次 AOI2 活动上升沿算一次晃动，触发后冷却 1500 ms。手指按住或翻页后 800 ms 内忽略晃动。关闭功能或离页时停用 AOI2 并休眠传感器，不影响睡眠页拿起唤醒。
Poll shake events every 40 ms. At least two AOI2 activity rising edges within 600 ms count as one shake, followed by a 1500 ms cooldown. Ignore shaking while a finger is down or within 800 ms after a page turn. Disable AOI2 and sleep the sensor when switching the feature off or leaving the page, preserving the sleep page's lift-to-wake behavior.

## 图书页映射与阶段边界 / Book-page mapping and phase boundaries

阅读版心左 30% / 中 40% / 右 30% 对应上一页 / 工具条 / 下一页。Phase 4a 增加左右滑动翻页、版心长按进目录；书架与目录上下滑动翻叶，行按下高亮、抬起打开，落外取消。工具条显示时进度条才可点跳；进度清除由 Phase 1 工具条临时入口迁到 4a 书架长按确认入口。
The reading area's left 30%, middle 40%, and right 30% select previous page, toolbar, and next page. Phase 4a adds horizontal swipe page turns and a content long press to open the TOC. Shelf and TOC lists use vertical swipes for pagination; rows highlight on press, open on release, and cancel on outside release. The progress bar accepts jumps only while the toolbar is visible. Progress clearing moves from the temporary Phase 1 toolbar action to a shelf long-press confirmation in 4a.

Phase 1 与 4a 中，图书页只收 KEY1（下一页或下一叶），KEY2 / KEY3 保留全局强刷 / 菜单。4b 才把阅读三键改为上一页 / 工具条 / 下一页，把书架与目录三键改为上一叶 / 返回或菜单 / 下一叶；工具条同时补齐强刷与演示菜单入口。
In Phases 1 and 4a the book page receives only KEY1 for the next page or list page; KEY2 and KEY3 retain global full-refresh and menu behavior. Only 4b maps the reading keys to previous page, toolbar, and next page, and the shelf/TOC keys to previous list page, back or menu, and next list page. The toolbar gains full-refresh and demo-menu actions at the same time.

## 真机验收门槛 / Hardware acceptance gates

验证流程遵循 [ONBOARDING.md](ONBOARDING.md) 与计划的通用验证命令。编译、烧写校验不能代替实际启动日志和屏幕交互检查；目视结果由人工回填。本文不记录当前执行状态，实际命令、日志、测试版本和未通过项写入 `docs/HANDOFF.local.md`。
Follow [ONBOARDING.md](ONBOARDING.md) and the plan's common validation commands. A build or flash verification does not replace startup logs and on-screen interaction checks; a human records visual results. Keep current execution status, commands, logs, tested revision, and failures in `docs/HANDOFF.local.md`, not in this policy document.

| 门槛 / Gate | 必须验证 / Required verification |
| --- | --- |
| 4a → 4b | 500 ms 长按稳定进目录且误触可接受；左右滑动不混成 TAP；6 次 DU 后自动 GL16；连续快速翻页 20 次无漏事件。以上必须真机通过后才进入 4b。/ A 500 ms hold reliably opens the TOC with acceptable false triggers; horizontal swipes are distinct from taps; six DU updates trigger GL16; 20 rapid turns lose no events. All must pass on hardware before starting 4b. |
| 4b | 旧页面逐页回归，特别检查触摸页 `consumed` 跟手；菜单按下可见高亮、滑出行外抬起不切页；非 `owns_keys` 页面 KEY2 / KEY3 不变；图书页三键与三区一致，强刷与菜单可达，手势与 4a 一致。/ Regress every existing page, especially touch tracking using `consumed`; verify visible menu press feedback and outside-release cancellation, unchanged KEY2/KEY3 on other pages, matching book keys and screen zones, reachable refresh/menu actions, and unchanged 4a gestures. |
| 晃动 / Shake | 默认关闭；开启后只向后翻页，冷却与触摸屏蔽有效；关闭或离页后睡眠页拿起唤醒正常。误触不可接受时保留实验标记与默认关。/ Off by default; when enabled, advance only forward and honor cooldown and touch suppression; verify lift-to-wake after disabling or leaving. Retain the experimental label and default-off policy if false triggers remain unacceptable. |

## Before / After / Why 评审模板 / Review template

每项交互改动填写一行。4a 验收时填写图书页各行，4b 验收时补菜单行；以下为待填写模板，不能作为实现完成或测试通过的证据。
Fill one row per changed interaction. Complete book-page rows at 4a acceptance and the menu row at 4b acceptance. These are unfilled templates, not evidence of implementation or passing tests.

| 交互 / Interaction | Before / 改前 | After / 改后 | Why / 原因 | 验证证据 / Evidence |
| --- | --- | --- | --- | --- |
| 翻页 / Page turn | 待填 / Pending | 待填 / Pending | 高频，抬起提交且零装饰。/ Frequent action; release commits without decoration. | 待真机 / Hardware pending |
| 目录入口 / TOC entry | 待填 / Pending | 待填 / Pending | 低频，版心长按及工具条入口。/ Infrequent; content hold and toolbar entry. | 待真机 / Hardware pending |
| 字号 / Font size | 待填 / Pending | 待填 / Pending | DU 预览与 GL16 定稿。/ DU preview and GL16 settling. | 待真机 / Hardware pending |
| 书架进度清除 / Clear book progress | 待填 / Pending | 待填 / Pending | 破坏性，长按后确认。/ Destructive; confirmation after a hold. | 待真机 / Hardware pending |
| 菜单项（4b）/ Menu row (4b) | 待填 / Pending | 待填 / Pending | 抬起提交，误触可取消。/ Commit on release so accidental presses can cancel. | 待 4a 门槛通过 / Awaiting 4a gate |
| 其他改动 / Other change | 待填 / Pending | 待填 / Pending | 待填 / Pending | 待填 / Pending |
