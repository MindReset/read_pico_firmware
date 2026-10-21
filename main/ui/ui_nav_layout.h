/* SPDX-License-Identifier: Apache-2.0
 * 中文：轻量底栏边界，绘制和刷新共用，防止局部扫描碰到固定图标。
 * English: Lightweight navigation bounds shared by painting and presentation to protect fixed icons.
 */
#pragma once
#define UI_NAV_TOP 1096
// 只扩大触控热区，不移动图标或改变内容/底栏的刷新边界。
// Expand touch admission only; icons and content/navigation refresh boundaries stay fixed.
#define UI_NAV_TOUCH_TOP (UI_NAV_TOP - 16)
#define UI_NAV_MARKER_TOP (UI_NAV_TOP + 1)
#define UI_NAV_MARKER_BOTTOM (UI_NAV_TOP + 6)
// 内容向内收至32列边界；横条单独短刷或并入快刷，外扩范围均止于图标首笔之前。
// Round content inward to 32 columns; marker-only or fused fast scans end before any icon ink.
#define UI_NAV_REFRESH_END (UI_NAV_TOP & ~31)
#define UI_NAV_MARKER_SCAN_END ((UI_NAV_MARKER_BOTTOM + 31) & ~31)
