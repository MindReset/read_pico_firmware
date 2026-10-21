/* 中文：调度测试替身。/ English: Scheduler test shim. */
#pragma once
#include "common.h"
// 横屏旋转 API 替身：枚举与 components/epdiy/src/epdiy.h 逐值一致；三个函数的
// 定义与可切换状态在 app_loop_host_test.c（其余宿主 harness 不调用它们）。
// 逻辑帧尺寸随方向变——横屏 1216x684、竖屏 684x1216——与 epdiy 的
// epd_width/epd_height 语义一致（app_loop 的横屏映射依赖这一点）。
// Landscape rotation API shim: the enum matches components/epdiy/src/epdiy.h
// value-for-value; the three definitions and their switchable state live in
// app_loop_host_test.c (no other host harness calls them). Logical frame
// sizes follow the rotation - landscape 1216x684, portrait 684x1216 -
// matching epdiy semantics, which app_loop's landscape remap relies on.
enum EpdRotation {
    EPD_ROT_LANDSCAPE = 0,
    EPD_ROT_PORTRAIT = 1,
    EPD_ROT_INVERTED_LANDSCAPE = 2,
    EPD_ROT_INVERTED_PORTRAIT = 3,
};
int epd_width(void);
int epd_height(void);
enum EpdRotation epd_get_rotation(void);
// prepare_lock / book_on_exit 的横屏兜底复位走这个入口；与 epdiy 真实 API 一致
// 是纯状态设置（无刷新副作用）。/ The landscape fallback reset in prepare_lock /
// book_on_exit enters here; like the real API it is a pure state set (no refresh).
void epd_set_rotation(enum EpdRotation rotation);
