/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：宿主布局测试的显示类型。/ English: Display types for host layout tests.
 * 冻结：仅供测试。/ Frozen: Tests only.
 */
#pragma once
#include <stdint.h>
typedef struct { int x, y, width, height; } EpdRect;
enum EpdFontFlags { EPD_DRAW_ALIGN_LEFT = 0 };
extern int test_guide_segments;
extern int test_guide_first_x;
extern int test_guide_first_y;
extern int test_guide_second_y;
extern int test_guide_last_y;
extern int test_guide_height;
extern uint8_t test_guide_gray;
static inline void epd_fill_rect(EpdRect rect, uint8_t gray, uint8_t* fb) {
    (void)fb;
    if (!test_guide_segments) {
        test_guide_first_x = rect.x;
        test_guide_first_y = rect.y;
        test_guide_height = rect.height;
        test_guide_gray = gray;
    } else if (test_guide_segments == 1) {
        test_guide_second_y = rect.y;
    }
    test_guide_last_y = rect.y;
    ++test_guide_segments;
}
