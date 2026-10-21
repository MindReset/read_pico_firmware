#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
"""从 vendor 的 Lucide SVG 生成固件图标掩模。

中文：这是全机唯一的图标来源。tools/icons/lucide/*.svg 是 Lucide 原图（ISC），
本脚本把它们的描边光栅化成 4bpp alpha 掩模，输出 main/assets/ui_icons.h。
不依赖 cairosvg / svglib：路径解析、圆弧转三次贝塞尔、超采样描边都在这里实现，
所以任何机器上跑出来的字节完全一致。

English: The single icon source for the whole firmware. tools/icons/lucide/*.svg
are upstream Lucide artworks (ISC); this script strokes and rasterizes them into
4bpp alpha masks for main/assets/ui_icons.h. No cairosvg or svglib dependency:
path parsing, arc-to-cubic conversion and supersampled stroking all live here, so
the output bytes are identical on every machine.

冻结：不要手改 main/assets/ui_icons.h。加图标只改下面的 ICONS 清单并重跑。
Frozen: never hand-edit main/assets/ui_icons.h. Add an icon by extending ICONS and
re-running this script.
"""
from __future__ import annotations

import math
import re
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
SOURCE_DIR = ROOT / "tools/icons/lucide"
HEADER = ROOT / "main/assets/ui_icons.h"
PREVIEW = ROOT / "docs/local/ui-icons-preview.png"

# 掩模边长。显示端最大画到 72px（电源弹窗的两枚大图标），其余都在 48px 以下，
# 所以每次绘制都是降采样，不会出现放大糊边。
# Mask side. The largest on-screen draw is 72 px (the two power-dialog marks) and
# everything else stays under 48 px, so every draw is a downscale with no upscale blur.
MASK_SIZE = 72
# 描边超采样倍率：先按 8 倍画，再 LANCZOS 缩回来做抗锯齿。
# Stroke supersampling: draw at 8x, then LANCZOS back down for antialiasing.
SUPERSAMPLE = 8
# Lucide 画布边长。/ Lucide canvas side.
VIEWBOX = 24.0
# 三次贝塞尔和圆弧的展平段数，够 384px 画布用。
# Flattening resolution for cubics and arcs; plenty for a 384px canvas.
CURVE_STEPS = 24

# 图标清单，顺序即 ui_icon_t 的枚举顺序。名字必须是 tools/icons/lucide/<name>.svg。
# Icon list; order defines the ui_icon_t enum. Each name must exist as a vendored SVG.
ICONS = (
    "house",             # 底栏·首页 / nav home
    "library-big",       # 底栏·书架、书架样式 / nav shelf, shelf style
    "folder-open",       # 底栏·文件管理 / nav files
    "sliders-horizontal",# 底栏·设置 / nav settings
    "chevron-left",      # 返回 / back
    "chevron-up",        # 菜单把手·展开 / menu handle, open
    "chevron-down",      # 菜单把手·收起 / menu handle, close
    "wifi",              # 状态栏无线、无线连接 / status wifi, wireless setting
    "bluetooth",         # 蓝牙 / bluetooth
    "type",              # 系统字体 / system font
    "a-large-small",     # 系统字号 / system font size
    "ticket",            # 锁屏样式·阅读票根 / lock style ticket
    "eye-closed",          # 自动休眠锁屏 / auto sleep lock
    "lock",              # 锁屏密码 / lock screen password
    "clock",             # 日期与时间 / date and time
    "contrast",          # 系统对比度 / system contrast
    "copy",              # 保存与恢复 / save and restore
    "power",             # 关机睡眠 / shutdown and sleep
    "text-align-start",  # 状态栏签名 / status bar signature
    "refresh-cw",        # 首页强刷、阅读刷新设置 / home full refresh, reader refresh settings
    "zap",               # 电池充电闪电 / battery charging bolt
    "list",              # 阅读工具栏·目录 / reader toolbar, table of contents
    "bookmark",          # 阅读工具栏·书签（未保存）/ reader toolbar, bookmark
    "bookmark-check",    # 阅读工具栏·书签（已保存）/ reader toolbar, saved bookmark
    "chart-column",      # 阅读工具栏·阅读统计 / reader toolbar, reading statistics
    "folder",            # 文件管理·文件夹行 / file manager, folder row
    "file",              # 文件管理·文件行 / file manager, file row
    "rotate-cw",         # 电源弹窗·重启 / power dialog, restart
    "radio-tower",       # 传书页·创建热点 / transfer page, create hotspot
    "chevron-right",     # 书架下一页 / next shelf page
    "download",          # 系统升级 / system upgrade
    "cpu",               # BOOT 刷机 / boot flashing
    "book-open",         # 阅读设置 / reading settings
    "timer",             # 自动休眠 / idle lock
)

_NUMBER = re.compile(r"[-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?")
_COMMAND = re.compile(r"[MmZzLlHhVvCcSsQqTtAa]")


# ---------------------------------------------------------------- SVG 解析 / SVG parsing

class _PathReader:
    """SVG 路径串游标。

    中文：圆弧的 large-arc 和 sweep 是单字符标志位，SVG 允许连写成 `00` 或 `01`，
    所以这两个必须按字符读，不能当数字一起分词（zap.svg 就用了 `0 00-2.474`）。
    其余参数仍按数字扫描，处理 `1.5.5`、`-3`、`.5` 这类紧凑写法。

    English: An arc's large-arc and sweep are single-character flags and SVG lets
    them run together as `00` or `01`, so they must be read character by character
    instead of being tokenized as numbers (zap.svg writes `0 00-2.474`). All other
    parameters are scanned as numbers, which covers compact forms like `1.5.5`.
    """

    def __init__(self, d: str) -> None:
        self.d = d
        self.i = 0

    def _skip(self) -> None:
        while self.i < len(self.d) and self.d[self.i] in " \t\r\n,":
            self.i += 1

    def eof(self) -> bool:
        self._skip()
        return self.i >= len(self.d)

    def at_command(self) -> bool:
        self._skip()
        return self.i < len(self.d) and _COMMAND.match(self.d[self.i]) is not None

    def command(self) -> str:
        self._skip()
        letter = self.d[self.i]
        self.i += 1
        return letter

    def number(self) -> float:
        self._skip()
        match = _NUMBER.match(self.d, self.i)
        if match is None:
            raise ValueError(
                f"expected a number at {self.i}: {self.d[self.i:self.i + 12]!r}"
            )
        self.i = match.end()
        return float(match.group())

    def flag(self) -> bool:
        self._skip()
        if self.i < len(self.d) and self.d[self.i] in "01":
            self.i += 1
            return self.d[self.i - 1] == "1"
        raise ValueError(
            f"expected an arc flag at {self.i}: {self.d[self.i:self.i + 12]!r}"
        )


def _arc_to_cubics(
    x1: float, y1: float, rx: float, ry: float, phi_deg: float,
    large_arc: bool, sweep: bool, x2: float, y2: float,
) -> list[tuple[float, float, float, float, float, float]]:
    """SVG 椭圆弧转三次贝塞尔（规范 F.6.5）。返回 (c1x,c1y,c2x,c2y,x,y) 列表。"""
    if rx == 0 or ry == 0:
        return [(x1, y1, x2, y2, x2, y2)]
    rx, ry = abs(rx), abs(ry)
    phi = math.radians(phi_deg)
    cos_phi, sin_phi = math.cos(phi), math.sin(phi)

    # 步骤 1：把端点变到椭圆坐标系。
    dx2, dy2 = (x1 - x2) / 2.0, (y1 - y2) / 2.0
    x1p = cos_phi * dx2 + sin_phi * dy2
    y1p = -sin_phi * dx2 + cos_phi * dy2

    # 步骤 2：半径不足时按比例放大。
    lam = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry)
    if lam > 1.0:
        scale = math.sqrt(lam)
        rx *= scale
        ry *= scale

    # 步骤 3：求圆心。
    num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p
    den = rx * rx * y1p * y1p + ry * ry * x1p * x1p
    factor = math.sqrt(max(0.0, num / den)) if den else 0.0
    if large_arc == sweep:
        factor = -factor
    cxp = factor * rx * y1p / ry
    cyp = -factor * ry * x1p / rx
    cx = cos_phi * cxp - sin_phi * cyp + (x1 + x2) / 2.0
    cy = sin_phi * cxp + cos_phi * cyp + (y1 + y2) / 2.0

    # 步骤 4：起止角。
    def angle(ux: float, uy: float, vx: float, vy: float) -> float:
        dot = ux * vx + uy * vy
        norm = math.hypot(ux, uy) * math.hypot(vx, vy)
        if norm == 0:
            return 0.0
        value = max(-1.0, min(1.0, dot / norm))
        sign = -1.0 if (ux * vy - uy * vx) < 0 else 1.0
        return sign * math.acos(value)

    theta1 = angle(1.0, 0.0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    delta = angle(
        (x1p - cxp) / rx, (y1p - cyp) / ry,
        (-x1p - cxp) / rx, (-y1p - cyp) / ry,
    )
    if not sweep and delta > 0:
        delta -= 2 * math.pi
    elif sweep and delta < 0:
        delta += 2 * math.pi

    # 步骤 5：按不超过 90 度一段切分，每段用标准三次贝塞尔逼近。
    segments = max(1, int(math.ceil(abs(delta) / (math.pi / 2.0))))
    step = delta / segments
    alpha = 4.0 / 3.0 * math.tan(step / 4.0)

    def point(t: float) -> tuple[float, float]:
        cos_t, sin_t = math.cos(t), math.sin(t)
        return (
            cx + rx * cos_t * cos_phi - ry * sin_t * sin_phi,
            cy + rx * cos_t * sin_phi + ry * sin_t * cos_phi,
        )

    def derivative(t: float) -> tuple[float, float]:
        cos_t, sin_t = math.cos(t), math.sin(t)
        return (
            -rx * sin_t * cos_phi - ry * cos_t * sin_phi,
            -rx * sin_t * sin_phi + ry * cos_t * cos_phi,
        )

    cubics = []
    theta = theta1
    for _ in range(segments):
        theta_next = theta + step
        px, py = point(theta)
        qx, qy = point(theta_next)
        dx1, dy1 = derivative(theta)
        dx3, dy3 = derivative(theta_next)
        cubics.append((
            px + alpha * dx1, py + alpha * dy1,
            qx - alpha * dx3, qy - alpha * dy3,
            qx, qy,
        ))
        theta = theta_next
    return cubics


def _flatten_cubic(
    x0: float, y0: float, c1x: float, c1y: float,
    c2x: float, c2y: float, x1: float, y1: float,
) -> list[tuple[float, float]]:
    """三次贝塞尔均匀取样，丢掉起点（调用方已经有了）。"""
    points = []
    for i in range(1, CURVE_STEPS + 1):
        t = i / CURVE_STEPS
        u = 1.0 - t
        a, b, c, d = u * u * u, 3 * u * u * t, 3 * u * t * t, t * t * t
        points.append((
            a * x0 + b * c1x + c * c2x + d * x1,
            a * y0 + b * c1y + c * c2y + d * y1,
        ))
    return points


def parse_path(d: str) -> list[tuple[list[tuple[float, float]], bool]]:
    """把 d 解析成子路径列表；每项是 (点列, 是否闭合)。"""
    reader = _PathReader(d)
    subpaths: list[tuple[list[tuple[float, float]], bool]] = []
    current: list[tuple[float, float]] = []
    x = y = 0.0
    start_x = start_y = 0.0
    prev_c2: tuple[float, float] | None = None
    prev_q: tuple[float, float] | None = None
    command = ""

    def flush(closed: bool) -> None:
        nonlocal current
        # 单点且不闭合的子路径是噪声，丢掉；闭合子路径哪怕只有一点也不丢。
        # A single unclosed point is noise; keep every closed subpath.
        if len(current) > 1 or (current and closed):
            subpaths.append((current, closed))
        current = []

    while not reader.eof():
        if reader.at_command():
            command = reader.command()
            if command.upper() == "Z":
                flush(True)
                x, y = start_x, start_y
                current = []
                prev_c2 = prev_q = None
                # Z 不接受参数，不能隐式重复。/ Z takes no arguments and never repeats implicitly.
                command = ""
                continue
        elif not command:
            raise ValueError("path data does not start with a command")

        relative = command.islower()
        letter = command.upper()

        if letter == "M":
            mx, my = reader.number(), reader.number()
            if relative:
                mx, my = x + mx, y + my
            flush(False)
            x = start_x = mx
            y = start_y = my
            current = [(x, y)]
            # M 之后的坐标对按 L 处理。/ Coordinate pairs after M behave as L.
            command = "l" if relative else "L"
            continue

        if letter == "L":
            lx, ly = reader.number(), reader.number()
            if relative:
                lx, ly = x + lx, y + ly
            x, y = lx, ly
            current.append((x, y))
        elif letter == "H":
            hx = reader.number()
            x = x + hx if relative else hx
            current.append((x, y))
        elif letter == "V":
            vy = reader.number()
            y = y + vy if relative else vy
            current.append((x, y))
        elif letter == "C":
            c1x, c1y = reader.number(), reader.number()
            c2x, c2y = reader.number(), reader.number()
            ex, ey = reader.number(), reader.number()
            if relative:
                c1x, c1y = x + c1x, y + c1y
                c2x, c2y = x + c2x, y + c2y
                ex, ey = x + ex, y + ey
            current.extend(_flatten_cubic(x, y, c1x, c1y, c2x, c2y, ex, ey))
            prev_c2, prev_q = (c2x, c2y), None
            x, y = ex, ey
        elif letter == "S":
            c2x, c2y = reader.number(), reader.number()
            ex, ey = reader.number(), reader.number()
            if relative:
                c2x, c2y = x + c2x, y + c2y
                ex, ey = x + ex, y + ey
            c1x, c1y = (2 * x - prev_c2[0], 2 * y - prev_c2[1]) if prev_c2 else (x, y)
            current.extend(_flatten_cubic(x, y, c1x, c1y, c2x, c2y, ex, ey))
            prev_c2, prev_q = (c2x, c2y), None
            x, y = ex, ey
        elif letter == "Q":
            qx, qy = reader.number(), reader.number()
            ex, ey = reader.number(), reader.number()
            if relative:
                qx, qy = x + qx, y + qy
                ex, ey = x + ex, y + ey
            c1x, c1y = x + 2.0 / 3.0 * (qx - x), y + 2.0 / 3.0 * (qy - y)
            c2x, c2y = ex + 2.0 / 3.0 * (qx - ex), ey + 2.0 / 3.0 * (qy - ey)
            current.extend(_flatten_cubic(x, y, c1x, c1y, c2x, c2y, ex, ey))
            prev_q, prev_c2 = (qx, qy), None
            x, y = ex, ey
        elif letter == "T":
            ex, ey = reader.number(), reader.number()
            if relative:
                ex, ey = x + ex, y + ey
            qx, qy = (2 * x - prev_q[0], 2 * y - prev_q[1]) if prev_q else (x, y)
            c1x, c1y = x + 2.0 / 3.0 * (qx - x), y + 2.0 / 3.0 * (qy - y)
            c2x, c2y = ex + 2.0 / 3.0 * (qx - ex), ey + 2.0 / 3.0 * (qy - ey)
            current.extend(_flatten_cubic(x, y, c1x, c1y, c2x, c2y, ex, ey))
            prev_q, prev_c2 = (qx, qy), None
            x, y = ex, ey
        elif letter == "A":
            rx, ry = reader.number(), reader.number()
            rotation = reader.number()
            large, sweep = reader.flag(), reader.flag()
            ex, ey = reader.number(), reader.number()
            if relative:
                ex, ey = x + ex, y + ey
            for c1x, c1y, c2x, c2y, px, py in _arc_to_cubics(
                x, y, rx, ry, rotation, large, sweep, ex, ey
            ):
                current.extend(_flatten_cubic(x, y, c1x, c1y, c2x, c2y, px, py))
                x, y = px, py
            prev_c2 = prev_q = None
        else:
            raise ValueError(f"unsupported path command {command!r}")

    flush(False)
    return subpaths


def _rounded_rect_points(
    x: float, y: float, w: float, h: float, rx: float, ry: float
) -> list[tuple[float, float]]:
    """圆角矩形轮廓转点列（顺时针）。"""
    rx = min(rx, w / 2.0)
    ry = min(ry, h / 2.0)
    points: list[tuple[float, float]] = []
    corners = (
        (x + w - rx, y + ry, -math.pi / 2.0, 0.0),
        (x + w - rx, y + h - ry, 0.0, math.pi / 2.0),
        (x + rx, y + h - ry, math.pi / 2.0, math.pi),
        (x + rx, y + ry, math.pi, 1.5 * math.pi),
    )
    for cx, cy, a0, a1 in corners:
        for step in range(CURVE_STEPS + 1):
            angle = a0 + (a1 - a0) * step / CURVE_STEPS
            points.append((cx + rx * math.cos(angle), cy + ry * math.sin(angle)))
    return points


def _ellipse_points(cx: float, cy: float, rx: float, ry: float) -> list[tuple[float, float]]:
    """椭圆轮廓转点列。"""
    return [
        (cx + rx * math.cos(2 * math.pi * i / 96),
         cy + ry * math.sin(2 * math.pi * i / 96))
        for i in range(96)
    ]


def _attributes(text: str) -> dict[str, str]:
    return dict(re.findall(r'([a-zA-Z-]+)="([^"]*)"', text))


def _number(attrs: dict[str, str], key: str, default: float = 0.0) -> float:
    try:
        return float(attrs[key])
    except (KeyError, ValueError):
        return default


def element_subpaths(tag: str, attrs: dict[str, str]) -> list[tuple[list[tuple[float, float]], bool]]:
    """把一个 SVG 元素统一成点列子路径。"""
    if tag == "path":
        return parse_path(attrs.get("d", ""))
    if tag == "circle":
        return [(_ellipse_points(
            _number(attrs, "cx"), _number(attrs, "cy"), _number(attrs, "r"), _number(attrs, "r")
        ), True)]
    if tag == "ellipse":
        return [(_ellipse_points(
            _number(attrs, "cx"), _number(attrs, "cy"),
            _number(attrs, "rx"), _number(attrs, "ry")
        ), True)]
    if tag == "line":
        return [([
            (_number(attrs, "x1"), _number(attrs, "y1")),
            (_number(attrs, "x2"), _number(attrs, "y2")),
        ], False)]
    if tag in ("polyline", "polygon"):
        raw = [float(v) for v in _NUMBER.findall(attrs.get("points", ""))]
        points = list(zip(raw[0::2], raw[1::2]))
        return [(points, tag == "polygon")]
    if tag == "rect":
        rx = _number(attrs, "rx", 0.0) or _number(attrs, "ry", 0.0)
        ry = _number(attrs, "ry", 0.0) or rx
        return [(_rounded_rect_points(
            _number(attrs, "x"), _number(attrs, "y"),
            _number(attrs, "width"), _number(attrs, "height"), rx, ry
        ), True)]
    raise ValueError(f"unsupported SVG element <{tag}>")


def parse_svg(text: str) -> tuple[float, list[tuple[str, dict[str, str]]]]:
    """取出 stroke-width 和全部可绘制元素。"""
    root = _attributes(text[:text.index(">") + 1])
    stroke_width = float(root.get("stroke-width", "2"))
    elements = [
        (match.group(1), _attributes(match.group(0)))
        for match in re.finditer(
            r"<(path|circle|ellipse|line|polyline|polygon|rect)\b[^>]*>", text
        )
    ]
    if not elements:
        raise ValueError("no drawable elements")
    return stroke_width, elements


# ---------------------------------------------------------------- 光栅化 / Rasterizing

def render_icon(name: str) -> Image.Image:
    """把一个 Lucide 图标描边光栅化成 MASK_SIZE 见方的 8 位 alpha 图。"""
    text = (SOURCE_DIR / f"{name}.svg").read_text(encoding="utf-8")
    stroke_width, elements = parse_svg(text)

    side = MASK_SIZE * SUPERSAMPLE
    scale = side / VIEWBOX
    width = max(1, int(round(stroke_width * scale)))
    canvas = Image.new("L", (side, side), 0)
    draw = ImageDraw.Draw(canvas)
    radius = width / 2.0

    for tag, attrs in elements:
        for points, closed in element_subpaths(tag, attrs):
            if not points:
                continue
            device = [(px * scale, py * scale) for px, py in points]
            if len(device) == 1 or (
                len(device) == 2
                and math.dist(device[0], device[1]) < radius
            ):
                # 「M12 20h.01」这类退化子路径是圆头点。/ A degenerate subpath is a round dot.
                cx, cy = device[0]
                draw.ellipse((cx - radius, cy - radius, cx + radius, cy + radius), fill=255)
                continue
            path = device + [device[0]] if closed else device
            draw.line(path, fill=255, width=width, joint="curve")
            if not closed:
                # 平头改圆头，和 Lucide 的 stroke-linecap="round" 一致。
                # Round caps, matching Lucide's stroke-linecap="round".
                for cx, cy in (device[0], device[-1]):
                    draw.ellipse((cx - radius, cy - radius, cx + radius, cy + radius), fill=255)

    return canvas.resize((MASK_SIZE, MASK_SIZE), Image.Resampling.LANCZOS)


def pack_4bpp(image: Image.Image) -> list[int]:
    """8 位 alpha 量化为 4bpp：高四位是左像素，低四位是右像素。0=透明，15=实心。"""
    values = [max(0, min(15, round(v * 15 / 255))) for v in image.get_flattened_data()]
    return [(values[i] << 4) | values[i + 1] for i in range(0, len(values), 2)]


# ---------------------------------------------------------------- 输出 / Emitting

def emit_header(masks: dict[str, list[int]]) -> None:
    lines = [
        "/*",
        " * SPDX-FileCopyrightText: 2026 mindreset",
        " * SPDX-License-Identifier: Apache-2.0",
        " *",
        " * 中文：由 tools/gen_ui_icons.py 从 tools/icons/lucide 下的 SVG 生成，勿手改。",
        " * 4bpp alpha 掩模，每像素 0=透明、15=实心，左上角起逐行、两个像素一字节。",
        " *",
        " * English: Generated by tools/gen_ui_icons.py from the SVG sources vendored in",
        " * tools/icons/lucide; do not edit by hand. 4bpp alpha masks, 0 = transparent",
        " * and 15 = solid, row-major from the top-left, two pixels per byte.",
        " *",
        " * 冻结：图标来自 Lucide（ISC），署名见 THIRD_PARTY_NOTICES.md。",
        " * Frozen: icons come from Lucide (ISC); credit lives in THIRD_PARTY_NOTICES.md.",
        " */",
        "#pragma once",
        "#include <stdint.h>",
        "",
        f"#define UI_ICON_MASK_SIZE {MASK_SIZE}",
        "#define UI_ICON_MASK_BYTES (UI_ICON_MASK_SIZE * UI_ICON_MASK_SIZE / 2)",
        "",
        "typedef enum {",
    ]
    for name in masks:
        lines.append(f"    UI_ICON_{name.replace('-', '_').upper()},  // lucide/{name}")
    lines += [
        "    UI_ICON_COUNT,",
        "} ui_icon_t;",
        "",
        "static const uint8_t ui_icon_masks[UI_ICON_COUNT][UI_ICON_MASK_BYTES] = {",
    ]
    for name, packed in masks.items():
        lines.append(f"    {{  // {name}")
        for start in range(0, len(packed), 20):
            row = ", ".join(f"0x{byte:02x}" for byte in packed[start:start + 20])
            lines.append(f"        {row},")
        lines.append("    },")
    lines.append("};")
    HEADER.write_text("\n".join(lines) + "\n", encoding="utf-8")


def emit_preview(masks: dict[str, list[int]]) -> None:
    """给人工验收用的对照图：真实尺寸 + 放大两档。"""
    PREVIEW.parent.mkdir(parents=True, exist_ok=True)
    columns = 5
    cell_w, cell_h = 200, 200
    rows = (len(masks) + columns - 1) // columns
    sheet = Image.new("RGB", (columns * cell_w, rows * cell_h), "white")
    for index, name in enumerate(masks):
        alpha = Image.new("L", (MASK_SIZE, MASK_SIZE))
        alpha.putdata([v * 17 for v in _unpack(masks[name], MASK_SIZE * MASK_SIZE)])
        ink = Image.new("RGB", (MASK_SIZE, MASK_SIZE), "white")
        ink.paste((30, 30, 30), (0, 0), alpha)
        big = ink.resize((112, 112), Image.Resampling.LANCZOS)
        small = ink.resize((40, 40), Image.Resampling.LANCZOS)
        col, row = index % columns, index // columns
        sheet.paste(big, (col * cell_w + 18, row * cell_h + 16))
        sheet.paste(small, (col * cell_w + 142, row * cell_h + 60))
    sheet.save(PREVIEW)


def _unpack(packed: list[int], count: int) -> list[int]:
    values = []
    for byte in packed:
        values.extend((byte >> 4, byte & 0x0F))
    return values[:count]


def main() -> None:
    missing = [n for n in ICONS if not (SOURCE_DIR / f"{n}.svg").exists()]
    if missing:
        raise SystemExit(
            "missing vendored sources: " + ", ".join(missing) +
            "\nrun: curl -O https://raw.githubusercontent.com/lucide-icons/lucide/main/icons/<name>.svg"
        )
    masks = {name: pack_4bpp(render_icon(name)) for name in ICONS}
    emit_header(masks)
    emit_preview(masks)
    print(f"{HEADER.relative_to(ROOT)}: {len(masks)} icons, "
          f"{len(masks) * len(next(iter(masks.values())))} bytes of mask data")
    print(PREVIEW.relative_to(ROOT))


if __name__ == "__main__":
    main()
