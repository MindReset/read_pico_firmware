/* 中文：调度测试替身。/ English: Scheduler test shim. */
#pragma once
#include "common.h"
#define UI_LONG_PRESS_MS 500
#define UI_SWIPE_MIN_PX 120
#define UI_TOUCH_SLOP_PX 24
EpdRect ui_content_refresh_area(void);

#define UI_SETTLE_DU_MAX 6
#define UI_SETTLE_IDLE_MS 2000
