# Vendored Lucide sources / Lucide 原图

中文：这里是固件全部界面图标的唯一来源。图标取自上游 Lucide 仓库，按文件逐字保存，
`tools/gen_ui_icons.py` 把它们描边光栅化成 `main/assets/ui_icons.h`。

English: The single source for every UI icon in the firmware. The SVG files are
verbatim copies from the upstream Lucide repository; `tools/gen_ui_icons.py`
strokes and rasterizes them into `main/assets/ui_icons.h`.

- 上游 / Upstream: <https://github.com/lucide-icons/lucide> (`icons/<name>.svg`)
- 许可 / License: ISC，全文见 [LICENSE](LICENSE)；署名见 [`THIRD_PARTY_NOTICES.md`](../../../THIRD_PARTY_NOTICES.md)
- 画布约定 / Canvas: `viewBox="0 0 24 24"`，`stroke-width="2"`，圆头圆角，无填充

## 加图标 / Adding an icon

1. 下载上游原图，文件名就是图标名：
   Download the upstream file; its name is the icon name:

   ```
   curl -O https://raw.githubusercontent.com/lucide-icons/lucide/main/icons/<name>.svg
   ```

2. 把 `<name>` 加进 `tools/gen_ui_icons.py` 的 `ICONS`，并在用它的页面里引用生成的
   `UI_ICON_<NAME>`。
   Add `<name>` to `ICONS` in `tools/gen_ui_icons.py` and use the generated
   `UI_ICON_<NAME>` at the call site.

3. 重新生成 / Regenerate:

   ```
   python tools/gen_ui_icons.py
   ```

生成器会校验每个清单项都有对应的 SVG，缺文件直接报错。
The generator fails loudly when a listed icon has no vendored SVG.

不要手改 `main/assets/ui_icons.h`，也不要在别处新增手绘图标：风格统一靠的是
「所有图标都来自这里」这一条。
Do not hand-edit `main/assets/ui_icons.h` and do not add hand-drawn icons anywhere
else; the consistent look depends on every icon coming from this directory.
