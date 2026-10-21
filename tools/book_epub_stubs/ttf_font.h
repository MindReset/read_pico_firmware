/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：宿主书源测试的字体契约替身，只为让 book_epub.c 编过。
 * English: Font contract shim so book_epub.c compiles in the host book-source test.
 * 冻结：仅供测试，不做任何字体工作。/ Frozen: tests only; no font work happens here.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void ttf_font_close_embedded(void);
/// 宿主替身：一律拒绝，书内字体在测试里回退系统字体。
/// Host shim: always declines, so embedded faces fall back to the system face under test.
int ttf_font_open_mem(uint8_t* data, size_t len, const char* label);
