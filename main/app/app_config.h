/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 应用界面刷新档位。日常用均衡配置；ALL_DU 只做整机 DU 实验。
 *
 * App refresh profile. Daily use is balanced; ALL_DU is a whole-device
 * DU experiment only.
 */

#pragma once

#include "epdiy.h"

// 常规页面用 GL16；阅读翻页用 DU，并在翻页当下周期性 GC16 压残影，不做延迟二次刷新。
// KEY2、铺白、睡眠画面这类「清屏重来」仍直接走 GC16。
// General pages use GL16; reading turns use DU with immediate periodic GC16 ghost cleanup and no delayed second refresh.
// KEY2, wipe-to-white and sleep faces still go GC16.
#define APP_REFRESH_BALANCED 0
#define APP_REFRESH_ALL_DU 1
#define APP_REFRESH_PROFILE APP_REFRESH_BALANCED

// 用户可选择普通灰阶或主页面黑白快刷，默认普通；阅读与图片保持各自刷新设置。
// Users choose ordinary grays or monochrome main-page fast refresh, defaulting to ordinary; reader/images keep their own settings.


#if APP_REFRESH_PROFILE == APP_REFRESH_ALL_DU
#define APP_PAGE_REFRESH_MODE MODE_DU
#define APP_PAGE_FORCE_FULL 0
#define APP_SETTLE_REFRESH_MODE MODE_DU
#else
#define APP_PAGE_REFRESH_MODE MODE_GL16
// APP_REDRAW_FULL（KEY2 清残影、首帧、换字体、enter_full 进页）仍是整屏 GC16。
// APP_REDRAW_FULL (KEY2, first frame, font change, enter_full) stays GC16.
#define APP_PAGE_FORCE_FULL 1
// 触摸抬手定稿：笔迹连续 DU 之后白底已脏，顺手用 GC16 清一次。
// Touch settle: continuous DU dirties the white; GC16 once on lift.
#define APP_SETTLE_REFRESH_MODE MODE_GC16
#endif

// 手指、传感器等连续变化过程始终优先响应速度。/ Motion always prefers DU.
#define APP_DYNAMIC_REFRESH_MODE MODE_DU

// 系统页面只统计整页切换；按钮、弹窗和封面等局部刷新不累计，避免几次操作后就黑白全刷。
// 阅读页另有独立的可选整屏清残影周期。
// Count only whole system-page transitions; local controls, dialogs and cover updates do not advance
// the cleanup cadence. Reading keeps its own configurable full-screen cleanup interval.
#define APP_UI_GC16_EVERY 40
// 非普通子页面的旧灰阶出口每三次做全像素整理；普通模式已改用单次柔和差分，仅保留下面的完整清残影周期。
// Legacy nonordinary subpage output settles every third page; ordinary uses single soft differences with only the full-cleanup interval below.
#define APP_UI_FAST_GL16_SETTLE_EVERY 3
// 全像素灰阶仍不能无限消除累积残影，达到阈值时同步做完整 GC16。
// Full-pixel gray updates cannot remove unlimited ghosting, so periodically run a synchronous GC16.
#define APP_UI_FAST_GC16_EVERY 12
