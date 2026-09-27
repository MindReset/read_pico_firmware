# HANDOFF · 任务交接协议 / Task handoff protocol

本文件**只定义协议和模板**，入库、可合并到上游。**真正的进行中任务账本是 `docs/HANDOFF.local.md`**，被 `.gitignore` 忽略，只存在于当前机器的工作区，永远不进提交、不进 PR。

This file holds **only the protocol and the template** and is safe to merge upstream. **The live ledger is `docs/HANDOFF.local.md`**, which is gitignored: it never enters a commit or a PR.

## 为什么账本不入库 / Why the ledger stays local

- 账本内容是某台机器、某个开发者、某次会话的临时状态（串口号、本机工具链、未完成的半成品、agent 名字）。上游仓库不需要也不应该收到这些。
- 用 `.gitignore` 而不是"提交后 PR 时再剔除"：前者靠工具兜底，后者靠人记得，多 agent 场景下后者必失效。
- 需要跨机器交接时，把最新条目贴到 **PR 描述 / draft PR / issue 评论**，那是 GitHub 上合适的临时状态载体；不要为此把账本 `git add -f` 进分支。

## 接手方 / Taking over

1. `docs/HANDOFF.local.md` **不存在** → 这台机器上没有进行中的任务，从 [ONBOARDING.md §0](ONBOARDING.md#0-阅读顺序--reading-order) 正常开始。
2. 存在 → 读最新条目 → `git status --short` / `git diff --stat` 对照"涉及文件" → 重读涉及文件的 Frozen 段 → 在账本顶部追加"接手"条目 → 开工。
3. 账本最新条目明显比工作区状态（`git status`、`build/` 时间戳、终端历史）旧 → 前任没写交接。用你能拿到的证据**重建**一条，标题标注"（由 <你> 重建）"，写不出的字段填"未知"，再追加你自己的接手条目。
4. 完整步骤与检查单：[ONBOARDING.md §12.3–12.4](ONBOARDING.md#12-agent-协作与交接约定--multi-agent-collaboration--handoff)。

## 交接方 / Handing off

在切换 agent、结束会话、完成里程碑之前，把下面模板复制到 `docs/HANDOFF.local.md` 顶部（最新在上）。文件不存在就新建，第一行写 `# HANDOFF.local · 本机进行中任务账本（不入库）`。

```markdown
## YYYY-MM-DD HH:MM · <agent 或人名> · <任务短名>

- **分支 / 基线**：`<branch>` @ `<short-sha>`（`git rev-parse --short HEAD`）
- **目标**：一句话。
- **已完成**：
  - …
- **未完成 / 进行中**：
  - …
- **验证等级**：未编译 | 已编译 ci | 已编译 defaults（本机 IDF / docker run / devcontainer）| 已烧写未验证启动 | 真机已验证（板 RDP-G01-W，IDF v6.1，COMx，USB 供电；验证了 …；未覆盖 …）
- **烧写记录**（本次没烧写就写"无"）：
  - 命令原文：`cd build; python -m esptool … write-flash '@flash_args'`（必须包含地址或 `@flash_args`）
  - 串口里看到的开机日志片段（至少 `Loaded app from partition at offset 0x10000` 与 `UI ready on …`）/ "没看串口"
- **涉及文件**（新建标 N，修改标 M）：
  - M `main/apps/app_xxx.c`
- **Frozen 触碰**：无 | `app_xxx.c` 文件头第 N 行，原因：…
- **硬约束自检**：ONBOARDING §9 全部通过 | 例外：…
- **设备状态**：在跑 <哪个 build>；串口已释放；是否需要重新烧写。
- **下一步（可直接执行）**：
  1. …
- **未决问题**：
  - Q：… 倾向：… 理由：…
- **环境备注**：本机 idf.py 是否可用、串口号、容器状态、其他 agent 是否在并行。
```

## 规则 / Rules

- 不要删除账本里的历史条目；任务彻底关闭后移到账本的末尾"已归档"部分。
- 不要在本文件里写任何具体任务状态。看到有人写了，移到 `HANDOFF.local.md`。
- 不要把 `HANDOFF.local.md` 从 `.gitignore` 里拿掉，也不要 `git add -f` 它。
- 其他 agent 的入口文件（`CLAUDE.md`、`.cursorrules` 等）如需提到交接，只写一行指向本文件，不复制模板。

## 内部记录与对外交接 / Internal records and external handoff

计划、检查点、过程审查与验收结果保存在被忽略的 `docs/local/`；本机当前状态仍由 `HANDOFF.local.md` 提供入口。正式功能说明不能依赖这些本地文件，版本变化只在 `docs/CHANGELOG.md` 轻量记录。
Keep plans, checkpoints, review records and acceptance results in ignored `docs/local/`, with current local state linked from `HANDOFF.local.md`. Published feature documentation must be self-contained; keep version changes brief in `docs/CHANGELOG.md`.

需要把未完功能或维护责任交给他人时，按本协议提供最小摘要：功能边界、当前实现、未完成事项、必要验证结论及下一步。通过PR描述或约定的交接渠道传递，不把完整内部计划、聊天、调试日志或过程账本加入产品提交。
When handing unfinished functionality or maintenance responsibility to someone else, provide a minimal summary of scope, implementation, remaining work, relevant validation and next steps. Use the PR description or agreed handoff channel rather than committing full internal plans, conversations or debug logs.
